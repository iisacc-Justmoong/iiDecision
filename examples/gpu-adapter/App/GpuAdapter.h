#pragma once
#include <iiDecision/Acceleration.h>

namespace iiDecision::gpu_adapter {
enum class GpuApi { metal, vulkan };
// This is application composition code: the caller owns and injects the results.
std::vector<std::shared_ptr<IEvidenceBackend>> discover(GpuApi);
}
