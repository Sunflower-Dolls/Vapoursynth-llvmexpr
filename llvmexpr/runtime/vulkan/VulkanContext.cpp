#include "VulkanContext.hpp"
#include <iostream>
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
    vk::ApplicationInfo appInfo("Vapoursynth-llvmexpr", 1, "No Engine", 1,
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

    vk::InstanceCreateInfo createInfo(
        vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR, &appInfo, layers,
        extensions);

    try {
        instance = vk::raii::Instance(context, createInfo);
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
    std::cout << "Available Physical Devices:" << '\n';
    for (const auto& dev : devices) {
        auto props = dev.getProperties();
        std::cout << "  - " << props.deviceName << '\n';

        // Check for compute queue
        auto queueFamilies = dev.getQueueFamilyProperties();
        int i = 0;
        for (const auto& queueFamily : queueFamilies) {
            if (queueFamily.queueFlags & vk::QueueFlagBits::eCompute) {
                physicalDevice = dev;
                queueFamilyIndex = i;
                break;
            }
            i++;
        }
        if (*physicalDevice) {
            std::cout << "    Selected Device: " << props.deviceName << '\n';
            break;
        }
    }

    if (!*physicalDevice) {
        throw std::runtime_error(
            "Failed to find a suitable GPU with Compute capabilities!");
    }
}

void VulkanContext::createDevice() {
    float queuePriority = 1.0F;
    vk::DeviceQueueCreateInfo queueCreateInfo({}, queueFamilyIndex, 1,
                                              &queuePriority);

    std::vector<const char*> deviceExtensions;
    // macOS MoltenVK compatibility
    deviceExtensions.push_back("VK_KHR_portability_subset");

    vk::DeviceCreateInfo createInfo({}, queueCreateInfo,
                                    {}, // layers deprecated
                                    deviceExtensions);

    device = vk::raii::Device(physicalDevice, createInfo);

    // Load device specific pointers
    volkLoadDevice(*device);

    computeQueue = device.getQueue(queueFamilyIndex, 0);
}

} // namespace llvmexpr
