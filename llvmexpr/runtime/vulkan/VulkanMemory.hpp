/**
 * Copyright (C) 2025 yuygfgg
 * 
 * This file is part of Vapoursynth-llvmexpr.
 * 
 * Vapoursynth-llvmexpr is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Vapoursynth-llvmexpr is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Vapoursynth-llvmexpr.  If not, see <https://www.gnu.org/licenses/>.
 */

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
    VmaAllocationInfo alloc_info = {};
    VkDeviceSize size = 0;

    VulkanBuffer() = default;
    VulkanBuffer(VkBuffer buf, VmaAllocation alloc, VmaAllocationInfo info,
                 VkDeviceSize sz)
        : buffer(buf), allocation(alloc), alloc_info(info), size(sz) {}

    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    ~VulkanBuffer() = default;

    VulkanBuffer(VulkanBuffer&& other) noexcept
        : buffer(other.buffer), allocation(other.allocation),
          alloc_info(other.alloc_info), size(other.size) {
        other.buffer = VK_NULL_HANDLE;
        other.allocation = VK_NULL_HANDLE;
        other.size = 0;
    }

    VulkanBuffer& operator=(VulkanBuffer&& other) noexcept {
        if (this != &other) {
            buffer = other.buffer;
            allocation = other.allocation;
            alloc_info = other.alloc_info;
            size = other.size;
            other.buffer = VK_NULL_HANDLE;
            other.allocation = VK_NULL_HANDLE;
            other.size = 0;
        }
        return *this;
    }

    [[nodiscard]] bool isValid() const { return buffer != VK_NULL_HANDLE; }
    [[nodiscard]] void* getMappedData() const { return alloc_info.pMappedData; }
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

    VulkanBuffer createStagingBuffer(VkDeviceSize size, bool for_upload = true);

    void destroyBuffer(VulkanBuffer& buffer);

    void uploadToBuffer(VulkanBuffer& gpu_buffer, const void* data,
                        VkDeviceSize size, VkDeviceSize offset = 0);

    void downloadFromBuffer(VulkanBuffer& gpu_buffer, void* data,
                            VkDeviceSize size, VkDeviceSize offset = 0);

    void copyBuffer(VulkanBuffer& src, VulkanBuffer& dst, VkDeviceSize size);

    [[nodiscard]] VmaAllocator getAllocator() const { return allocator; }

    void flushBuffer(const VulkanBuffer& buffer, VkDeviceSize size,
                     VkDeviceSize offset = 0) const;
    void invalidateBuffer(const VulkanBuffer& buffer, VkDeviceSize size,
                          VkDeviceSize offset = 0) const;

  private:
    VulkanContext& context;
    VmaAllocator allocator = VK_NULL_HANDLE;
    vk::raii::CommandPool transfer_pool = nullptr;
    vk::raii::CommandBuffer transfer_cmd = nullptr;
    vk::raii::Fence transfer_fence = nullptr;
};

} // namespace llvmexpr

#endif // LLVMEXPR_RUNTIME_VULKAN_VULKANMEMORY_HPP
