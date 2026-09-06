// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include "../VulkanMinimal.h"

struct VulkanPipelineCreateInfo
{
    const uint32* vertexShader = nullptr;
    usize vertexShaderSize = 0;

    const uint32* fragmentShader = nullptr;
    usize fragmentShaderSize = 0;

    const VkVertexInputBindingDescription* vertexBindings = nullptr;
    uint32 vertexBindingCount = 0;

    const VkVertexInputAttributeDescription* vertexAttributes = nullptr;
    uint32 vertexAttributeCount = 0;

    const VkFormat* colorFormats = nullptr;
    uint32 colorFormatCount = 0;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;

    const VkDescriptorSetLayout* descriptorSetLayouts = nullptr;
    uint32 descriptorSetLayoutCount = 0;

    const VkPushConstantRange* pushConstantRanges = nullptr;
    uint32 pushConstantRangeCount = 0;

    bool depthTest = false;
    bool depthWrite = false;
    VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;

    bool depthBias = false;
    bool alphaBlending = false;

    VkCullModeFlags cullMode = VK_CULL_MODE_NONE;
    VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
    bool sampleShading = false;
};

struct VulkanComputePipelineCreateInfo
{
    const uint32* computeShader = nullptr;
    usize computeShaderSize = 0;

    const VkDescriptorSetLayout* descriptorSetLayouts = nullptr;
    uint32 descriptorSetLayoutCount = 0;

    const VkPushConstantRange* pushConstantRanges = nullptr;
    uint32 pushConstantRangeCount = 0;
};

class VulkanPipeline
{
public:
    VulkanPipeline() = default;
    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    bool Create(VkDevice _device, const VulkanPipelineCreateInfo& createInfo);
    bool Create(VkDevice _device, const VulkanComputePipelineCreateInfo& createInfo);
    void Destroy();

    VkPipeline GetPipeline() const { return pipeline; }
    VkPipelineLayout GetPipelineLayout() const { return pipelineLayout; }

private:
    VkDevice device = VK_NULL_HANDLE;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
};
