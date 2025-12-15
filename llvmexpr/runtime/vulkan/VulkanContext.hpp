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

#ifndef LLVMEXPR_RUNTIME_VULKAN_VULKANCONTEXT_HPP
#define LLVMEXPR_RUNTIME_VULKAN_VULKANCONTEXT_HPP

// NOLINTBEGIN(cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum)

#define VK_NO_PROTOTYPES
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

// NOLINTEND(cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum)

#include <mutex>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_raii.hpp>

namespace llvmexpr {

class VulkanContext {
  public:
    static VulkanContext& getInstance();

    VulkanContext();
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;

    vk::raii::Context& getContext() { return context; }
    vk::raii::Instance& getInstanceRef() { return instance; }
    vk::raii::PhysicalDevice& getPhysicalDevice() { return physical_device; }
    vk::raii::Device& getDevice() { return device; }
    vk::raii::Queue& getComputeQueue() { return compute_queue; }
    [[nodiscard]] uint32_t getQueueFamilyIndex() const {
        return queue_family_index;
    }

    void submit(const vk::SubmitInfo& submit_info, const vk::Fence& fence);
    void waitIdle();

  private:
    void createInstance();
    void pickPhysicalDevice();
    void createDevice();

    vk::raii::Context context;
    vk::raii::Instance instance;
    vk::raii::PhysicalDevice physical_device = nullptr;
    vk::raii::Device device = nullptr;
    vk::raii::Queue compute_queue = nullptr;

    uint32_t queue_family_index = -1;
    std::mutex queue_mutex;
};

} // namespace llvmexpr

#endif // LLVMEXPR_RUNTIME_VULKAN_VULKANCONTEXT_HPP
