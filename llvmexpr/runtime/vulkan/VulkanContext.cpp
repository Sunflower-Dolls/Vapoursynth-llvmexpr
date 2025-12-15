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
#include <algorithm>
#include <cstring>
#ifdef _WIN32
#include <memory>
#endif
#ifndef NDEBUG
#include <iostream>
#endif
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

#ifdef _WIN32
    struct NoDestroy {
        void operator()(VulkanContext* /*unused*/) const noexcept {}
    };
    static auto instance = []() {
        auto owned = std::make_unique<VulkanContext>();
        return std::unique_ptr<VulkanContext, NoDestroy>(owned.release());
    }();
    return *instance;
#else
    static VulkanContext instance;
    return instance;
#endif
}

VulkanContext::VulkanContext() : instance(nullptr) {

    createInstance();
    // Re-load volk with the instance
    volkLoadInstance(*instance);

    pickPhysicalDevice();
    createDevice();
}

VulkanContext::~VulkanContext() {
    try {
        waitIdle();
    } catch (...) {
    }
}

void VulkanContext::createInstance() {
    vk::ApplicationInfo app_info("Vapoursynth-llvmexpr", 1, "No Engine", 1,
                                 VK_API_VERSION_1_3);

    std::vector<const char*> layers;
// Enable validation layers in debug if needed
#ifndef NDEBUG
    std::vector<vk::LayerProperties> available_layers =
        context.enumerateInstanceLayerProperties();
    auto has_layer = [&](const char* name) {
        return std::ranges::any_of(available_layers, [&](const auto& layer) {
            return strcmp(layer.layerName, name) == 0;
        });
    };
    if (has_layer("VK_LAYER_KHRONOS_validation")) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }
#endif

    std::vector<const char*> extensions;
    vk::InstanceCreateFlags flags;

    // Check available instance extensions
    std::vector<vk::ExtensionProperties> instance_extensions =
        context.enumerateInstanceExtensionProperties();

    auto has_extension = [&](const char* name) {
        return std::ranges::any_of(instance_extensions, [&](const auto& ext) {
            return strcmp(ext.extensionName, name) == 0;
        });
    };

    if (has_extension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
    }
    if (has_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)) {
        extensions.push_back(
            VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    }

    vk::InstanceCreateInfo create_info(flags, &app_info, layers, extensions);

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
#ifndef NDEBUG
    std::cout << "Available Physical Devices:" << '\n';
#endif
    for (const auto& dev : devices) {
#ifndef NDEBUG
        auto props = dev.getProperties();
        std::cout << "  - " << props.deviceName << '\n';
#endif

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
#ifndef NDEBUG
            std::cout << "    Selected Device: " << props.deviceName << '\n';
#endif
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

    std::vector<vk::ExtensionProperties> available_extensions =
        physical_device.enumerateDeviceExtensionProperties();
    for (const auto& ext : available_extensions) {
        if (strcmp(ext.extensionName, "VK_KHR_portability_subset") == 0) {
            device_extensions.push_back("VK_KHR_portability_subset");
            break;
        }
    }

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

void VulkanContext::waitIdle() {
    if (!*device) {
        return;
    }
    std::lock_guard<std::mutex> lock(queue_mutex);
    device.waitIdle();
}

} // namespace llvmexpr
