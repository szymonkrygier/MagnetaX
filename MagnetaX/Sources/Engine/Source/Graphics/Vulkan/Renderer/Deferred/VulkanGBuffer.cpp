// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#include "VulkanGBuffer.h"

#if MX_GRAPHICS_VULKAN
bool VulkanGBuffer::Create(const VulkanGBufferCreateInfo& createInfo)
{
    if (!createInfo.device || createInfo.extent.width == 0 || createInfo.extent.height == 0) return false;

    Destroy();

    extent = createInfo.extent;

    const VkImageUsageFlags colorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    const VkImageUsageFlags depthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VulkanImageCreateInfo imageInfo{};
    imageInfo.device = createInfo.device;
    imageInfo.extent = extent;
    imageInfo.usage = colorUsage;
    imageInfo.sampleCount = createInfo.sampleCount;

    imageInfo.format = ImageFormat::RGBA8_UNORM;
    if (!albedoImage.Create(imageInfo))
    {
        Destroy();
        return false;
    }

    imageInfo.format = ImageFormat::RGBA16_FLOAT;
    if (!normalImage.Create(imageInfo))
    {
        Destroy();
        return false;
    }

    imageInfo.format = ImageFormat::RGBA8_UNORM;
    if (!materialImage.Create(imageInfo))
    {
        Destroy();
        return false;
    }

    if (createInfo.createVelocity)
    {
        imageInfo.format = ImageFormat::RGBA16_FLOAT;

        if (!velocityImage.Create(imageInfo))
        {
            Destroy();
            return false;
        }
    }

    imageInfo.format = ImageFormat::D32_FLOAT;
    imageInfo.usage = depthUsage;
    if (!depthImage.Create(imageInfo))
    {
        Destroy();
        return false;
    }

    return true;
}

void VulkanGBuffer::Destroy()
{
    depthImage.Destroy();
    materialImage.Destroy();
    normalImage.Destroy();
    velocityImage.Destroy();
    albedoImage.Destroy();

    extent = {};
}
#endif
