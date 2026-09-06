// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <MX/Core/Math/Vector.h>
#include "../VulkanPass.h"
#include "../VulkanPipeline.h"

struct VulkanTAAPreparePassCreateInfo : VulkanPassCreateInfo
{
    const VulkanImage* depthImage = nullptr;
    const VulkanImage* velocityImage = nullptr;

    const VulkanImage* dilatedDepthImage = nullptr;
    const VulkanImage* dilatedVelocityImage = nullptr;

    const VulkanImage* reconstructedPrevDepthImage = nullptr;
};

struct VulkanTAAPreparePassRenderInfo : VulkanPassRenderInfo
{
    VkExtent2D extent{};

    Vector2f jitterUV{};
    Vector2f prevJitterUV{};
};

class VulkanTAAPreparePass
{
public:
    VulkanTAAPreparePass() = default;

    bool Create(const VulkanTAAPreparePassCreateInfo& createInfo);
    void Destroy();

    void Record(const VulkanTAAPreparePassRenderInfo& renderInfo);

private:
    VkDevice device = VK_NULL_HANDLE;

    VulkanPipeline pipeline;

    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout descSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descPool = VK_NULL_HANDLE;
    VkDescriptorSet descSet = VK_NULL_HANDLE;
};
