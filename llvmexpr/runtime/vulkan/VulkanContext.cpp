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

#include "VulkanContext.hpp"
#include <vector>
#include <volk.h>

// NOLINTNEXTLINE
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace llvmexpr {

VulkanContext& VulkanContext::getInstance() {
    static bool initialized = []() { return volkInitialize() == VK_SUCCESS; }();
    if (!initialized) {
        throw std::runtime_error("Failed to initialize volk");
    }

    static VulkanContext instance;
    return instance;
}

VulkanContext::VulkanContext() : instance(nullptr) {

    createInstance();
    // Re-load volk with the instance
    volkLoadInstance(*instance);

    pickPhysicalDevice();
    createDevice();
}

VulkanContext::~VulkanContext() = default;

void VulkanContext::createInstance() {
    vk::ApplicationInfo app_info("Vapoursynth-llvmexpr", 1, "No Engine", 1,
                                 VK_API_VERSION_1_3);

    std::vector<const char*> layers;
// Enable validation layers in debug if needed
#ifndef NDEBUG
    layers.push_back("VK_LAYER_KHRONOS_validation");
#endif

    std::vector<const char*> extensions;
    // macOS requires portability enumeration
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    extensions.push_back(
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

    vk::InstanceCreateInfo create_info(
        vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR, &app_info, layers,
        extensions);

    try {
        instance = vk::raii::Instance(context, create_info);
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("Failed to create Vulkan instance: ") + e.what());
    }
}

void VulkanContext::pickPhysicalDevice() {
    vk::raii::PhysicalDevices devices(instance);
    if (devices.empty()) {
        throw std::runtime_error("Failed to find GPUs with Vulkan support!");
    }

    // TODO: Finish device selection
    // std::cout << "Available Physical Devices:" << '\n';
    for (const auto& dev : devices) {
        // auto props = dev.getProperties();
        // std::cout << "  - " << props.deviceName << '\n';

        // Check for compute queue
        auto queue_families = dev.getQueueFamilyProperties();
        int i = 0;
        for (const auto& queue_family : queue_families) {
            if (queue_family.queueFlags & vk::QueueFlagBits::eCompute) {
                physical_device = dev;
                queue_family_index = i;
                break;
            }
            i++;
        }
        if (*physical_device) {
            // std::cout << "    Selected Device: " << props.deviceName << '\n';
            break;
        }
    }

    if (!*physical_device) {
        throw std::runtime_error(
            "Failed to find a suitable GPU with Compute capabilities!");
    }
}

void VulkanContext::createDevice() {
    float queue_priority = 1.0F;
    vk::DeviceQueueCreateInfo queue_create_info({}, queue_family_index, 1,
                                                &queue_priority);

    std::vector<const char*> device_extensions;
    // macOS MoltenVK compatibility
    device_extensions.push_back("VK_KHR_portability_subset");

    vk::DeviceCreateInfo create_info({}, queue_create_info,
                                     {}, // layers deprecated
                                     device_extensions);

    device = vk::raii::Device(physical_device, create_info);

    // Load device specific pointers
    volkLoadDevice(*device);

    compute_queue = device.getQueue(queue_family_index, 0);
}

void VulkanContext::submit(const vk::SubmitInfo& submit_info,
                           const vk::Fence& fence) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    compute_queue.submit(submit_info, fence);
}

} // namespace llvmexpr
