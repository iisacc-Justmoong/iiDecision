#include "Backend.h"
#ifdef IIDECISION_HAS_OPENCL
#define CL_TARGET_OPENCL_VERSION 120
#define CL_SILENCE_DEPRECATION
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif
#include <limits>

namespace iiDecision::detail {
namespace {
void checked(cl_int status) { if (status != CL_SUCCESS) throw std::runtime_error("OpenCL error "+std::to_string(status)); }
template<class T, auto Release> struct Resource {
    T value = nullptr;
    Resource() = default;
    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;
    ~Resource() { if (value) Release(value); }
};
const char* source = R"(
__kernel void evidence_match(__global const uint* values, __global const uint* query,
    __global uint* mask, uint rows, uint columns) {
    size_t r = get_global_id(0);
    if (r >= rows) return;
    uint matched = 1;
    for (uint c = 0; c < columns; ++c)
        if (values[r * columns + c] != query[c]) { matched = 0; break; }
    mask[r] = matched;
})";
class OpenCl final : public IEvidenceBackend {
    BackendInfo info_;
    Resource<cl_context,clReleaseContext> context_;
    Resource<cl_command_queue,clReleaseCommandQueue> queue_;
    Resource<cl_program,clReleaseProgram> program_;
    Resource<cl_kernel,clReleaseKernel> kernel_;
public:
    OpenCl(cl_device_id device,const std::string& id) {
        std::size_t length = 0; checked(clGetDeviceInfo(device,CL_DEVICE_NAME,0,nullptr,&length));
        std::string name(length,'\0'); checked(clGetDeviceInfo(device,CL_DEVICE_NAME,length,name.data(),nullptr));
        if (!name.empty() && name.back() == '\0') name.pop_back();
        cl_device_type type; checked(clGetDeviceInfo(device,CL_DEVICE_TYPE,sizeof(type),&type,nullptr));
        info_ = {id,"OpenCL",name,(type & CL_DEVICE_TYPE_CPU) ? DeviceKind::Cpu :
            (type & CL_DEVICE_TYPE_GPU) ? DeviceKind::Gpu : DeviceKind::Managed,true,{}};
        cl_int status;
        context_.value = clCreateContext(nullptr,1,&device,nullptr,nullptr,&status); checked(status);
        queue_.value = clCreateCommandQueue(context_.value,device,0,&status); checked(status);
        program_.value = clCreateProgramWithSource(context_.value,1,&source,nullptr,&status); checked(status);
        status = clBuildProgram(program_.value,1,&device,"-cl-std=CL1.2",nullptr,nullptr);
        if (status != CL_SUCCESS) {
            clGetProgramBuildInfo(program_.value,device,CL_PROGRAM_BUILD_LOG,0,nullptr,&length);
            std::string log(length,'\0');
            clGetProgramBuildInfo(program_.value,device,CL_PROGRAM_BUILD_LOG,length,log.data(),nullptr);
            throw std::runtime_error("OpenCL build failed: "+log);
        }
        kernel_.value = clCreateKernel(program_.value,"evidence_match",&status); checked(status);
    }
    BackendInfo info() const override { return info_; }
    std::vector<std::uint8_t> match(std::size_t rows,std::span<const std::uint32_t> values,
        std::span<const std::uint32_t> evidence) override {
        if (rows > UINT32_MAX || evidence.size() > UINT32_MAX || values.size() > UINT32_MAX)
            throw std::length_error("OpenCL evidence matrix exceeds uint32 addressing");
        cl_int status;
        Resource<cl_mem,clReleaseMemObject> matrix,query,output;
        matrix.value = clCreateBuffer(context_.value,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,
            values.size_bytes(),const_cast<std::uint32_t*>(values.data()),&status); checked(status);
        query.value = clCreateBuffer(context_.value,CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR,
            evidence.size_bytes(),const_cast<std::uint32_t*>(evidence.data()),&status); checked(status);
        output.value = clCreateBuffer(context_.value,CL_MEM_WRITE_ONLY,rows*sizeof(cl_uint),nullptr,&status); checked(status);
        cl_uint row_count = static_cast<cl_uint>(rows), columns = static_cast<cl_uint>(evidence.size());
        checked(clSetKernelArg(kernel_.value,0,sizeof(cl_mem),&matrix.value));
        checked(clSetKernelArg(kernel_.value,1,sizeof(cl_mem),&query.value));
        checked(clSetKernelArg(kernel_.value,2,sizeof(cl_mem),&output.value));
        checked(clSetKernelArg(kernel_.value,3,sizeof(row_count),&row_count));
        checked(clSetKernelArg(kernel_.value,4,sizeof(columns),&columns));
        checked(clEnqueueNDRangeKernel(queue_.value,kernel_.value,1,nullptr,&rows,nullptr,0,nullptr,nullptr));
        std::vector<cl_uint> mask(rows);
        checked(clEnqueueReadBuffer(queue_.value,output.value,CL_TRUE,0,rows*sizeof(cl_uint),mask.data(),0,nullptr,nullptr));
        std::vector<std::uint8_t> result(rows);
        for (std::size_t r = 0; r < rows; ++r) {
            if (mask[r] > 1) throw std::runtime_error("OpenCL returned a nonbinary mask");
            result[r] = static_cast<std::uint8_t>(mask[r]);
        }
        return result;
    }
};
}
void addOpenCl(BackendList& result) {
    cl_uint count = 0;
    auto status = clGetPlatformIDs(0,nullptr,&count);
    if (status != CL_SUCCESS || !count) {
        result.push_back(std::make_shared<Unavailable>("opencl","OpenCL","no OpenCL platform: "+std::to_string(status))); return;
    }
    std::vector<cl_platform_id> platforms(count);
    checked(clGetPlatformIDs(count,platforms.data(),nullptr));
    std::size_t found = 0;
    for (std::size_t p = 0; p < platforms.size(); ++p) {
        cl_uint devices_count = 0;
        status = clGetDeviceIDs(platforms[p],CL_DEVICE_TYPE_ALL,0,nullptr,&devices_count);
        if (status != CL_SUCCESS || !devices_count) continue;
        std::vector<cl_device_id> devices(devices_count);
        checked(clGetDeviceIDs(platforms[p],CL_DEVICE_TYPE_ALL,devices_count,devices.data(),nullptr));
        for (std::size_t d = 0; d < devices.size(); ++d) {
            const auto id = "opencl/"+std::to_string(p)+"/"+std::to_string(d);
            try { result.push_back(std::make_shared<OpenCl>(devices[d],id)); }
            catch (const std::exception& error) { result.push_back(std::make_shared<Unavailable>(id,"OpenCL",error.what())); }
            ++found;
        }
    }
    if (!found) result.push_back(std::make_shared<Unavailable>("opencl","OpenCL","no OpenCL compute device detected"));
}
}
#else
namespace iiDecision::detail { void addOpenCl(BackendList& result) {
    result.push_back(std::make_shared<Unavailable>("opencl","OpenCL","built without OpenCL"));
} }
#endif
