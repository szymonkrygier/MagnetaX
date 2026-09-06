// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include "../VulkanPass.h"
#include "../VulkanPipeline.h"

struct VulkanToneMapPassCreateInfo : VulkanPassCreateInfo
{
    const VulkanImage* srcImage = nullptr;
    VkFormat outFormat = VK_FORMAT_UNDEFINED;
};

struct VulkanToneMapPassRenderInfo : VulkanPassRenderInfo
{
    VkImageView srcView = VK_NULL_HANDLE;
    VkImageView targetView = VK_NULL_HANDLE;
    VkExtent2D extent{};
    float32 exposureEV = 0.0f;
};

class VulkanToneMapPass
{
public:
    VulkanToneMapPass() = default;

    bool Create(const VulkanToneMapPassCreateInfo& createInfo);
    void Destroy();

    void Record(const VulkanToneMapPassRenderInfo& renderInfo);

private:
    VkDevice device = VK_NULL_HANDLE;

    VulkanPipeline pipeline;

    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout descSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descPool = VK_NULL_HANDLE;
    VkDescriptorSet descSet = VK_NULL_HANDLE;
};
