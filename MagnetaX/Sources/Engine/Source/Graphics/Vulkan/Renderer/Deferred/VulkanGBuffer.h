// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <Graphics/Vulkan/Resources/VulkanImage.h>

struct VulkanGBufferCreateInfo
{
    VulkanDevice* device;
    VkExtent2D extent;
    bool createVelocity = false;
    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
};

class VulkanGBuffer
{
public:
    VulkanGBuffer() = default;
    VulkanGBuffer(const VulkanGBuffer&) = delete;
    VulkanGBuffer& operator=(const VulkanGBuffer&) = delete;

    bool Create(const VulkanGBufferCreateInfo& createInfo);
    void Destroy();

    const VulkanImage& GetAlbedoImage() const { return albedoImage; }
    const VulkanImage& GetNormalImage() const { return normalImage; }
    const VulkanImage& GetMaterialImage() const { return materialImage; }
    const VulkanImage& GetVelocityImage() const { return velocityImage; }
    const VulkanImage& GetDepthImage() const { return depthImage; }

    VkExtent2D GetExtent() const { return extent; }

private:
    VulkanImage albedoImage;
    VulkanImage normalImage;
    VulkanImage materialImage;
    VulkanImage velocityImage;
    VulkanImage depthImage;

    VkExtent2D extent{};

};
