#pragma once
#include <iiDecision/Acceleration.h>
#include <stdexcept>

namespace iiDecision::detail {
using BackendList = std::vector<std::shared_ptr<IEvidenceBackend>>;
class Unavailable final : public IEvidenceBackend {
    BackendInfo info_;
public:
    Unavailable(std::string id, std::string api, std::string reason)
        : info_{std::move(id),std::move(api),{},DeviceKind::Managed,false,std::move(reason)} {}
    BackendInfo info() const override { return info_; }
    std::vector<std::uint8_t> match(std::size_t, std::span<const std::uint32_t>,
        std::span<const std::uint32_t>) override { throw std::runtime_error(info_.diagnostic); }
};
void addMetal(BackendList&);
void addMlx(BackendList&);
void addOpenCl(BackendList&);
void addVulkan(BackendList&);
}
