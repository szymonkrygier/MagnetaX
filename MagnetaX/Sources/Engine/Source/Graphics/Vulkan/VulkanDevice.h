// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <MX/Graphics/GraphicsCapabilities.h>
#include <MX/Graphics/GraphicsDeviceInfo.h>
#include <MX/Graphics/Resources/ImageFormat.h>
#include "VulkanCommon.h"

class VulkanDevice
{
public:
    VulkanDevice() = default;
    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    bool Create(VkInstance instance, VkSurfaceKHR surface);
    void Destroy();

    VkPhysicalDevice GetPhysicalDevice() const { return physicalDevice; }
    VkDevice GetDevice() const { return device; }

    uint32 GetGraphicsQueueFamily() const { return graphicsQueueFamily; }
    uint32 GetPresentQueueFamily() const { return presentQueueFamily; }
    uint32 GetTransferQueueFamily() const { return transferQueueFamily; }

    VkQueue GetGraphicsQueue() const { return graphicsQueue; }
    VkQueue GetPresentQueue() const { return presentQueue; }
    VkQueue GetTransferQueue() const { return transferQueue; }

    const GraphicsCapabilities& GetCapabilities() const { return capabilities; }
    const GraphicsDeviceInfo& GetInfo() const { return info; }

    uint32 FindMemoryType(uint32 type, VkMemoryPropertyFlags memoryProps) const;

    bool SupportsImageSampleCount(ImageFormat format, VkImageUsageFlags usage, VkSampleCountFlagBits sampleCount) const;

private:
    VkDevice device = VK_NULL_HANDLE;

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

    uint32 graphicsQueueFamily = UINT32_MAX;
    uint32 presentQueueFamily = UINT32_MAX;
    uint32 transferQueueFamily = UINT32_MAX;

    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkQueue transferQueue = VK_NULL_HANDLE;

    GraphicsCapabilities capabilities;
    GraphicsDeviceInfo info;

    bool CreateDevice(VkInstance instance, VkSurfaceKHR surface);
    void DestroyDevice();
};
