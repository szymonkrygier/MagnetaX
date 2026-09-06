// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#include "VulkanDevice.h"

#if MX_GRAPHICS_VULKAN
#include "VulkanQueueFamily.h"
#include "Resources/VulkanImageFormat.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#if MX_GRAPHICS_VULKAN_DEBUG
#include <iostream>
#endif

namespace
{
    std::vector<const char*> GetPhysicalDeviceExts()
    {
        std::vector<const char*> exts;

        // Swapchain is mandatory on all platforms
        exts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

        // Because of MoltenVK on Apple we need VK_KHR_portability_subset 
        #if MX_PLATFORM_APPLE
        exts.push_back("VK_KHR_portability_subset");
        #endif

        return exts;
    }

    bool CheckPhysicalDeviceExtsSupport(VkPhysicalDevice physicalDevice, const std::vector<const char*>& exts)
    {
        uint32 count = 0;
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);

        std::vector<VkExtensionProperties> availableExts(count);
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, availableExts.data());

        for (const char* expectedExt : exts)
        {
            bool found = false;

            for (const VkExtensionProperties& availableExt : availableExts)
            {
                if (std::strcmp(expectedExt, availableExt.extensionName) == 0)
                {
                    found = true;
                    break;
                }
            }

            if (!found) return false;
        }

        return true;
    }

    bool CheckSwapchainSupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
    {
        uint32 formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);

        if (formatCount == 0) return false;

        uint32 presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);

        return presentModeCount > 0;
    }

    /*
    * Mandatory reqs:
    * + Vulkan version from MX_GRAPHICS_VULKAN_MIN_VERSION or higher
    * + Dynamic rendering
    * + synchronization2
    * + Extensions from GetPhysicalDeviceExts()
    * + GFX queue families
    */
    bool IsPhysicalDeviceSupported(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physicalDevice, &props);

        // Vulkan version defined in MX_GRAPHICS_VULKAN_MIN_VERSION or higher
        if (props.apiVersion < MX_GRAPHICS_VULKAN_MIN_VERSION) return false;

        // Features: dynamic rendering and synchronization2
        VkPhysicalDeviceVulkan13Features vk13Features{};
        vk13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        vk13Features.pNext = nullptr;

        VkPhysicalDeviceFeatures2 vkFeatures2{};
        vkFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        vkFeatures2.pNext = &vk13Features;

        vkGetPhysicalDeviceFeatures2(physicalDevice, &vkFeatures2);

        if (!vk13Features.dynamicRendering || !vk13Features.synchronization2) return false;

        // Required extensions
        if (!CheckPhysicalDeviceExtsSupport(physicalDevice, GetPhysicalDeviceExts())) return false;
        if (!CheckSwapchainSupport(physicalDevice, surface)) return false;

        // GFX queue families (core)
        const VulkanQueueFamily::Indices queueFamilyIndices = VulkanQueueFamily::FindQueueFamilies(physicalDevice, surface);
        if (!queueFamilyIndices.IsComplete()) return false;

        return true;
    }

    /*
    * 0 means it's not suitable
    * 1 is worst possible score but device should work
    *
    * I should review it later because this code might be
    * ultra bad. It works at this moment but should test
    * with other setups, etc..
    */
    uint32 ScorePhysicalDevice(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physicalDevice, &props);

        if (!IsPhysicalDeviceSupported(physicalDevice, surface)) return 0;

        uint32 currentScore = 1;

        // Score by type
        switch (props.deviceType)
        {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        {
            currentScore += 10000;
            break;
        }
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        {
            currentScore += 1000;
            break;
        }
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        {
            currentScore += 100;
            break;
        }
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
        {
            currentScore += 10;
            break;
        }
        case VK_PHYSICAL_DEVICE_TYPE_OTHER:
        {
            // Check what other exactly means?
            break;
        }
        default:
            break;
        }

        // Score by max supported API version? I think no need

        // Score by memory caps
        VkPhysicalDeviceMemoryProperties memoryProps{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProps);

        VkDeviceSize localBytes = 0;

        for (uint32 i = 0; i < memoryProps.memoryHeapCount; ++i)
        {
            if (memoryProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            {
                localBytes = std::max(localBytes, memoryProps.memoryHeaps[i].size);
            }
        }

        // I think it's unfair compared to type scoring and should be normalized
        currentScore += (uint32)(localBytes / (1024ull * 1024ull * 256ull));

        // Score by available queues? Maybe... in the future

        return currentScore;
    }

    void DebugLog(const std::string& msg)
    {
    #if MX_GRAPHICS_VULKAN_DEBUG
        std::clog << "MX: " << msg << std::endl;
    #else
        (void)msg;
    #endif
    }
}

bool VulkanDevice::Create(VkInstance instance, VkSurfaceKHR surface)
{
    if (!instance || !surface) return false;

    Destroy();

    DebugLog("Creating VulkanDevice instance");

    if (!CreateDevice(instance, surface))
    {
        Destroy();
        return false;
    }

    return true;
}

void VulkanDevice::Destroy()
{
    DebugLog("Destroying VulkanDevice instance");

    DestroyDevice();
}

uint32 VulkanDevice::FindMemoryType(uint32 type, VkMemoryPropertyFlags memoryProps) const
{
    if (!physicalDevice) return UINT32_MAX;

    VkPhysicalDeviceMemoryProperties currentProps{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &currentProps);

    for (uint32 i = 0; i < currentProps.memoryTypeCount; ++i)
    {
        const bool typeSupported = type & (1u << i);
        const bool propsSupported = (currentProps.memoryTypes[i].propertyFlags & memoryProps) == memoryProps;

        if (typeSupported && propsSupported) return i;
    }

    return UINT32_MAX;
}

bool VulkanDevice::SupportsImageSampleCount(ImageFormat format, VkImageUsageFlags usage, VkSampleCountFlagBits sampleCount) const
{
    if (!physicalDevice || usage == 0 || sampleCount == 0) return false;

    const VkFormat vkFormat = VulkanImageFormat::FromImageFormat(format);
    if (vkFormat == VK_FORMAT_UNDEFINED) return false;

    VkImageFormatProperties properties{};

    if (vkGetPhysicalDeviceImageFormatProperties(physicalDevice, vkFormat, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0, &properties) != VK_SUCCESS)
    {
        return false;
    }

    return (properties.sampleCounts & sampleCount) == sampleCount;
}

bool VulkanDevice::CreateDevice(VkInstance instance, VkSurfaceKHR surface)
{
    DebugLog("Creating Vulkan device");

    uint32 physicalDevicesCount = 0;

    if (vkEnumeratePhysicalDevices(instance, &physicalDevicesCount, nullptr) != VK_SUCCESS) return false;

    if (physicalDevicesCount == 0)
    {
        DebugLog("No physical devices found");

        return false;
    }

    DebugLog("Found " + std::to_string(physicalDevicesCount) + " devices");

    std::vector<VkPhysicalDevice> availPhysicalDevices(physicalDevicesCount);

    if (vkEnumeratePhysicalDevices(instance, &physicalDevicesCount, availPhysicalDevices.data()) != VK_SUCCESS) return false;

    // Choose physical device (best and suitable)
    VkPhysicalDevice bestPhysicalDevice = VK_NULL_HANDLE;
    uint32 bestScore = 0;

    for (VkPhysicalDevice candidate : availPhysicalDevices)
    {
        if (!candidate) continue;

        const uint32 buffScore = ScorePhysicalDevice(candidate, surface);
        if (buffScore <= bestScore) continue;

        bestPhysicalDevice = candidate;
        bestScore = buffScore;
    }

    if (bestScore == 0)
    {
        DebugLog("No suitable physical devices available");

        return false;
    }

    physicalDevice = bestPhysicalDevice;

    // Get device properties and features
    VkPhysicalDeviceProperties physicalDeviceProps{};
    vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProps);

    info.name = physicalDeviceProps.deviceName;

    DebugLog("Selected device: " + info.name + " with score: " + std::to_string(bestScore));

    VkPhysicalDeviceFeatures physicalDeviceFeatures{};
    vkGetPhysicalDeviceFeatures(physicalDevice, &physicalDeviceFeatures);

    capabilities.samplerAniso = physicalDeviceFeatures.samplerAnisotropy == VK_TRUE;
    capabilities.maxSamplerAniso = capabilities.samplerAniso ? physicalDeviceProps.limits.maxSamplerAnisotropy : 1.0f;
    capabilities.sampleRateShading = physicalDeviceFeatures.sampleRateShading == VK_TRUE;

    // Find core queue families
    const VulkanQueueFamily::Indices queueFamilyIndices = VulkanQueueFamily::FindQueueFamilies(physicalDevice, surface);

    if (!queueFamilyIndices.IsComplete())
    {
        physicalDevice = VK_NULL_HANDLE;

        DebugLog("Selected physical device has no required queue families");

        return false;
    }

    graphicsQueueFamily = queueFamilyIndices.graphics;
    presentQueueFamily = queueFamilyIndices.present;
    transferQueueFamily = queueFamilyIndices.transfer;

    // Configure Vulkan 1.3 features + features2
    VkPhysicalDeviceVulkan13Features vk13Features{};
    vk13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vk13Features.dynamicRendering = VK_TRUE;
    vk13Features.synchronization2 = VK_TRUE;
    vk13Features.pNext = nullptr;

    VkPhysicalDeviceFeatures2 vkFeatures2{};
    vkFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    vkFeatures2.features.samplerAnisotropy = capabilities.samplerAniso ? VK_TRUE : VK_FALSE;
    vkFeatures2.features.sampleRateShading = capabilities.sampleRateShading ? VK_TRUE : VK_FALSE;
    vkFeatures2.pNext = &vk13Features;

    // Create queue infos
    std::vector<uint32> queueFamilies;
    queueFamilies.push_back(graphicsQueueFamily);

    if (presentQueueFamily != graphicsQueueFamily)
    {
        queueFamilies.push_back(presentQueueFamily);
    }

    if (transferQueueFamily != graphicsQueueFamily && transferQueueFamily != presentQueueFamily)
    {
        queueFamilies.push_back(transferQueueFamily);
    }

    const float32 queuePriority = 1.0f;

    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    queueInfos.reserve(queueFamilies.size());

    for (uint32 queueFamily : queueFamilies)
    {
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = queueFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;
        queueInfo.pNext = nullptr;

        queueInfos.push_back(queueInfo);
    }

    // Create Vulkan device
    const std::vector<const char*> deviceExts = GetPhysicalDeviceExts();

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = (uint32)queueInfos.size();
    deviceInfo.pQueueCreateInfos = queueInfos.data();
    deviceInfo.enabledExtensionCount = (uint32)deviceExts.size();
    deviceInfo.ppEnabledExtensionNames = deviceExts.data();
    deviceInfo.pEnabledFeatures = nullptr;
    deviceInfo.pNext = &vkFeatures2;

    if (vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device) != VK_SUCCESS)
    {
        DebugLog("Failed to create Vulkan device!");

        DestroyDevice();
        return false;
    }

    // Get queues
    vkGetDeviceQueue(device, graphicsQueueFamily, 0, &graphicsQueue);
    vkGetDeviceQueue(device, presentQueueFamily, 0, &presentQueue);
    vkGetDeviceQueue(device, transferQueueFamily, 0, &transferQueue);

    DebugLog("Vulkan device created");

    return true;
}

void VulkanDevice::DestroyDevice()
{
    if (device)
    {
        vkDeviceWaitIdle(device);
        vkDestroyDevice(device, nullptr);
    }

    device = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE;

    graphicsQueueFamily = UINT32_MAX;
    presentQueueFamily = UINT32_MAX;
    transferQueueFamily = UINT32_MAX;

    graphicsQueue = VK_NULL_HANDLE;
    presentQueue = VK_NULL_HANDLE;
    transferQueue = VK_NULL_HANDLE;

    capabilities = {};
    info = {};
}
#endif
