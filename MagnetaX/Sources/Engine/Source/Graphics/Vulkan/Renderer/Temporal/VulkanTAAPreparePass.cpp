// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#include "VulkanTAAPreparePass.h"

#if MX_GRAPHICS_VULKAN
#include <MX/Generated/Shaders/Temporal/VulkanShaderPrepareComp.h>
#include <Graphics/Vulkan/VulkanDevice.h>
#include <Graphics/Vulkan/VulkanInitializers.h>
#include <Graphics/Vulkan/Resources/VulkanImage.h>

namespace
{
    struct TAAPreparePushConstants
    {
        Vector2f jitterUV;
        Vector2f prevJitterUV;
    };
}

bool VulkanTAAPreparePass::Create(const VulkanTAAPreparePassCreateInfo& createInfo)
{
    if (!createInfo.device || !createInfo.depthImage || !createInfo.velocityImage) return false;
    if (!createInfo.dilatedDepthImage || !createInfo.dilatedVelocityImage) return false;
    if (!createInfo.depthImage->GetImageView() || !createInfo.velocityImage->GetImageView()) return false;
    if (!createInfo.dilatedDepthImage->GetImageView() || !createInfo.dilatedVelocityImage->GetImageView()) return false;
    if (!createInfo.reconstructedPrevDepthImage) return false;
    if (!createInfo.reconstructedPrevDepthImage->GetImageView()) return false;

    const VkDevice buffDevice = createInfo.device->GetDevice();
    if (!buffDevice) return false;

    Destroy();

    device = buffDevice;

    VkDescriptorSetLayoutBinding bindings[5]{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    const VkDescriptorSetLayoutCreateInfo layoutInfo = VulkanInitializers::DescriptorSetLayoutCreateInfo(5, bindings);

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

    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
    {
        Destroy();
        return false;
    }

    VkDescriptorPoolSize poolSizes[2]{};

    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 2;

    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 3;

    const VkDescriptorPoolCreateInfo poolInfo = VulkanInitializers::DescriptorPoolCreateInfo(1, 2, poolSizes);

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

    VkDescriptorImageInfo imageInfos[5]{};

    imageInfos[0].sampler = sampler;
    imageInfos[0].imageView = createInfo.depthImage->GetImageView();
    imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    imageInfos[1].sampler = sampler;
    imageInfos[1].imageView = createInfo.velocityImage->GetImageView();
    imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageInfos[2].imageView = createInfo.dilatedDepthImage->GetImageView();
    imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    imageInfos[3].imageView = createInfo.dilatedVelocityImage->GetImageView();
    imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    imageInfos[4].imageView = createInfo.reconstructedPrevDepthImage->GetImageView();
    imageInfos[4].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet writes[5]{};

    for (uint32 i = 0; i < 5; ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descSet;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].pImageInfo = &imageInfos[i];
    }

    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

    vkUpdateDescriptorSets(device, 5, writes, 0, nullptr);

    VkPushConstantRange pushConstRange{};
    pushConstRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstRange.offset = 0;
    pushConstRange.size = sizeof(TAAPreparePushConstants);

    VulkanComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.computeShader = MX_GRAPHICS_VULKAN_SHADER_PREPARE_COMP;
    pipelineInfo.computeShaderSize = MX_GRAPHICS_VULKAN_SHADER_PREPARE_COMP_SIZE;
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

void VulkanTAAPreparePass::Destroy()
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

void VulkanTAAPreparePass::Record(const VulkanTAAPreparePassRenderInfo& renderInfo)
{
    if (!renderInfo.cmdBuffer) return;
    if (renderInfo.extent.width == 0 || renderInfo.extent.height == 0) return;

    vkCmdBindPipeline(renderInfo.cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.GetPipeline());

    TAAPreparePushConstants pushConstants{};
    pushConstants.jitterUV = renderInfo.jitterUV;
    pushConstants.prevJitterUV = renderInfo.prevJitterUV;

    vkCmdPushConstants(renderInfo.cmdBuffer, pipeline.GetPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TAAPreparePushConstants), &pushConstants);

    vkCmdBindDescriptorSets(renderInfo.cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        pipeline.GetPipelineLayout(), 0, 1, &descSet, 0, nullptr);

    const uint32 groupCountX = (renderInfo.extent.width + 7u) / 8u;
    const uint32 groupCountY = (renderInfo.extent.height + 7u) / 8u;

    vkCmdDispatch(renderInfo.cmdBuffer, groupCountX, groupCountY, 1);
}
#endif
