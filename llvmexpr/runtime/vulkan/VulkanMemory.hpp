#ifndef LLVMEXPR_RUNTIME_VULKAN_VULKANMEMORY_HPP
#define LLVMEXPR_RUNTIME_VULKAN_VULKANMEMORY_HPP

#define VK_NO_PROTOTYPES

// NOLINTBEGIN(cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum)

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

// NOLINTEND(cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum)

#include <vk_mem_alloc.h>
#include <vulkan/vulkan_raii.hpp>

#include <cstddef>

namespace llvmexpr {

class VulkanContext;

struct VulkanBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocInfo = {};
    VkDeviceSize size = 0;

    VulkanBuffer() = default;
    VulkanBuffer(VkBuffer buf, VmaAllocation alloc, VmaAllocationInfo info,
                 VkDeviceSize sz)
        : buffer(buf), allocation(alloc), allocInfo(info), size(sz) {}

    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    ~VulkanBuffer() = default;

    VulkanBuffer(VulkanBuffer&& other) noexcept
        : buffer(other.buffer), allocation(other.allocation),
          allocInfo(other.allocInfo), size(other.size) {
        other.buffer = VK_NULL_HANDLE;
        other.allocation = VK_NULL_HANDLE;
        other.size = 0;
    }

    VulkanBuffer& operator=(VulkanBuffer&& other) noexcept {
        if (this != &other) {
            buffer = other.buffer;
            allocation = other.allocation;
            allocInfo = other.allocInfo;
            size = other.size;
            other.buffer = VK_NULL_HANDLE;
            other.allocation = VK_NULL_HANDLE;
            other.size = 0;
        }
        return *this;
    }

    [[nodiscard]] bool isValid() const { return buffer != VK_NULL_HANDLE; }
    [[nodiscard]] void* getMappedData() const { return allocInfo.pMappedData; }
};

class VulkanMemory {
  public:
    explicit VulkanMemory(VulkanContext& ctx);
    ~VulkanMemory();

    VulkanMemory(const VulkanMemory&) = delete;
    VulkanMemory& operator=(const VulkanMemory&) = delete;
    VulkanMemory(VulkanMemory&&) = delete;
    VulkanMemory& operator=(VulkanMemory&&) = delete;

    VulkanBuffer createGPUBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    VulkanBuffer createStagingBuffer(VkDeviceSize size, bool forUpload = true);

    void destroyBuffer(VulkanBuffer& buffer);

    void uploadToBuffer(VulkanBuffer& gpuBuffer, const void* data,
                        VkDeviceSize size, VkDeviceSize offset = 0);

    // blocks until transfer completes
    void downloadFromBuffer(VulkanBuffer& gpuBuffer, void* data,
                            VkDeviceSize size, VkDeviceSize offset = 0);

    // blocks until transfer completes
    [[nodiscard]] VmaAllocator getAllocator() const { return allocator; }

  private:
    VulkanContext& context;
    VmaAllocator allocator = VK_NULL_HANDLE;
    vk::raii::CommandPool transferPool = nullptr;
    vk::raii::CommandBuffer transferCmd = nullptr;
    vk::raii::Fence transferFence = nullptr;
};

} // namespace llvmexpr

#endif // LLVMEXPR_RUNTIME_VULKAN_VULKANMEMORY_HPP
