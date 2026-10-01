#include "Backend.h"
#ifdef IIDECISION_HAS_MLX
#include <mlx/mlx.h>
#include <limits>
#include <climits>
#include <mutex>
namespace iiDecision::detail {
namespace {
// MLX streams and lazy graph construction are serialized across matcher instances.
std::mutex mlx_mutex;
class Mlx final : public IEvidenceBackend {
    mlx::core::Device device_;
    BackendInfo info_;
public:
    explicit Mlx(int index) : device_(mlx::core::Device::gpu,index) {
        const auto& properties = mlx::core::device_info(device_);
        auto found = properties.find("device_name");
        const auto name = found != properties.end() ? std::get<std::string>(found->second) : "MLX GPU";
        info_ = {"mlx/"+std::to_string(index),"MLX",name,DeviceKind::Gpu,true,{}};
    }
    BackendInfo info() const override { return info_; }
    std::vector<std::uint8_t> match(std::size_t rows,std::span<const std::uint32_t> values,
        std::span<const std::uint32_t> evidence) override {
        if (rows > INT_MAX || evidence.size() > INT_MAX) throw std::length_error("MLX shape exceeds int32");
        std::lock_guard lock(mlx_mutex);
        namespace mx = mlx::core;
        auto stream = mx::default_stream(device_);
        mx::array matrix(values.begin(),{static_cast<int>(rows),static_cast<int>(evidence.size())},mx::uint32);
        mx::array query(evidence.begin(),{1,static_cast<int>(evidence.size())},mx::uint32);
        auto mask = mx::all(mx::equal(matrix,query,stream),1,false,stream);
        mask.eval(); // Completes native work before accessing the host result.
        std::vector<std::uint8_t> result(rows);
        const auto* data = mask.data<bool>();
        for (std::size_t r = 0; r < rows; ++r) result[r] = data[r];
        return result;
    }
};
}
void addMlx(BackendList& result) {
    try {
        const auto count = mlx::core::device_count(mlx::core::Device::gpu);
        for (int i = 0; i < count; ++i) result.push_back(std::make_shared<Mlx>(i));
        if (!count) result.push_back(std::make_shared<Unavailable>("mlx","MLX","no MLX GPU detected"));
    } catch (const std::exception& error) { result.push_back(std::make_shared<Unavailable>("mlx","MLX",error.what())); }
}
}
#else
namespace iiDecision::detail { void addMlx(BackendList& result) {
    result.push_back(std::make_shared<Unavailable>("mlx","MLX","built without MLX"));
} }
#endif
