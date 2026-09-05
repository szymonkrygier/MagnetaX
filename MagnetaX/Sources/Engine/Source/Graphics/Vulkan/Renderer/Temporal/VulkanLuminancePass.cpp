// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#include "VulkanLuminancePass.h"

#if MX_GRAPHICS_VULKAN
#include <MX/Generated/Shaders/Common/VulkanShaderFullscreenVert.h>
#include <MX/Generated/Shaders/Temporal/VulkanShaderLuminanceFrag.h>
#include <Graphics/Vulkan/VulkanDevice.h>
#include <Graphics/Vulkan/VulkanInitializers.h>
#include <Graphics/Vulkan/Resources/VulkanImage.h>

bool VulkanLuminancePass::Create(const VulkanLuminancePassCreateInfo& createInfo)
{
    if (!createInfo.device || !createInfo.srcImage || createInfo.outFormat == VK_FORMAT_UNDEFINED) return false;
    if (!createInfo.srcImage->GetImageView()) return false;

    const VkDevice buffDevice = createInfo.device->GetDevice();
    if (!buffDevice) return false;

    Destroy();

    device = buffDevice;

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    const VkDescriptorSetLayoutCreateInfo layoutInfo = VulkanInitializers::DescriptorSetLayoutCreateInfo(1, &binding);

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descSetLayout) != VK_SUCCESS)
    {
        Destroy();
        return false;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.pNext = nullptr;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
    {
        Destroy();
        return false;
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    const VkDescriptorPoolCreateInfo poolInfo = VulkanInitializers::DescriptorPoolCreateInfo(1, 1, &poolSize);

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descPool) != VK_SUCCESS)
    {
        Destroy();
        return false;
    }

    const VkDescriptorSetAllocateInfo allocInfo = VulkanInitializers::DescriptorSetAllocateInfo(descPool, 1, &descSetLayout);

    if (vkAllocateDescriptorSets(device, &allocInfo, &descSet) != VK_SUCCESS)
    {
        Destroy();
        return false;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = sampler;
    imageInfo.imageView = createInfo.srcImage->GetImageView();
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet writeSet{};
    writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeSet.dstSet = descSet;
    writeSet.dstBinding = 0;
    writeSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeSet.descriptorCount = 1;
    writeSet.pImageInfo = &imageInfo;
    writeSet.pNext = nullptr;

    vkUpdateDescriptorSets(device, 1, &writeSet, 0, nullptr);

    VulkanPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertexShader = MX_GRAPHICS_VULKAN_SHADER_FULLSCREEN_VERT;
    pipelineInfo.vertexShaderSize = MX_GRAPHICS_VULKAN_SHADER_FULLSCREEN_VERT_SIZE;
    pipelineInfo.fragmentShader = MX_GRAPHICS_VULKAN_SHADER_LUMINANCE_FRAG;
    pipelineInfo.fragmentShaderSize = MX_GRAPHICS_VULKAN_SHADER_LUMINANCE_FRAG_SIZE;
    pipelineInfo.colorFormats = &createInfo.outFormat;
    pipelineInfo.colorFormatCount = 1;
    pipelineInfo.descriptorSetLayouts = &descSetLayout;
    pipelineInfo.descriptorSetLayoutCount = 1;

    if (!pipeline.Create(device, pipelineInfo))
    {
        Destroy();
        return false;
    }

    return true;
}

void VulkanLuminancePass::Destroy()
{
    pipeline.Destroy();

    if (device)
    {
        if (descPool) vkDestroyDescriptorPool(device, descPool, nullptr);
        if (sampler) vkDestroySampler(device, sampler, nullptr);
        if (descSetLayout) vkDestroyDescriptorSetLayout(device, descSetLayout, nullptr);
    }

    descSet = VK_NULL_HANDLE;
    descPool = VK_NULL_HANDLE;
    descSetLayout = VK_NULL_HANDLE;
    sampler = VK_NULL_HANDLE;

    device = VK_NULL_HANDLE;
}

void VulkanLuminancePass::Record(const VulkanLuminancePassRenderInfo& renderInfo)
{
    if (!renderInfo.cmdBuffer || !renderInfo.targetView || !renderInfo.srcView) return;
    if (renderInfo.extent.width == 0 || renderInfo.extent.height == 0) return;

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = sampler;
    imageInfo.imageView = renderInfo.srcView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet writeSet{};
    writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeSet.dstSet = descSet;
    writeSet.dstBinding = 0;
    writeSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeSet.descriptorCount = 1;
    writeSet.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device, 1, &writeSet, 0, nullptr);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = renderInfo.targetView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    colorAttachment.pNext = nullptr;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = { { 0, 0 }, renderInfo.extent };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pNext = nullptr;

    vkCmdBeginRendering(renderInfo.cmdBuffer, &renderingInfo);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float32)renderInfo.extent.width;
    viewport.height = (float32)renderInfo.extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = renderInfo.extent;

    vkCmdSetViewport(renderInfo.cmdBuffer, 0, 1, &viewport);
    vkCmdSetScissor(renderInfo.cmdBuffer, 0, 1, &scissor);

    vkCmdBindPipeline(renderInfo.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetPipeline());
    vkCmdBindDescriptorSets(renderInfo.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetPipelineLayout(), 0, 1, &descSet, 0, nullptr);
    vkCmdDraw(renderInfo.cmdBuffer, 3, 1, 0, 0);

    vkCmdEndRendering(renderInfo.cmdBuffer);
}
#endif
