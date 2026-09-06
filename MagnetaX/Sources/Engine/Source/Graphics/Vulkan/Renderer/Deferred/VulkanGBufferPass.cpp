// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#include "VulkanGBufferPass.h"

#if MX_GRAPHICS_VULKAN
#include <MX/Generated/Shaders/Deferred/VulkanShaderBaseVert.h>
#include <MX/Generated/Shaders/Deferred/VulkanShaderBaseFrag.h>
#include <MX/Graphics/Resources/MeshVertex.h>
#include <Graphics/Vulkan/Resources/VulkanMaterial.h>
#include <Graphics/Vulkan/Resources/VulkanMesh.h>
#include <Graphics/Vulkan/VulkanDevice.h>
#include <Graphics/Vulkan/VulkanInitializers.h>
#include <Graphics/Renderer/Scene/RenderViewData.h>
#include <cstddef>

namespace
{
    struct GBufferFrameData
    {
        Matrix4f viewProj;
        Matrix4f jitteredViewProj;
        Matrix4f prevViewProj;
    };

    struct GBufferPushConst
    {
        Matrix4f model;
        Matrix4f prevModel;
    };

    VkRenderingAttachmentInfo CreateColorAttachment(VkImageView imageView)
    {
        VkRenderingAttachmentInfo attachmentInfo{};
        attachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        attachmentInfo.imageView = imageView;
        attachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachmentInfo.clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

        return attachmentInfo;
    }
}

bool VulkanGBufferPass::Create(const VulkanGBufferPassCreateInfo& createInfo)
{
    if (!createInfo.device || !createInfo.materialDescSetLayout) return false;
    if (createInfo.extent.width == 0 || createInfo.extent.height == 0) return false;

    const VkDevice buffDevice = createInfo.device->GetDevice();
    if (!buffDevice) return false;

    Destroy();

    device = buffDevice;

    VulkanGBufferCreateInfo gBufferInfo{};
    gBufferInfo.device = createInfo.device;
    gBufferInfo.extent = createInfo.extent;
    gBufferInfo.createVelocity = createInfo.velocityEnabled;
    gBufferInfo.sampleCount = createInfo.sampleCount;

    if (!gBuffer.Create(gBufferInfo))
    {
        Destroy();
        return false;
    }

    VkDescriptorSetLayoutBinding frameBinding{};
    frameBinding.binding = 0;
    frameBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    frameBinding.descriptorCount = 1;
    frameBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    const VkDescriptorSetLayoutCreateInfo frameLayoutInfo = VulkanInitializers::DescriptorSetLayoutCreateInfo(1, &frameBinding);

    if (vkCreateDescriptorSetLayout(device, &frameLayoutInfo, nullptr, &frameDescSetLayout) != VK_SUCCESS)
    {
        Destroy();
        return false;
    }

    const VkMemoryPropertyFlags memoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    if (!frameDataBuffer.Create(createInfo.device, sizeof(GBufferFrameData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, memoryProps))
    {
        Destroy();
        return false;
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;

    const VkDescriptorPoolCreateInfo poolInfo = VulkanInitializers::DescriptorPoolCreateInfo(1, 1, &poolSize);

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &frameDescPool) != VK_SUCCESS)
    {
        Destroy();
        return false;
    }

    const VkDescriptorSetAllocateInfo allocInfo = VulkanInitializers::DescriptorSetAllocateInfo(frameDescPool, 1, &frameDescSetLayout);

    if (vkAllocateDescriptorSets(device, &allocInfo, &frameDescSet) != VK_SUCCESS)
    {
        Destroy();
        return false;
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = frameDataBuffer.GetBuffer();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(GBufferFrameData);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = frameDescSet;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    VkVertexInputBindingDescription vertexBinding{};
    vertexBinding.binding = 0;
    vertexBinding.stride = sizeof(MeshVertex);
    vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertexAttribs[3]{};

    vertexAttribs[0].location = 0;
    vertexAttribs[0].binding = 0;
    vertexAttribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexAttribs[0].offset = offsetof(MeshVertex, position);

    vertexAttribs[1].location = 1;
    vertexAttribs[1].binding = 0;
    vertexAttribs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexAttribs[1].offset = offsetof(MeshVertex, normal);

    vertexAttribs[2].location = 2;
    vertexAttribs[2].binding = 0;
    vertexAttribs[2].format = VK_FORMAT_R32G32_SFLOAT;
    vertexAttribs[2].offset = offsetof(MeshVertex, uv);

    VkPushConstantRange pushConstRange{};
    pushConstRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstRange.offset = 0;
    pushConstRange.size = sizeof(GBufferPushConst);

    const VkFormat colorFormats[] =
    {
        gBuffer.GetAlbedoImage().GetFormat(),
        gBuffer.GetNormalImage().GetFormat(),
        gBuffer.GetMaterialImage().GetFormat(),
        createInfo.velocityEnabled ? gBuffer.GetVelocityImage().GetFormat() : VK_FORMAT_UNDEFINED
    };

    const VkDescriptorSetLayout descSetLayouts[] =
    {
        createInfo.materialDescSetLayout,
        frameDescSetLayout
    };

    VulkanPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertexShader = MX_GRAPHICS_VULKAN_SHADER_BASE_VERT;
    pipelineInfo.vertexShaderSize = MX_GRAPHICS_VULKAN_SHADER_BASE_VERT_SIZE;
    pipelineInfo.fragmentShader = MX_GRAPHICS_VULKAN_SHADER_BASE_FRAG;
    pipelineInfo.fragmentShaderSize = MX_GRAPHICS_VULKAN_SHADER_BASE_FRAG_SIZE;
    pipelineInfo.vertexBindings = &vertexBinding;
    pipelineInfo.vertexBindingCount = 1;
    pipelineInfo.vertexAttributes = vertexAttribs;
    pipelineInfo.vertexAttributeCount = 3;
    pipelineInfo.colorFormats = colorFormats;
    pipelineInfo.colorFormatCount = 4;
    pipelineInfo.depthFormat = gBuffer.GetDepthImage().GetFormat();
    pipelineInfo.descriptorSetLayouts = descSetLayouts;
    pipelineInfo.descriptorSetLayoutCount = 2;
    pipelineInfo.pushConstantRanges = &pushConstRange;
    pipelineInfo.pushConstantRangeCount = 1;
    pipelineInfo.depthTest = true;
    pipelineInfo.depthWrite = true;
    pipelineInfo.depthCompareOp = VK_COMPARE_OP_LESS;
    pipelineInfo.cullMode = VK_CULL_MODE_BACK_BIT;
    pipelineInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    pipelineInfo.sampleCount = createInfo.sampleCount;

    if (!pipeline.Create(buffDevice, pipelineInfo))
    {
        Destroy();
        return false;
    }

    return true;
}

void VulkanGBufferPass::Destroy()
{
    pipeline.Destroy();

    if (device)
    {
        if (frameDescPool) vkDestroyDescriptorPool(device, frameDescPool, nullptr);
        if (frameDescSetLayout) vkDestroyDescriptorSetLayout(device, frameDescSetLayout, nullptr);
    }

    frameDescSet = VK_NULL_HANDLE;
    frameDescPool = VK_NULL_HANDLE;
    frameDescSetLayout = VK_NULL_HANDLE;

    frameDataBuffer.Destroy();

    gBuffer.Destroy();

    device = VK_NULL_HANDLE;
}

void VulkanGBufferPass::Record(const VulkanGBufferPassRenderInfo& renderInfo)
{
    if (!renderInfo.cmdBuffer) return;
    if (!renderInfo.viewData) return;

    const VkExtent2D extent = gBuffer.GetExtent();
    if (extent.width == 0 || extent.height == 0) return;

    GBufferFrameData frameData{};
    frameData.viewProj = renderInfo.viewData->viewProj.Transposed();
    frameData.jitteredViewProj = renderInfo.viewData->jitteredViewProj.Transposed();
    frameData.prevViewProj = renderInfo.prevViewProj.Transposed();

    if (!frameDataBuffer.Upload(&frameData, sizeof(GBufferFrameData))) return;

    bool hasVelocity = gBuffer.GetVelocityImage().GetImageView() != VK_NULL_HANDLE;

    VkImageMemoryBarrier2 attachmentBarriers[5]{};
    uint32 attachmentsCount = 0;

    // Albedo
    attachmentBarriers[attachmentsCount++] = VulkanInitializers::ImageMemoryBarrier(gBuffer.GetAlbedoImage().GetImage(), VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    // Normal
    attachmentBarriers[attachmentsCount++] = VulkanInitializers::ImageMemoryBarrier(gBuffer.GetNormalImage().GetImage(), VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    // Material
    attachmentBarriers[attachmentsCount++] = VulkanInitializers::ImageMemoryBarrier(gBuffer.GetMaterialImage().GetImage(), VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    // Velocity
    if (hasVelocity)
    {
        attachmentBarriers[attachmentsCount++] = VulkanInitializers::ImageMemoryBarrier(gBuffer.GetVelocityImage().GetImage(), VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    }

    // Depth
    attachmentBarriers[attachmentsCount++] = VulkanInitializers::ImageMemoryBarrier(gBuffer.GetDepthImage().GetImage(), VK_IMAGE_ASPECT_DEPTH_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.imageMemoryBarrierCount = attachmentsCount;
    dependencyInfo.pImageMemoryBarriers = attachmentBarriers;

    vkCmdPipelineBarrier2(renderInfo.cmdBuffer, &dependencyInfo);

    VkRenderingAttachmentInfo colorAttachments[] =
    {
        CreateColorAttachment(gBuffer.GetAlbedoImage().GetImageView()),
        CreateColorAttachment(gBuffer.GetNormalImage().GetImageView()),
        CreateColorAttachment(gBuffer.GetMaterialImage().GetImageView()),
        CreateColorAttachment(hasVelocity ? gBuffer.GetVelocityImage().GetImageView() : VK_NULL_HANDLE)
    };

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = gBuffer.GetDepthImage().GetImageView();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = { { 0, 0 }, extent };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 4;
    renderingInfo.pColorAttachments = colorAttachments;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(renderInfo.cmdBuffer, &renderingInfo);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float32)extent.width;
    viewport.height = (float32)extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = extent;

    vkCmdSetViewport(renderInfo.cmdBuffer, 0, 1, &viewport);
    vkCmdSetScissor(renderInfo.cmdBuffer, 0, 1, &scissor);
    vkCmdBindPipeline(renderInfo.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetPipeline());

    vkCmdBindDescriptorSets(renderInfo.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetPipelineLayout(), 1, 1, &frameDescSet, 0, nullptr);

    for (usize i = 0; i < renderInfo.drawItems.size(); ++i)
    {
        const VulkanDrawItem& drawItem = renderInfo.drawItems[i];

        if (!drawItem.mesh || !drawItem.material) continue;

        const VkBuffer vertexBuffer = drawItem.mesh->GetVertexBuffer();
        const VkDeviceSize vertexOffset = 0;

        vkCmdBindVertexBuffers(renderInfo.cmdBuffer, 0, 1, &vertexBuffer, &vertexOffset);
        vkCmdBindIndexBuffer(renderInfo.cmdBuffer, drawItem.mesh->GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

        const VkDescriptorSet materialDescSet = drawItem.material->GetDescriptorSet();

        vkCmdBindDescriptorSets(renderInfo.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetPipelineLayout(),
            0, 1, &materialDescSet, 0, nullptr);

        Matrix4f prevModel = drawItem.model;
        if (i < renderInfo.prevModels.size()) prevModel = renderInfo.prevModels[i];

        GBufferPushConst pushConst{};
        pushConst.model = drawItem.model.Transposed();
        pushConst.prevModel = prevModel.Transposed();

        vkCmdPushConstants(renderInfo.cmdBuffer, pipeline.GetPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT,
            0, sizeof(GBufferPushConst), &pushConst);

        vkCmdDrawIndexed(renderInfo.cmdBuffer, drawItem.mesh->GetIndexCount(), 1, 0, 0, 0);
    }

    vkCmdEndRendering(renderInfo.cmdBuffer);

    VkImageMemoryBarrier2 shaderReadBarriers[5]{};
    uint32 readBarriersCount = 0;

    // Albedo
    shaderReadBarriers[readBarriersCount++] = VulkanInitializers::ImageMemoryBarrier(gBuffer.GetAlbedoImage().GetImage(), VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    // Normal
    shaderReadBarriers[readBarriersCount++] = VulkanInitializers::ImageMemoryBarrier(gBuffer.GetNormalImage().GetImage(), VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    // Material
    shaderReadBarriers[readBarriersCount++] = VulkanInitializers::ImageMemoryBarrier(gBuffer.GetMaterialImage().GetImage(), VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    // Velocity
    if (hasVelocity)
    {
        shaderReadBarriers[readBarriersCount++] = VulkanInitializers::ImageMemoryBarrier(gBuffer.GetVelocityImage().GetImage(), VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }

    // Depth
    shaderReadBarriers[readBarriersCount++] = VulkanInitializers::ImageMemoryBarrier(gBuffer.GetDepthImage().GetImage(), VK_IMAGE_ASPECT_DEPTH_BIT,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    dependencyInfo.imageMemoryBarrierCount = readBarriersCount;
    dependencyInfo.pImageMemoryBarriers = shaderReadBarriers;

    vkCmdPipelineBarrier2(renderInfo.cmdBuffer, &dependencyInfo);
}
#endif
