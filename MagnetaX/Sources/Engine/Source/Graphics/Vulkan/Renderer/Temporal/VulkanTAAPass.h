// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <MX/Core/Math/Matrix.h>
#include <MX/Core/Math/Vector.h>
#include "../VulkanPass.h"
#include "../VulkanPipeline.h"

struct VulkanTAAPassCreateInfo : VulkanPassCreateInfo
{
    VulkanImage* currentColor = nullptr;
    VkFormat outFormat = VK_FORMAT_UNDEFINED;

    const VulkanImage* velocityImage = nullptr;
    const VulkanImage* depthImage = nullptr;
};

struct VulkanTAAPassRenderInfo : VulkanPassRenderInfo
{
    VkImageView historyView = VK_NULL_HANDLE;
    VkImageView targetView = VK_NULL_HANDLE;
    VkImageView previousDepthView = VK_NULL_HANDLE;
    VkExtent2D extent{};

    Vector2f jitterUV{};
    Vector2f prevJitterUV{};
    float32 feedbackMin = 0.88f;
    float32 feedbackMax = 0.97f;

    bool historyValid = false;
};

class VulkanTAAPass
{
public:
    VulkanTAAPass() = default;

    bool Create(const VulkanTAAPassCreateInfo& createInfo);
    void Destroy();

    void Record(const VulkanTAAPassRenderInfo& renderInfo);

private:
    VkDevice device = VK_NULL_HANDLE;

    VulkanPipeline pipeline;

    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout descSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descPool = VK_NULL_HANDLE;
    VkDescriptorSet descSet = VK_NULL_HANDLE;
};
