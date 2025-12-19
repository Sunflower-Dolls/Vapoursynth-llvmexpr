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
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#ifndef NDEBUG
#include <iostream>
#endif
#include <vector>
#include <volk.h>

// NOLINTNEXTLINE
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace vkexpr {

namespace {

std::optional<uint32_t>
find_compute_queue_family_index(const vk::raii::PhysicalDevice& dev) {
    const auto queue_families = dev.getQueueFamilyProperties();
    for (size_t i = 0; i < queue_families.size(); ++i) {
        if (queue_families[i].queueFlags & vk::QueueFlagBits::eCompute) {
            return static_cast<uint32_t>(i);
        }
    }
    return std::nullopt;
}

std::string format_physical_devices(const vk::raii::PhysicalDevices& devices) {
    std::string result = "Available Physical Devices:\n";
    for (size_t idx = 0; idx < devices.size(); ++idx) {
        const auto props = devices[idx].getProperties();
        result +=
            std::format("  [{}] {}\n", idx, std::string(props.deviceName));
    }
    return result;
}

} // namespace

VulkanContext& VulkanContext::getInstance() { return getInstance(-1); }

VulkanContext& VulkanContext::getInstance(int device_id) {
    static bool initialized = []() { return volkInitialize() == VK_SUCCESS; }();
    if (!initialized) {
        throw std::runtime_error("Failed to initialize volk");
    }

    struct NoDestroy {
        void operator()(VulkanContext* /*ptr*/) const {}
    };
    using ContextPtr = std::unique_ptr<VulkanContext, NoDestroy>;

    static std::mutex context_mutex;
    static std::unordered_map<int, ContextPtr> contexts;

    int key = device_id;
    if (key < 0) {
        key = -1;
    }

    std::lock_guard<std::mutex> lock(context_mutex);
    auto it = contexts.find(key);
    if (it != contexts.end()) {
        return *it->second;
    }

    auto created = std::make_unique<VulkanContext>(key);
    VulkanContext& ref = *created;

    // Leaky Singleton
    contexts.emplace(key, ContextPtr(created.release()));
    return ref;
}

VulkanContext::VulkanContext(int device_id)
    : device_id(device_id), instance(nullptr) {

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

#ifndef NDEBUG
    std::cerr << format_physical_devices(devices);
#endif

    auto select_device = [&](const vk::raii::PhysicalDevice& dev) -> bool {
        const auto compute_queue = find_compute_queue_family_index(dev);
        if (!compute_queue.has_value()) {
            return false;
        }
        physical_device = dev;
        queue_family_index = *compute_queue;
#ifndef NDEBUG
        const auto props = dev.getProperties();
        std::cout << "Selected Device: " << props.deviceName << '\n';
#endif
        return true;
    };

    if (device_id >= 0) {
        if (static_cast<size_t>(device_id) >= devices.size()) {
            throw std::runtime_error(
                std::format("Invalid device_id: {}\n{}", device_id,
                            format_physical_devices(devices)));
        }

        const auto& dev = devices[static_cast<size_t>(device_id)];
        if (!select_device(dev)) {
            throw std::runtime_error(
                std::format("Selected device_id {} has no compute queue\n{}",
                            device_id, format_physical_devices(devices)));
        }
        return;
    }

    if (std::ranges::any_of(devices, select_device)) {
        return;
    }

    throw std::runtime_error(std::format(
        "Failed to find a suitable GPU with Compute capabilities!\n{}",
        format_physical_devices(devices)));
}

void VulkanContext::createDevice() {
    float queue_priority = 1.0F;
    vk::DeviceQueueCreateInfo queue_create_info({}, queue_family_index, 1,
                                                &queue_priority);

    std::vector<const char*> device_extensions;

    std::vector<vk::ExtensionProperties> available_extensions =
        physical_device.enumerateDeviceExtensionProperties();
    const bool has_portability_subset = std::ranges::any_of(
        available_extensions, [](const vk::ExtensionProperties& ext) {
            return strcmp(ext.extensionName, "VK_KHR_portability_subset") == 0;
        });
    if (has_portability_subset) {
        device_extensions.push_back("VK_KHR_portability_subset");
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

} // namespace vkexpr
