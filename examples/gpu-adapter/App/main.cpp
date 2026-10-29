#include "GpuAdapter.h"
#include <iostream>

int main() {
    auto backends = iiDecision::gpu_adapter::discover(iiDecision::gpu_adapter::GpuApi::metal);
    auto vulkan = iiDecision::gpu_adapter::discover(iiDecision::gpu_adapter::GpuApi::vulkan);
    backends.insert(backends.end(),vulkan.begin(),vulkan.end());
    iiDecision::EvidenceMatcher matcher({},std::move(backends));
    for (const auto& info : matcher.backends())
        std::cout << info.id << ": " << (info.available ? info.device : info.diagnostic) << '\n';
}
