#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace iiDecision {

enum class ExecutionMode { CpuOnly, PreferAccelerators, RequireAccelerators };
enum class DeviceKind { Cpu, Gpu, Npu, Managed };

struct BackendInfo {
    std::string id;
    std::string api;
    std::string device;
    DeviceKind kind = DeviceKind::Managed;
    bool available = false; // Discovered device; graph support is checked on execution.
    std::string diagnostic;
};

struct ExecutionRecord {
    std::string backend_id;
    std::size_t first_row = 0;
    std::size_t row_count = 0;
    bool completed = false;
    std::string diagnostic;
};

struct ExecutionReport {
    std::vector<ExecutionRecord> records;
    std::size_t cpu_rows = 0;
};

struct AccelerationOptions {
    ExecutionMode mode = ExecutionMode::PreferAccelerators;
    std::vector<std::string> backend_ids; // Empty selects every available accelerator.
};

// Exact categorical matching, independent of domain and probability arithmetic.
// Implementations must return one 0/1 value per row and must finish device work
// before returning. Input values are row-major uint32 IDs; columns == evidence.size().
class IEvidenceBackend {
public:
    virtual ~IEvidenceBackend() = default;
    virtual BackendInfo info() const = 0;
    virtual std::vector<std::uint8_t> match(std::size_t rows,
        std::span<const std::uint32_t> values,
        std::span<const std::uint32_t> evidence) = 0;
};

struct EvidenceMatch {
    std::vector<std::uint8_t> mask;
    ExecutionReport execution;
};

class EvidenceMatcher {
public:
    explicit EvidenceMatcher(AccelerationOptions options = {},
        std::vector<std::shared_ptr<IEvidenceBackend>> additional_backends = {});
    std::vector<BackendInfo> backends() const;
    EvidenceMatch match(std::size_t rows, std::span<const std::uint32_t> values,
        std::span<const std::uint32_t> evidence) const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace iiDecision
