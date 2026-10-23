#include "Backend.h"
#include <algorithm>
#include <map>
#ifdef IIDECISION_HAS_ONNX
#include "OnnxModel.h"
#include <onnxruntime_cxx_api.h>
#include <bit>
#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#if __has_include(<dml_provider_factory.h>) && defined(_WIN32)
#include <dml_provider_factory.h>
#define IIDECISION_ORT_DML
#endif
#if __has_include(<nnapi_provider_factory.h>) && defined(__ANDROID__)
#include <nnapi_provider_factory.h>
#define IIDECISION_ORT_NNAPI
#endif
#if __has_include(<acl_provider_factory.h>)
#include <acl_provider_factory.h>
#define IIDECISION_ORT_ACL
#endif
#if __has_include(<armnn_provider_factory.h>)
#include <armnn_provider_factory.h>
#define IIDECISION_ORT_ARMNN
#endif

namespace iiDecision::detail {
namespace {
using Options = std::unordered_map<std::string,std::string>;
struct Environment {
    Ort::Env env{ORT_LOGGING_LEVEL_ERROR,"iiDecision"};
    std::vector<std::string> plugins;
    Environment() { env.DisableTelemetryEvents(); }
    ~Environment() {
        for (auto it = plugins.rbegin(); it != plugins.rend(); ++it)
            try { env.UnregisterExecutionProviderLibrary(it->c_str()); } catch (...) {}
    }
};
void append(Ort::SessionOptions& session,const std::string& name,const Options& options) {
    std::vector<const char*> keys,values;
    for (const auto& [key,value] : options) { keys.push_back(key.c_str()); values.push_back(value.c_str()); }
    const auto& api = Ort::GetApi();
    if (name == "CPUExecutionProvider") return;
    if (name == "CUDAExecutionProvider") {
        OrtCUDAProviderOptionsV2* raw = nullptr; Ort::ThrowOnError(api.CreateCUDAProviderOptions(&raw));
        std::unique_ptr<OrtCUDAProviderOptionsV2,decltype(api.ReleaseCUDAProviderOptions)> settings(raw,api.ReleaseCUDAProviderOptions);
        Ort::ThrowOnError(api.UpdateCUDAProviderOptions(raw,keys.data(),values.data(),keys.size()));
        session.AppendExecutionProvider_CUDA_V2(*raw);
    } else if (name == "TensorrtExecutionProvider") {
        OrtTensorRTProviderOptionsV2* raw = nullptr; Ort::ThrowOnError(api.CreateTensorRTProviderOptions(&raw));
        std::unique_ptr<OrtTensorRTProviderOptionsV2,decltype(api.ReleaseTensorRTProviderOptions)> settings(raw,api.ReleaseTensorRTProviderOptions);
        Ort::ThrowOnError(api.UpdateTensorRTProviderOptions(raw,keys.data(),values.data(),keys.size()));
        session.AppendExecutionProvider_TensorRT_V2(*raw);
    } else if (name == "MIGraphXExecutionProvider") {
        OrtMIGraphXProviderOptions settings{};
        for (const auto& [key,value] : options) {
            if (key == "device_id") settings.device_id = std::stoi(value);
            else throw std::invalid_argument("legacy MIGraphX accepts device_id; use plugin EP for other options");
        }
        session.AppendExecutionProvider_MIGraphX(settings);
    } else if (name == "ROCMExecutionProvider") {
        OrtROCMProviderOptions settings{};
        for (const auto& [key,value] : options) {
            if (key == "device_id") settings.device_id = std::stoi(value);
            else throw std::invalid_argument("legacy ROCm accepts device_id; use MIGraphX/plugin EP for other options");
        }
        session.AppendExecutionProvider_ROCM(settings);
    } else if (name == "CANNExecutionProvider") {
        OrtCANNProviderOptions* raw = nullptr; Ort::ThrowOnError(api.CreateCANNProviderOptions(&raw));
        std::unique_ptr<OrtCANNProviderOptions,decltype(api.ReleaseCANNProviderOptions)> settings(raw,api.ReleaseCANNProviderOptions);
        Ort::ThrowOnError(api.UpdateCANNProviderOptions(raw,keys.data(),values.data(),keys.size()));
        session.AppendExecutionProvider_CANN(*raw);
    } else if (name == "DnnlExecutionProvider") {
        OrtDnnlProviderOptions* raw = nullptr; Ort::ThrowOnError(api.CreateDnnlProviderOptions(&raw));
        std::unique_ptr<OrtDnnlProviderOptions,decltype(api.ReleaseDnnlProviderOptions)> settings(raw,api.ReleaseDnnlProviderOptions);
        Ort::ThrowOnError(api.UpdateDnnlProviderOptions(raw,keys.data(),values.data(),keys.size()));
        session.AppendExecutionProvider_Dnnl(*raw);
#ifdef IIDECISION_ORT_DML
    } else if (name == "DmlExecutionProvider") {
        int device = 0;
        for (const auto& [key,value] : options) {
            if (key == "device_id") device = std::stoi(value);
            else throw std::invalid_argument("legacy DirectML accepts device_id; use plugin EP for other options");
        }
        session.DisableMemPattern(); session.SetExecutionMode(ORT_SEQUENTIAL);
        Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(session,device));
#endif
#ifdef IIDECISION_ORT_NNAPI
    } else if (name == "NnapiExecutionProvider") {
        uint32_t flags = NNAPI_FLAG_CPU_DISABLED;
        for (const auto& [key,value] : options) {
            if (key == "flags") flags |= static_cast<uint32_t>(std::stoul(value));
            else throw std::invalid_argument("legacy NNAPI accepts flags");
        }
        Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_Nnapi(session,flags));
#endif
#ifdef IIDECISION_ORT_ACL
    } else if (name == "ACLExecutionProvider") {
        if (!options.empty()) throw std::invalid_argument("legacy ACL options are unsupported");
        Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_ACL(session,1));
#endif
#ifdef IIDECISION_ORT_ARMNN
    } else if (name == "ArmNNExecutionProvider") {
        if (!options.empty()) throw std::invalid_argument("legacy ArmNN options are unsupported");
        Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_ArmNN(session,1));
#endif
    } else session.AppendExecutionProvider(name,options);
}
DeviceKind kind(const std::string& name) {
    if (name == "CPUExecutionProvider" || name == "DnnlExecutionProvider" || name == "XnnpackExecutionProvider" ||
        name == "ACLExecutionProvider" || name == "ArmNNExecutionProvider") return DeviceKind::Cpu;
    if (name == "CUDAExecutionProvider" || name == "TensorrtExecutionProvider" || name == "MIGraphXExecutionProvider" ||
        name == "ROCMExecutionProvider" || name == "DmlExecutionProvider") return DeviceKind::Gpu;
    return DeviceKind::Managed; // CoreML/OpenVINO/QNN can internally choose CPU/GPU/NPU.
}
class Onnx final : public IEvidenceBackend {
    std::shared_ptr<Environment> environment_;
    BackendInfo info_;
    std::string name_;
    Options options_;
    const OrtEpDevice* device_ = nullptr;
    // Shape-specific sessions are bounded to the last shape; graph caches remain vendor-controlled.
    std::unique_ptr<Ort::Session> session_;
    std::size_t rows_ = 0, columns_ = 0;
    void createSession(std::size_t rows,std::size_t columns) {
        Ort::SessionOptions settings;
        settings.SetIntraOpNumThreads(1); settings.SetInterOpNumThreads(1);
        settings.SetGraphOptimizationLevel(ORT_DISABLE_ALL);
        if (name_.starts_with("Dml")) { settings.DisableMemPattern(); settings.SetExecutionMode(ORT_SEQUENTIAL); }
        settings.AddConfigEntry("session.record_ep_graph_assignment_info","1");
        if (info_.kind != DeviceKind::Cpu) settings.AddConfigEntry("session.disable_cpu_ep_fallback","1");
        if (device_) settings.AppendExecutionProvider_V2(environment_->env,
            std::vector<Ort::ConstEpDevice>{Ort::ConstEpDevice(device_)},options_);
        else append(settings,name_,options_);
        const auto model = onnx::evidenceModel(rows,columns);
        auto candidate = std::make_unique<Ort::Session>(environment_->env,model.data(),model.size(),settings);
        bool assigned = false;
        for (const auto& graph : candidate->GetEpGraphAssignmentInfo()) {
            if (graph.GetEpName() != name_) throw std::runtime_error("ONNX graph assigned to unexpected provider: "+graph.GetEpName());
            assigned = true;
        }
        if (!assigned) throw std::runtime_error("ONNX provider graph assignment could not be verified");
        session_ = std::move(candidate); rows_ = rows; columns_ = columns;
    }
public:
    Onnx(std::shared_ptr<Environment> env,std::string id,std::string name,Options options,
        const OrtEpDevice* device = nullptr) : environment_(std::move(env)),name_(std::move(name)),
        options_(std::move(options)),device_(device) {
        info_ = {std::move(id),"ONNX Runtime",name_,kind(name_),true,{}};
        if (device_) {
            auto hardware = Ort::ConstEpDevice(device_).Device();
            info_.device = std::string(hardware.Vendor())+" device "+std::to_string(hardware.DeviceId())+" ("+name_+")";
            info_.kind = hardware.Type() == OrtHardwareDeviceType_CPU ? DeviceKind::Cpu :
                hardware.Type() == OrtHardwareDeviceType_GPU ? DeviceKind::Gpu : DeviceKind::Npu;
        }
        if (name_ == "CoreMLExecutionProvider") {
            options_.try_emplace("ModelFormat","MLProgram");
            options_.try_emplace("MLComputeUnits","ALL");
            options_.try_emplace("RequireStaticInputShapes","1");
        }
        if (name_ == "QNNExecutionProvider") options_.try_emplace("offload_graph_io_quantization","0");
        // This graph has no layout-sensitive operators. Avoid a temporary NHWC
        // assignment pass that prevents ORT 1.30 from recording final EP assignments.
        if (name_ == "WebGpuExecutionProvider") options_.try_emplace("preferredLayout","NCHW");
    }
    BackendInfo info() const override { return info_; }
    std::vector<std::uint8_t> match(std::size_t rows,std::span<const std::uint32_t> values,
        std::span<const std::uint32_t> evidence) override {
        // Encode IDs as independent 0/1 bits. Casting IDs to FP16/FP32 aliases large
        // categories; binary subtraction, multiplication and max are exactly representable.
        std::uint32_t maximum = 0;
        for (const auto v : values) maximum |= v;
        for (const auto v : evidence) maximum |= v;
        const auto bits = static_cast<unsigned>(std::max(1,static_cast<int>(std::bit_width(maximum))));
        if (evidence.size() > std::numeric_limits<std::size_t>::max()/bits ||
            values.size() > std::numeric_limits<std::size_t>::max()/bits)
            throw std::length_error("ONNX bit encoding exceeds address space");
        const auto columns = evidence.size()*bits;
        if (rows > INT64_MAX || columns > INT64_MAX) throw std::length_error("ONNX shape exceeds int64");
        std::vector<float> matrix(values.size()*bits), query(evidence.size()*bits);
        for (std::size_t i = 0; i < values.size(); ++i) for (unsigned b = 0; b < bits; ++b)
            matrix[i*bits+b] = static_cast<float>((values[i] >> b)&1);
        for (std::size_t i = 0; i < evidence.size(); ++i) for (unsigned b = 0; b < bits; ++b)
            query[i*bits+b] = static_cast<float>((evidence[i] >> b)&1);
        if (!session_ || rows_ != rows || columns_ != columns) createSession(rows,columns);
        const std::int64_t shape[]{static_cast<std::int64_t>(rows),static_cast<std::int64_t>(columns)};
        const std::int64_t query_shape[]{1,shape[1]};
        auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
        Ort::Value inputs[]{Ort::Value::CreateTensor<float>(memory,matrix.data(),matrix.size(),shape,2),
            Ort::Value::CreateTensor<float>(memory,query.data(),query.size(),query_shape,2)};
        const char* input_names[]{"values","query"}; const char* output_names[]{"mask"};
        auto outputs = session_->Run(Ort::RunOptions{nullptr},input_names,inputs,2,output_names,1);
        const auto type = outputs[0].GetTensorTypeAndShapeInfo();
        if (type.GetElementCount() != rows || type.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
            throw std::runtime_error("ONNX provider returned an invalid output shape/type");
        const auto* mask = outputs[0].GetTensorData<float>();
        std::vector<std::uint8_t> result(rows);
        for (std::size_t r = 0; r < rows; ++r) {
            if (mask[r] != 0 && mask[r] != 1) throw std::runtime_error("ONNX provider returned a nonbinary mask");
            result[r] = static_cast<std::uint8_t>(mask[r]);
        }
        return result;
    }
};
}
void addOnnx(BackendList& result,const AccelerationOptions& options) {
    auto env = std::make_shared<Environment>();
    for (const auto& plugin : options.onnx_plugins) {
        try {
            const std::u8string utf8(plugin.library_path.begin(),plugin.library_path.end());
            const auto path = std::filesystem::path(utf8).native();
            env->env.RegisterExecutionProviderLibrary(plugin.name.c_str(),path);
            env->plugins.push_back(plugin.name);
        } catch (const std::exception& error) {
            result.push_back(std::make_shared<Unavailable>("onnx/plugin/"+plugin.name,"ONNX Runtime",error.what()));
        }
    }
    try {
        auto configurations = options.onnx_providers;
        const auto devices = env->env.GetEpDevices();
        auto names = Ort::GetAvailableProviders();
        for (const auto& device : devices) if (std::find(names.begin(),names.end(),device.EpName()) == names.end()) names.emplace_back(device.EpName());
        if (configurations.empty()) for (const auto& name : names) {
            // Remote/cloud providers do not satisfy the native hardware contract.
            if (name != "AzureExecutionProvider" && name != "JsExecutionProvider") configurations.push_back({name,{}});
        }
        std::map<std::string,std::size_t> counts;
        for (const auto& configuration : configurations) {
            const auto base = "onnx/"+configuration.name+"/"+std::to_string(counts[configuration.name]++);
            Options settings;
            for (const auto& [key,value] : configuration.options)
                if (!settings.emplace(key,value).second) throw std::invalid_argument("duplicate ONNX provider option: "+key);
            bool modern = false;
            std::size_t index = 0;
            for (const auto& device : devices) if (configuration.name == device.EpName()) {
                result.push_back(std::make_shared<Onnx>(env,base+"/device/"+std::to_string(index++),
                    configuration.name,settings,device)); modern = true;
            }
            if (modern) continue;
            if (std::find(names.begin(),names.end(),configuration.name) == names.end())
                result.push_back(std::make_shared<Unavailable>(base,"ONNX Runtime","provider absent from this ONNX Runtime build"));
            else result.push_back(std::make_shared<Onnx>(env,base,configuration.name,std::move(settings)));
        }
    } catch (const std::exception& error) { result.push_back(std::make_shared<Unavailable>("onnx","ONNX Runtime",error.what())); }
}
}
#else
namespace iiDecision::detail { void addOnnx(BackendList& result,const AccelerationOptions& options) {
    result.push_back(std::make_shared<Unavailable>("onnx","ONNX Runtime","built without ONNX Runtime"));
    std::map<std::string,std::size_t> counts;
    for (const auto& provider : options.onnx_providers)
        result.push_back(std::make_shared<Unavailable>("onnx/"+provider.name+"/"+std::to_string(counts[provider.name]++),
            "ONNX Runtime","built without ONNX Runtime"));
    for (const auto& plugin : options.onnx_plugins)
        result.push_back(std::make_shared<Unavailable>("onnx/plugin/"+plugin.name,"ONNX Runtime","built without ONNX Runtime"));
} }
#endif
