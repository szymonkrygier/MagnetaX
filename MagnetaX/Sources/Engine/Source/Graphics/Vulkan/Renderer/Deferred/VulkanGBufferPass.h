// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <Graphics/Vulkan/Resources/VulkanBuffer.h>
#include "VulkanGBuffer.h"
#include "../VulkanDrawItem.h"
#include "../VulkanPass.h"
#include "../VulkanPipeline.h"
#include <span>

struct RenderViewData;

struct VulkanGBufferPassCreateInfo : VulkanPassCreateInfo
{
    VkExtent2D extent{};
    VkDescriptorSetLayout materialDescSetLayout = VK_NULL_HANDLE;
    bool velocityEnabled = false;
    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
};

struct VulkanGBufferPassRenderInfo : VulkanPassRenderInfo
{
    std::span<const VulkanDrawItem> drawItems;
    std::span<const Matrix4f> prevModels;

    const RenderViewData* viewData = nullptr;
    Matrix4f prevViewProj = Matrix4f::Identity();
};

class VulkanGBufferPass
{
public:
    VulkanGBufferPass() = default;
    VulkanGBufferPass(const VulkanGBufferPass&) = delete;
    VulkanGBufferPass& operator=(const VulkanGBufferPass&) = delete;

    bool Create(const VulkanGBufferPassCreateInfo& createInfo);
    void Destroy();

    void Record(const VulkanGBufferPassRenderInfo& renderInfo);

    VulkanGBuffer& GetGBuffer() { return gBuffer; }
    const VulkanGBuffer& GetGBuffer() const { return gBuffer; }

private:
    VkDevice device = VK_NULL_HANDLE;

    VulkanGBuffer gBuffer;
    VulkanPipeline pipeline;

    VulkanBuffer frameDataBuffer;
    VkDescriptorSetLayout frameDescSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool frameDescPool = VK_NULL_HANDLE;
    VkDescriptorSet frameDescSet = VK_NULL_HANDLE;
};
