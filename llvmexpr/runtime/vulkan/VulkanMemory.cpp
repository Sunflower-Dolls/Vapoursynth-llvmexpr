#include "VulkanMemory.hpp"
#include "VulkanContext.hpp"

#include <cstring>
#include <stdexcept>
#include <volk.h>

// NOLINTBEGIN(cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum)

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

// NOLINTEND(cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum)

#include <vk_mem_alloc.h>

namespace llvmexpr {

VulkanMemory::VulkanMemory(VulkanContext& ctx) : context(ctx) {
    VmaVulkanFunctions vulkanFunctions = {};
    vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.physicalDevice = *context.getPhysicalDevice();
    allocatorInfo.device = *context.getDevice();
    allocatorInfo.instance = *context.getInstanceRef();
    allocatorInfo.pVulkanFunctions = &vulkanFunctions;

    if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VMA allocator");
    }

    vk::CommandPoolCreateInfo poolInfo(
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        context.getQueueFamilyIndex());
    transferPool = vk::raii::CommandPool(context.getDevice(), poolInfo);

    vk::CommandBufferAllocateInfo cmdInfo(*transferPool,
                                          vk::CommandBufferLevel::ePrimary, 1);
    auto cmdBuffers = vk::raii::CommandBuffers(context.getDevice(), cmdInfo);
    transferCmd = std::move(cmdBuffers[0]);

    vk::FenceCreateInfo fenceInfo;
    transferFence = vk::raii::Fence(context.getDevice(), fenceInfo);
}

VulkanMemory::~VulkanMemory() {
    if (allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator);
    }
}

VulkanBuffer VulkanMemory::createGPUBuffer(VkDeviceSize size,
                                           VkBufferUsageFlags usage) {
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocationInfo = {};

    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer,
                        &allocation, &allocationInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GPU buffer");
    }

    return VulkanBuffer(buffer, allocation, allocationInfo, size);
}

VulkanBuffer VulkanMemory::createStagingBuffer(VkDeviceSize size,
                                               bool forUpload) {
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = forUpload ? VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                                 : VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;
    if (!forUpload) {
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocationInfo = {};

    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer,
                        &allocation, &allocationInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create staging buffer");
    }

    return VulkanBuffer(buffer, allocation, allocationInfo, size);
}

void VulkanMemory::destroyBuffer(VulkanBuffer& buffer) {
    if (buffer.isValid()) {
        vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
        buffer.buffer = VK_NULL_HANDLE;
        buffer.allocation = VK_NULL_HANDLE;
        buffer.size = 0;
    }
}

void VulkanMemory::uploadToBuffer(VulkanBuffer& gpuBuffer, const void* data,
                                  VkDeviceSize size, VkDeviceSize offset) {
    auto staging = createStagingBuffer(size, true);

    std::memcpy(staging.getMappedData(), data, size);

    vk::CommandBufferBeginInfo beginInfo(
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    transferCmd.begin(beginInfo);

    vk::BufferCopy copyRegion(0, offset, size);
    transferCmd.copyBuffer(staging.buffer, gpuBuffer.buffer, copyRegion);

    transferCmd.end();

    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(*transferCmd);
    context.getComputeQueue().submit(submitInfo, *transferFence);

    auto result =
        context.getDevice().waitForFences(*transferFence, VK_TRUE, UINT64_MAX);
    if (result != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to wait for upload fence");
    }
    context.getDevice().resetFences(*transferFence);

    destroyBuffer(staging);
}

void VulkanMemory::downloadFromBuffer(VulkanBuffer& gpuBuffer, void* data,
                                      VkDeviceSize size, VkDeviceSize offset) {
    auto staging = createStagingBuffer(size, false);

    vk::CommandBufferBeginInfo beginInfo(
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    transferCmd.begin(beginInfo);

    vk::BufferCopy copyRegion(offset, 0, size);
    transferCmd.copyBuffer(gpuBuffer.buffer, staging.buffer, copyRegion);

    transferCmd.end();

    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(*transferCmd);
    context.getComputeQueue().submit(submitInfo, *transferFence);

    auto result =
        context.getDevice().waitForFences(*transferFence, VK_TRUE, UINT64_MAX);
    if (result != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to wait for download fence");
    }
    context.getDevice().resetFences(*transferFence);

    std::memcpy(data, staging.getMappedData(), size);

    destroyBuffer(staging);
}

void VulkanMemory::copyBuffer(VulkanBuffer& src, VulkanBuffer& dst,
                              VkDeviceSize size) {
    vk::CommandBufferBeginInfo beginInfo(
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    transferCmd.begin(beginInfo);

    vk::BufferCopy copyRegion(0, 0, size);
    transferCmd.copyBuffer(src.buffer, dst.buffer, copyRegion);

    transferCmd.end();

    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(*transferCmd);
    context.getComputeQueue().submit(submitInfo, *transferFence);

    auto result =
        context.getDevice().waitForFences(*transferFence, VK_TRUE, UINT64_MAX);
    if (result != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to wait for copy fence");
    }
    context.getDevice().resetFences(*transferFence);
}

} // namespace llvmexpr
