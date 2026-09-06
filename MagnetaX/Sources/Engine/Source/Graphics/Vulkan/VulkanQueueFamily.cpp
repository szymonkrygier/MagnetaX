// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#include "VulkanQueueFamily.h"

#if MX_GRAPHICS_VULKAN
#include <vector>

VulkanQueueFamily::Indices VulkanQueueFamily::FindQueueFamilies(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
    Indices result{};

    uint32 count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, queueFamilies.data());

    for (uint32 i = 0; i < count; ++i)
    {
        if (queueFamilies[i].queueCount == 0) continue;

        const VkQueueFlags queueFlags = queueFamilies[i].queueFlags;
        const VkQueueFlags requiredGraphicsFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;

        if (!result.HasGraphics() && (queueFlags & requiredGraphicsFlags) == requiredGraphicsFlags) result.graphics = i;

        if (queueFlags & VK_QUEUE_TRANSFER_BIT)
        {
            const bool dedicatedTransfer = (queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0;

            if (!result.HasTransfer() || dedicatedTransfer) result.transfer = i;
        }

        if (surface != VK_NULL_HANDLE && !result.HasPresent())
        {
            VkBool32 supportsPresent = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &supportsPresent);

            if (supportsPresent) result.present = i;
        }

        if (result.IsCoreComplete() && result.HasTransfer() && (surface == VK_NULL_HANDLE)) break;
    }

    if (!result.HasTransfer()) result.transfer = result.graphics;

    return result;
}
#endif
