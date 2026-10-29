#include <iiDecision/Acceleration.h>
#include "backends/Backend.h"

#include <algorithm>
#include <future>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>

namespace iiDecision {
namespace {
std::vector<std::uint8_t> cpuMatch(std::size_t rows, std::span<const std::uint32_t> values,
    std::span<const std::uint32_t> query) {
    std::vector<std::uint8_t> mask(rows,1);
    for (std::size_t r = 0; r < rows; ++r)
        for (std::size_t c = 0; c < query.size(); ++c)
            if (values[r * query.size() + c] != query[c]) { mask[r] = 0; break; }
    return mask;
}
class Cpu final : public IEvidenceBackend {
public:
    BackendInfo info() const override { return {"cpu", "C++23", "host CPU", DeviceKind::Cpu,true,{}}; }
    std::vector<std::uint8_t> match(std::size_t rows,std::span<const std::uint32_t> values,
        std::span<const std::uint32_t> query) override { return cpuMatch(rows,values,query); }
};
}
struct EvidenceMatcher::Impl {
    struct Slot { std::shared_ptr<IEvidenceBackend> backend; std::mutex mutex; };
    AccelerationOptions options;
    std::vector<std::shared_ptr<Slot>> slots;
};
EvidenceMatcher::EvidenceMatcher(AccelerationOptions options,
    std::vector<std::shared_ptr<IEvidenceBackend>> additional) : impl_(std::make_shared<Impl>()) {
    impl_->options = std::move(options);
    detail::BackendList backends{std::make_shared<Cpu>()};
    if (impl_->options.mode != ExecutionMode::CpuOnly) {
        auto probe = [&](const char* id, const char* api, auto discover) {
            try { discover(); }
            catch (const std::exception& error) {
                backends.push_back(std::make_shared<detail::Unavailable>(std::string(id)+"/discovery",api,error.what()));
            }
        };
        probe("metal","Metal",[&]{detail::addMetal(backends);});
        probe("mlx","MLX",[&]{detail::addMlx(backends);});
        probe("opencl","OpenCL",[&]{detail::addOpenCl(backends);});
        probe("vulkan","Vulkan",[&]{detail::addVulkan(backends);});
    }
    backends.insert(backends.end(),additional.begin(),additional.end());
    std::set<std::string> ids;
    for (auto& backend : backends) {
        if (!backend || backend->info().id.empty() || !ids.insert(backend->info().id).second)
            throw std::invalid_argument("backend IDs must be nonempty and unique");
        auto slot = std::make_shared<Impl::Slot>(); slot->backend = std::move(backend);
        impl_->slots.push_back(std::move(slot));
    }
    std::set<std::string> selected;
    for (const auto& id : impl_->options.backend_ids) {
        if (!ids.contains(id) || !selected.insert(id).second)
            throw std::invalid_argument("selected backend IDs must exist and occur once: " + id);
    }
}
std::vector<BackendInfo> EvidenceMatcher::backends() const {
    std::vector<BackendInfo> result;
    for (const auto& slot : impl_->slots) result.push_back(slot->backend->info());
    return result;
}
EvidenceMatch EvidenceMatcher::match(std::size_t rows, std::span<const std::uint32_t> values,
    std::span<const std::uint32_t> evidence) const {
    if ((!evidence.empty() && rows > std::numeric_limits<std::size_t>::max() / evidence.size()) ||
        values.size() != rows * evidence.size()) throw std::invalid_argument("invalid evidence matrix dimensions");
    EvidenceMatch result;
    result.mask.resize(rows);
    if (!rows) return result;
    auto fallback = [&](std::size_t first,std::size_t count,std::span<const std::uint32_t> batch) {
        const auto mask = cpuMatch(count,batch,evidence);
        std::copy(mask.begin(),mask.end(),result.mask.begin()+first);
        result.execution.cpu_rows += count;
        result.execution.records.push_back({"cpu",first,count,true,{}});
    };
    // A prior has no predicate to dispatch. Strict mode also accepts this identity.
    if (evidence.empty() || impl_->options.mode == ExecutionMode::CpuOnly) {
        fallback(0,rows,values); return result;
    }
    std::vector<std::shared_ptr<Impl::Slot>> selected;
    if (impl_->options.backend_ids.empty()) {
        for (const auto& slot : impl_->slots) {
            const auto info = slot->backend->info();
            if (info.available && info.kind != DeviceKind::Cpu) selected.push_back(slot);
        }
    } else for (const auto& id : impl_->options.backend_ids) {
        for (const auto& slot : impl_->slots) if (slot->backend->info().id == id) selected.push_back(slot);
    }
    if (selected.empty()) {
        if (impl_->options.mode == ExecutionMode::RequireAccelerators)
            throw std::runtime_error("no accelerator is available");
        fallback(0,rows,values); return result;
    }
    const auto count = std::min(rows,selected.size());
    struct Shard { ExecutionRecord record; std::vector<std::uint8_t> mask; bool cpu = false; };
    std::vector<std::future<Shard>> jobs;
    for (std::size_t i = 0; i < count; ++i) {
        const auto first = (rows / count) * i + std::min(i,rows % count);
        const auto size = rows / count + (i < rows % count);
        const auto batch = values.subspan(first * evidence.size(),size * evidence.size());
        jobs.push_back(std::async(std::launch::async,[slot=selected[i],first,size,batch,evidence,this] {
            const auto info = slot->backend->info();
            Shard shard{{info.id,first,size,false,{}},{},info.kind == DeviceKind::Cpu};
            try {
                if (!info.available) throw std::runtime_error(info.diagnostic.empty() ? "backend is unavailable" : info.diagnostic);
                if (shard.cpu && impl_->options.mode == ExecutionMode::RequireAccelerators)
                    throw std::runtime_error("strict acceleration cannot select a CPU backend");
                std::lock_guard lock(slot->mutex);
                shard.mask = slot->backend->match(size,batch,evidence);
                if (shard.mask.size() != size || std::any_of(shard.mask.begin(),shard.mask.end(),[](auto v){return v > 1;}))
                    throw std::runtime_error("backend returned an invalid categorical mask");
                shard.record.completed = true;
            } catch (const std::exception& error) { shard.record.diagnostic = error.what(); }
            return shard;
        }));
    }
    std::string failure;
    for (auto& job : jobs) {
        auto shard = job.get();
        const auto& record = shard.record;
        result.execution.records.push_back(record);
        if (record.completed) {
            std::copy(shard.mask.begin(),shard.mask.end(),result.mask.begin()+record.first_row);
            if (shard.cpu) result.execution.cpu_rows += record.row_count;
        } else if (impl_->options.mode == ExecutionMode::RequireAccelerators) {
            failure += record.backend_id + ": " + record.diagnostic + "\n";
        } else fallback(record.first_row,record.row_count,
            values.subspan(record.first_row * evidence.size(),record.row_count * evidence.size()));
    }
    if (!failure.empty()) throw std::runtime_error(failure);
    return result;
}
} // namespace iiDecision
