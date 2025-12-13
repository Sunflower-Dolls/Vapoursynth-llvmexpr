#ifndef LLVMEXPR_RUNTIME_VULKAN_VULKANCONTEXT_HPP
#define LLVMEXPR_RUNTIME_VULKAN_VULKANCONTEXT_HPP

#define VK_NO_PROTOTYPES
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <mutex>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_raii.hpp>

namespace llvmexpr {

class VulkanContext {
  public:
    static VulkanContext& getInstance();

    VulkanContext();
    ~VulkanContext();

    // Disable copy/move
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;

    vk::raii::Context& getContext() { return context; }
    vk::raii::Instance& getInstanceRef() { return instance; }
    vk::raii::PhysicalDevice& getPhysicalDevice() { return physicalDevice; }
    vk::raii::Device& getDevice() { return device; }
    vk::raii::Queue& getComputeQueue() { return computeQueue; }
    [[nodiscard]] uint32_t getQueueFamilyIndex() const {
        return queueFamilyIndex;
    }

    void submit(const vk::SubmitInfo& submitInfo, const vk::Fence& fence);

  private:
    void createInstance();
    void pickPhysicalDevice();
    void createDevice();

    vk::raii::Context context;
    vk::raii::Instance instance;
    vk::raii::PhysicalDevice physicalDevice = nullptr;
    vk::raii::Device device = nullptr;
    vk::raii::Queue computeQueue = nullptr;

    uint32_t queueFamilyIndex = -1;
    std::mutex queueMutex;
};

} // namespace llvmexpr

#endif // LLVMEXPR_RUNTIME_VULKAN_VULKANCONTEXT_HPP
