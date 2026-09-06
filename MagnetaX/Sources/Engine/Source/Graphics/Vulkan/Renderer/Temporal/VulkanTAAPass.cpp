// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#include "VulkanTAAPass.h"

#if MX_GRAPHICS_VULKAN
#include <MX/Generated/Shaders/Common/VulkanShaderFullscreenVert.h>
#include <MX/Generated/Shaders/Temporal/VulkanShaderTAAFrag.h>
#include <Graphics/Vulkan/VulkanDevice.h>
#include <Graphics/Vulkan/VulkanInitializers.h>
#include <Graphics/Vulkan/Resources/VulkanImage.h>

namespace
{
    struct TAAPushConstants
    {
        Vector2f jitterUV;
        Vector2f prevJitterUV;
        float32 feedbackMin;
        float32 feedbackMax;
        uint32 historyValid;
    };
}

bool VulkanTAAPass::Create(const VulkanTAAPassCreateInfo& createInfo)
{
    if (!createInfo.device || !createInfo.currentColor || !createInfo.velocityImage || !createInfo.depthImage) return false;
    if (!createInfo.currentColor->GetImageView()) return false;
    if (!createInfo.velocityImage->GetImageView() || !createInfo.depthImage->GetImageView()) return false;
    if (createInfo.outFormat == VK_FORMAT_UNDEFINED) return false;

    const VkDevice buffDevice = createInfo.device->GetDevice();
    if (!buffDevice) return false;

    Destroy();

    device = buffDevice;

    VkDescriptorSetLayoutBinding bindings[5]{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    const VkDescriptorSetLayoutCreateInfo layoutInfo = VulkanInitializers::DescriptorSetLayoutCreateInfo(5, bindings);

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descSetLayout) != VK_SUCCESS)
    {
        Destroy();
        return false;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
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
    poolSize.descriptorCount = 5;

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

    VkDescriptorImageInfo imageInfos[4]{};

    imageInfos[0].sampler = sampler;
    imageInfos[0].imageView = createInfo.currentColor->GetImageView();
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageInfos[1].sampler = sampler;
    imageInfos[1].imageView = createInfo.currentColor->GetImageView();
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageInfos[2].sampler = sampler;
    imageInfos[2].imageView = createInfo.velocityImage->GetImageView();
    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageInfos[3].sampler = sampler;
    imageInfos[3].imageView = createInfo.depthImage->GetImageView();
    imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet writeSets[4]{};

    writeSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeSets[0].dstSet = descSet;
    writeSets[0].dstBinding = 0;
    writeSets[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeSets[0].descriptorCount = 1;
    writeSets[0].pImageInfo = &imageInfos[0];

    writeSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeSets[1].dstSet = descSet;
    writeSets[1].dstBinding = 1;
    writeSets[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeSets[1].descriptorCount = 1;
    writeSets[1].pImageInfo = &imageInfos[1];

    writeSets[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeSets[2].dstSet = descSet;
    writeSets[2].dstBinding = 2;
    writeSets[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeSets[2].descriptorCount = 1;
    writeSets[2].pImageInfo = &imageInfos[2];

    writeSets[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeSets[3].dstSet = descSet;
    writeSets[3].dstBinding = 3;
    writeSets[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeSets[3].descriptorCount = 1;
    writeSets[3].pImageInfo = &imageInfos[3];

    vkUpdateDescriptorSets(device, 4, writeSets, 0, nullptr);

    VkPushConstantRange pushConstRange{};
    pushConstRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstRange.offset = 0;
    pushConstRange.size = sizeof(TAAPushConstants);

    VulkanPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertexShader = MX_GRAPHICS_VULKAN_SHADER_FULLSCREEN_VERT;
    pipelineInfo.vertexShaderSize = MX_GRAPHICS_VULKAN_SHADER_FULLSCREEN_VERT_SIZE;
    pipelineInfo.fragmentShader = MX_GRAPHICS_VULKAN_SHADER_TAA_FRAG;
    pipelineInfo.fragmentShaderSize = MX_GRAPHICS_VULKAN_SHADER_TAA_FRAG_SIZE;
    pipelineInfo.colorFormats = &createInfo.outFormat;
    pipelineInfo.colorFormatCount = 1;
    pipelineInfo.descriptorSetLayouts = &descSetLayout;
    pipelineInfo.descriptorSetLayoutCount = 1;
    pipelineInfo.pushConstantRanges = &pushConstRange;
    pipelineInfo.pushConstantRangeCount = 1;

    if (!pipeline.Create(device, pipelineInfo))
    {
        Destroy();
        return false;
    }

    return true;
}

void VulkanTAAPass::Destroy()
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

void VulkanTAAPass::Record(const VulkanTAAPassRenderInfo& renderInfo)
{
    if (!renderInfo.cmdBuffer || !renderInfo.historyView || !renderInfo.targetView) return;
    if (renderInfo.extent.width == 0 || renderInfo.extent.height == 0) return;
    if (!renderInfo.previousDepthView) return;

    VkDescriptorImageInfo historyImageInfo{};
    historyImageInfo.sampler = sampler;
    historyImageInfo.imageView = renderInfo.historyView;
    historyImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo previousDepthImageInfo{};
    previousDepthImageInfo.sampler = sampler;
    previousDepthImageInfo.imageView = renderInfo.previousDepthView;
    previousDepthImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet dynamicWrites[2]{};

    dynamicWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    dynamicWrites[0].dstSet = descSet;
    dynamicWrites[0].dstBinding = 1;
    dynamicWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    dynamicWrites[0].descriptorCount = 1;
    dynamicWrites[0].pImageInfo = &historyImageInfo;

    dynamicWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    dynamicWrites[1].dstSet = descSet;
    dynamicWrites[1].dstBinding = 4;
    dynamicWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    dynamicWrites[1].descriptorCount = 1;
    dynamicWrites[1].pImageInfo = &previousDepthImageInfo;

    vkUpdateDescriptorSets(device, 2, dynamicWrites, 0, nullptr);

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

    TAAPushConstants pushConstants{};
    pushConstants.feedbackMin = renderInfo.feedbackMin;
    pushConstants.feedbackMax = renderInfo.feedbackMax;
    pushConstants.historyValid = renderInfo.historyValid ? 1u : 0u;
    pushConstants.jitterUV = renderInfo.jitterUV;
    pushConstants.prevJitterUV = renderInfo.prevJitterUV;

    vkCmdPushConstants(renderInfo.cmdBuffer, pipeline.GetPipelineLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TAAPushConstants), &pushConstants);
    vkCmdBindDescriptorSets(renderInfo.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetPipelineLayout(), 0, 1, &descSet, 0, nullptr);

    vkCmdDraw(renderInfo.cmdBuffer, 3, 1, 0, 0);

    vkCmdEndRendering(renderInfo.cmdBuffer);
}
#endif
