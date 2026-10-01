#include "Backend.h"
#ifdef IIDECISION_HAS_VULKAN
#include "EvidenceShader.h"
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstring>
#include <limits>

namespace iiDecision::detail {
namespace {
void checked(VkResult status) { if (status != VK_SUCCESS) throw std::runtime_error("Vulkan error "+std::to_string(status)); }
struct Instance {
    Instance() = default;
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
    VkInstance handle = VK_NULL_HANDLE;
    ~Instance() { if (handle) vkDestroyInstance(handle,nullptr); }
};
struct Device {
    Device() = default;
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    VkDevice handle = VK_NULL_HANDLE;
    ~Device() { if (handle) vkDestroyDevice(handle,nullptr); }
};
template<class T, auto Destroy> struct Resource {
    VkDevice device;
    T handle = VK_NULL_HANDLE;
    explicit Resource(VkDevice parent) : device(parent) {}
    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;
    ~Resource() { if (handle) Destroy(device,handle,nullptr); }
};
struct Buffer {
    VkDevice device;
    VkBuffer handle = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    explicit Buffer(VkDevice parent) : device(parent) {}
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    ~Buffer() {
        if (mapped) vkUnmapMemory(device,memory);
        if (handle) vkDestroyBuffer(device,handle,nullptr);
        if (memory) vkFreeMemory(device,memory,nullptr);
    }
};
class Vulkan final : public IEvidenceBackend {
    std::shared_ptr<Instance> instance_;
    Device device_;
    VkPhysicalDevice physical_;
    VkPhysicalDeviceProperties properties_{};
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t family_ = 0;
    BackendInfo info_;
    void allocate(Buffer& buffer,VkDeviceSize size) {
        if (size > properties_.limits.maxStorageBufferRange) throw std::length_error("Vulkan storage buffer limit exceeded");
        VkBufferCreateInfo settings{}; settings.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        settings.size = size; settings.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        settings.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        checked(vkCreateBuffer(device_.handle,&settings,nullptr,&buffer.handle));
        VkMemoryRequirements requirements; vkGetBufferMemoryRequirements(device_.handle,buffer.handle,&requirements);
        VkPhysicalDeviceMemoryProperties memory; vkGetPhysicalDeviceMemoryProperties(physical_,&memory);
        std::uint32_t index = memory.memoryTypeCount;
        for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
            constexpr auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            if ((requirements.memoryTypeBits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) { index = i; break; }
        }
        if (index == memory.memoryTypeCount) throw std::runtime_error("Vulkan needs coherent host-visible memory");
        VkMemoryAllocateInfo allocation{}; allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = index;
        checked(vkAllocateMemory(device_.handle,&allocation,nullptr,&buffer.memory));
        checked(vkBindBufferMemory(device_.handle,buffer.handle,buffer.memory,0));
        checked(vkMapMemory(device_.handle,buffer.memory,0,size,0,&buffer.mapped));
    }
public:
    Vulkan(std::shared_ptr<Instance> instance,VkPhysicalDevice physical,std::size_t index)
        : instance_(std::move(instance)), physical_(physical) {
        vkGetPhysicalDeviceProperties(physical_,&properties_);
        info_ = {"vulkan/"+std::to_string(index),"Vulkan",properties_.deviceName,
            properties_.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ? DeviceKind::Cpu : DeviceKind::Gpu,true,{}};
        std::uint32_t count = 0; vkGetPhysicalDeviceQueueFamilyProperties(physical_,&count,nullptr);
        std::vector<VkQueueFamilyProperties> families(count); vkGetPhysicalDeviceQueueFamilyProperties(physical_,&count,families.data());
        family_ = count;
        for (std::uint32_t i = 0; i < count; ++i) if (families[i].queueCount && (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) { family_ = i; break; }
        if (family_ == count) throw std::runtime_error("Vulkan device has no compute queue");
        if (properties_.limits.maxComputeWorkGroupInvocations < 64 || properties_.limits.maxComputeWorkGroupSize[0] < 64)
            throw std::runtime_error("Vulkan device cannot run the 64-thread kernel");
        float priority = 1;
        VkDeviceQueueCreateInfo queue{}; queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue.queueFamilyIndex = family_; queue.queueCount = 1; queue.pQueuePriorities = &priority;
        checked(vkEnumerateDeviceExtensionProperties(physical_,nullptr,&count,nullptr));
        std::vector<VkExtensionProperties> available(count); checked(vkEnumerateDeviceExtensionProperties(physical_,nullptr,&count,available.data()));
        std::vector<const char*> extensions;
        for (const auto& extension : available)
            if (std::strcmp(extension.extensionName,"VK_KHR_portability_subset") == 0) extensions.push_back("VK_KHR_portability_subset");
        VkDeviceCreateInfo settings{}; settings.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        settings.queueCreateInfoCount = 1; settings.pQueueCreateInfos = &queue;
        settings.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()); settings.ppEnabledExtensionNames = extensions.data();
        checked(vkCreateDevice(physical_,&settings,nullptr,&device_.handle));
        vkGetDeviceQueue(device_.handle,family_,0,&queue_);
    }
    BackendInfo info() const override { return info_; }
    std::vector<std::uint8_t> match(std::size_t rows,std::span<const std::uint32_t> values,
        std::span<const std::uint32_t> evidence) override {
        if (rows > UINT32_MAX || evidence.size() > UINT32_MAX || values.size() > UINT32_MAX ||
            (rows+63)/64 > properties_.limits.maxComputeWorkGroupCount[0]) throw std::length_error("Vulkan dispatch limit exceeded");
        const auto device = device_.handle;
        Buffer matrix(device),query(device),output(device);
        allocate(matrix,values.size_bytes()); allocate(query,evidence.size_bytes()); allocate(output,rows*sizeof(std::uint32_t));
        std::memcpy(matrix.mapped,values.data(),values.size_bytes());
        std::memcpy(query.mapped,evidence.data(),evidence.size_bytes());
        Resource<VkShaderModule,vkDestroyShaderModule> shader(device);
        VkShaderModuleCreateInfo shader_info{}; shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shader_info.codeSize = sizeof(evidence_shader); shader_info.pCode = evidence_shader;
        checked(vkCreateShaderModule(device,&shader_info,nullptr,&shader.handle));
        Resource<VkDescriptorSetLayout,vkDestroyDescriptorSetLayout> descriptor_layout(device);
        VkDescriptorSetLayoutBinding bindings[3]{};
        for (std::uint32_t i = 0; i < 3; ++i) bindings[i] = {i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo layout_info{}; layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 3; layout_info.pBindings = bindings;
        checked(vkCreateDescriptorSetLayout(device,&layout_info,nullptr,&descriptor_layout.handle));
        Resource<VkPipelineLayout,vkDestroyPipelineLayout> pipeline_layout(device);
        VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,8};
        VkPipelineLayoutCreateInfo pipeline_layout_info{}; pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_layout_info.setLayoutCount = 1; pipeline_layout_info.pSetLayouts = &descriptor_layout.handle;
        pipeline_layout_info.pushConstantRangeCount = 1; pipeline_layout_info.pPushConstantRanges = &push;
        checked(vkCreatePipelineLayout(device,&pipeline_layout_info,nullptr,&pipeline_layout.handle));
        Resource<VkPipeline,vkDestroyPipeline> pipeline(device);
        VkComputePipelineCreateInfo pipeline_info{}; pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeline_info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,shader.handle,"main",nullptr};
        pipeline_info.layout = pipeline_layout.handle;
        checked(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pipeline_info,nullptr,&pipeline.handle));
        Resource<VkDescriptorPool,vkDestroyDescriptorPool> descriptors(device);
        VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,3};
        VkDescriptorPoolCreateInfo pool_info{}; pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.maxSets = 1; pool_info.poolSizeCount = 1; pool_info.pPoolSizes = &pool_size;
        checked(vkCreateDescriptorPool(device,&pool_info,nullptr,&descriptors.handle));
        VkDescriptorSet set;
        VkDescriptorSetAllocateInfo set_info{}; set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        set_info.descriptorPool = descriptors.handle; set_info.descriptorSetCount = 1; set_info.pSetLayouts = &descriptor_layout.handle;
        checked(vkAllocateDescriptorSets(device,&set_info,&set));
        const VkDescriptorBufferInfo buffers[]{{matrix.handle,0,values.size_bytes()},
            {query.handle,0,evidence.size_bytes()},{output.handle,0,rows*sizeof(std::uint32_t)}};
        VkWriteDescriptorSet writes[3]{};
        for (std::uint32_t i = 0; i < 3; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set; writes[i].dstBinding = i; writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[i].pBufferInfo = &buffers[i];
        }
        vkUpdateDescriptorSets(device,3,writes,0,nullptr);
        Resource<VkCommandPool,vkDestroyCommandPool> commands(device);
        VkCommandPoolCreateInfo command_pool_info{}; command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO; command_pool_info.queueFamilyIndex = family_;
        checked(vkCreateCommandPool(device,&command_pool_info,nullptr,&commands.handle));
        VkCommandBuffer command;
        VkCommandBufferAllocateInfo command_info{}; command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command_info.commandPool = commands.handle; command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; command_info.commandBufferCount = 1;
        checked(vkAllocateCommandBuffers(device,&command_info,&command));
        VkCommandBufferBeginInfo begin{}; begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        checked(vkBeginCommandBuffer(command,&begin));
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline.handle);
        vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_layout.handle,0,1,&set,0,nullptr);
        std::uint32_t shape[]{static_cast<std::uint32_t>(rows),static_cast<std::uint32_t>(evidence.size())};
        vkCmdPushConstants(command,pipeline_layout.handle,VK_SHADER_STAGE_COMPUTE_BIT,0,8,shape);
        vkCmdDispatch(command,static_cast<std::uint32_t>((rows+63)/64),1,1);
        VkMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        checked(vkEndCommandBuffer(command));
        Resource<VkFence,vkDestroyFence> fence(device);
        VkFenceCreateInfo fence_info{}; fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; checked(vkCreateFence(device,&fence_info,nullptr,&fence.handle));
        VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
        checked(vkQueueSubmit(queue_,1,&submit,fence.handle));
        checked(vkWaitForFences(device,1,&fence.handle,VK_TRUE,UINT64_MAX));
        const auto* mask = static_cast<const std::uint32_t*>(output.mapped);
        std::vector<std::uint8_t> result(rows);
        for (std::size_t r = 0; r < rows; ++r) {
            if (mask[r] > 1) throw std::runtime_error("Vulkan returned a nonbinary mask");
            result[r] = static_cast<std::uint8_t>(mask[r]);
        }
        return result;
    }
};
}
void addVulkan(BackendList& result) {
    try {
        auto instance = std::make_shared<Instance>();
        std::uint32_t count = 0; checked(vkEnumerateInstanceExtensionProperties(nullptr,&count,nullptr));
        std::vector<VkExtensionProperties> available(count); checked(vkEnumerateInstanceExtensionProperties(nullptr,&count,available.data()));
        std::vector<const char*> extensions;
        for (const auto& extension : available)
            if (std::strcmp(extension.extensionName,"VK_KHR_portability_enumeration") == 0) extensions.push_back("VK_KHR_portability_enumeration");
        VkApplicationInfo application{}; application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.pApplicationName = "iiDecision"; application.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo settings{}; settings.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; settings.pApplicationInfo = &application;
        if (!extensions.empty()) settings.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        settings.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()); settings.ppEnabledExtensionNames = extensions.data();
        checked(vkCreateInstance(&settings,nullptr,&instance->handle));
        checked(vkEnumeratePhysicalDevices(instance->handle,&count,nullptr));
        std::vector<VkPhysicalDevice> devices(count); checked(vkEnumeratePhysicalDevices(instance->handle,&count,devices.data()));
        for (std::size_t i = 0; i < devices.size(); ++i) {
            try { result.push_back(std::make_shared<Vulkan>(instance,devices[i],i)); }
            catch (const std::exception& error) { result.push_back(std::make_shared<Unavailable>("vulkan/"+std::to_string(i),"Vulkan",error.what())); }
        }
        if (!count) result.push_back(std::make_shared<Unavailable>("vulkan","Vulkan","no Vulkan physical device detected"));
    } catch (const std::exception& error) { result.push_back(std::make_shared<Unavailable>("vulkan","Vulkan",error.what())); }
}
}
#else
namespace iiDecision::detail { void addVulkan(BackendList& result) {
    result.push_back(std::make_shared<Unavailable>("vulkan","Vulkan","built without Vulkan"));
} }
#endif
