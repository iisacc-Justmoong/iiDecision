#include "Backend.h"
namespace iiDecision::detail {
void addVulkan(BackendList& result) {
    result.push_back(std::make_shared<Unavailable>("vulkan","Vulkan","supply a Vulkan backend from the application"));
}
}
