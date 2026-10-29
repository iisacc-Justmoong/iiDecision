#include "GpuAdapter.h"
#include <iiGPU/GPU.h>
#include "EvidenceShader.h"
#include <array>
#include <iterator>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace iiDecision::gpu_adapter {
using BackendList = std::vector<std::shared_ptr<IEvidenceBackend>>;
using iiDecision::detail::evidence_shader;
namespace {
class Unavailable final : public IEvidenceBackend {
    BackendInfo info_;
public:
    Unavailable(std::string id,std::string api,std::string reason)
        : info_{std::move(id),std::move(api),{},DeviceKind::Managed,false,std::move(reason)} {}
    BackendInfo info() const override { return info_; }
    std::vector<std::uint8_t> match(std::size_t,std::span<const std::uint32_t>,
        std::span<const std::uint32_t>) override { throw std::runtime_error(info_.diagnostic); }
};
template<class T> T checked(ii::gpu::Result<T> result) {
    if(!result) throw std::runtime_error(result.error().message);
    return std::move(*result);
}
void checked(ii::gpu::Result<void> result) { if(!result) throw std::runtime_error(result.error().message); }
ii::gpu::ComputeDesc kernel() {
    ii::gpu::ComputeDesc desc;
    desc.metal_entry="evidence_match";
    desc.metal_source=R"(
        #include <metal_stdlib>
        using namespace metal;
        struct Shape {uint rows,columns;};
        kernel void evidence_match(device const uint* values [[buffer(0)]],
            device const uint* query [[buffer(1)]],device uint* mask [[buffer(2)]],
            constant Shape& shape [[buffer(3)]],uint r [[thread_position_in_grid]]) {
            if(r>=shape.rows) return;
            uint matched=1;
            for(uint c=0;c<shape.columns;++c)
                if(values[r*shape.columns+c]!=query[c]) {matched=0;break;}
            mask[r]=matched;
        })";
    desc.spirv.assign(std::begin(evidence_shader),std::end(evidence_shader));
    desc.bindings={ii::gpu::Access::read,ii::gpu::Access::read,ii::gpu::Access::write};
    desc.constants_bytes=8; return desc;
}
class Gpu final : public IEvidenceBackend {
    ii::gpu::Device device_;
    ii::gpu::ComputePipeline pipeline_;
    ii::gpu::Buffer matrix_,query_,output_;
    std::vector<std::uint32_t> scratch_;
    std::mutex mutex_;
    BackendInfo info_;
    void reserve(ii::gpu::Buffer& buffer,std::size_t bytes) {
        if(buffer&&buffer.size()>=bytes) return;
        // Each backend slot is serialized by EvidenceMatcher. Reuse persistent
        // buffers and the compiled pipeline across repeated queries.
        buffer={};
        buffer=checked(device_.buffer({bytes,ii::gpu::Memory::shared,ii::gpu::Usage::storage}));
    }
public:
    Gpu(ii::gpu::Backend api,const ii::gpu::Adapter& adapter,const std::string& prefix) {
        device_=checked(ii::gpu::Device::create({.backend=api,.adapter_index=adapter.index,.require_hardware=false}));
        pipeline_=checked(device_.compute_pipeline(kernel()));
        info_={prefix+"/"+std::to_string(adapter.index),ii::gpu::backend_name(api),adapter.name,
            adapter.hardware?DeviceKind::Gpu:DeviceKind::Cpu,true,{}};
    }
    BackendInfo info() const override { return info_; }
    std::vector<std::uint8_t> match(std::size_t rows,std::span<const std::uint32_t> values,
        std::span<const std::uint32_t> evidence) override {
        if(rows>UINT32_MAX||rows>std::numeric_limits<std::size_t>::max()/sizeof(std::uint32_t)||values.size()>UINT32_MAX||evidence.size()>UINT32_MAX)
            throw std::length_error("GPU evidence matrix exceeds uint32 addressing");
        if(!rows) return {};
        if(evidence.empty()) return std::vector<std::uint8_t>(rows,1);
        if(rows>std::numeric_limits<std::size_t>::max()/evidence.size()||values.size()!=rows*evidence.size())
            throw std::invalid_argument("invalid GPU evidence matrix dimensions");
        std::lock_guard lock(mutex_);
        reserve(matrix_,values.size_bytes()); reserve(query_,evidence.size_bytes()); reserve(output_,rows*sizeof(std::uint32_t));
        checked(matrix_.upload(std::as_bytes(values))); checked(query_.upload(std::as_bytes(evidence)));
        const std::array<ii::gpu::Binding,3> bindings{{{matrix_,0,values.size_bytes()},
            {query_,0,evidence.size_bytes()},{output_,0,rows*sizeof(std::uint32_t)}}};
        const std::array<std::uint32_t,2> shape{static_cast<std::uint32_t>(rows),static_cast<std::uint32_t>(evidence.size())};
        auto commands=device_.commands();
        checked(commands.dispatch(pipeline_,bindings,{static_cast<std::uint32_t>((rows+63)/64),1,1},std::as_bytes(std::span(shape))));
        checked(commands.finish());
        auto submission=checked(device_.submit(std::move(commands))); checked(submission.wait());
        scratch_.resize(rows); checked(output_.read(std::as_writable_bytes(std::span(scratch_))));
        std::vector<std::uint8_t> result(rows);
        for(std::size_t r=0;r<rows;++r) {
            if(scratch_[r]>1) throw std::runtime_error("GPU returned a nonbinary mask");
            result[r]=static_cast<std::uint8_t>(scratch_[r]);
        }
        return result;
    }
};
}
BackendList discover(GpuApi selection) {
    BackendList result;
    const auto api=selection==GpuApi::metal?ii::gpu::Backend::metal:ii::gpu::Backend::vulkan;
    const std::string prefix=selection==GpuApi::metal?"metal":"vulkan";
    auto adapters=ii::gpu::Device::adapters(api);
    if(!adapters) {result.push_back(std::make_shared<Unavailable>(prefix+"/discovery",ii::gpu::backend_name(api),adapters.error().message));return result;}
    for(const auto& adapter:*adapters) {
        try {result.push_back(std::make_shared<Gpu>(api,adapter,prefix));}
        catch(const std::exception& error) {result.push_back(std::make_shared<Unavailable>(prefix+"/"+std::to_string(adapter.index),ii::gpu::backend_name(api),error.what()));}
    }
    if(adapters->empty()) result.push_back(std::make_shared<Unavailable>(prefix+"/discovery",ii::gpu::backend_name(api),"no adapter detected"));
    return result;
}
} // namespace iiDecision::gpu_adapter
