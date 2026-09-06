// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#include "VulkanTAA.h"

#if MX_GRAPHICS_VULKAN
#include <Graphics/Vulkan/VulkanInitializers.h>

namespace
{
    float32 Halton(uint32 index, uint32 base)
    {
        float32 result = 0.0f;
        float32 fraction = 1.0f;

        while (index > 0)
        {
            fraction /= (float32)base;
            result += fraction * (float32)(index % base);
            index /= base;
        }

        return result;
    }

    Vector2f CalculateJitter(uint64 frameIndex, VkExtent2D extent)
    {
        const uint32 sampleIndex = (uint32)((frameIndex - 1) % 16) + 1;

        const float32 jitterX = Halton(sampleIndex, 2) - 0.5f;
        const float32 jitterY = Halton(sampleIndex, 3) - 0.5f;

        return Vector2f(2.0f * jitterX / (float32)extent.width, 2.0f * jitterY / (float32)extent.height);
    }
}

bool VulkanTAA::Create(const VulkanTAACreateInfo& createInfo)
{
    if (!createInfo.device || !createInfo.currentColor || !createInfo.velocityImage || !createInfo.depthImage) return false;
    if (createInfo.extent.width == 0 || createInfo.extent.height == 0) return false;

    Destroy();

    extent = createInfo.extent;

    velocityImage = createInfo.velocityImage;
    depthImage = createInfo.depthImage;

    VulkanImageCreateInfo historyInfo{};
    historyInfo.device = createInfo.device;
    historyInfo.extent = createInfo.extent;
    historyInfo.format = ImageFormat::RGBA16_FLOAT;
    historyInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    for (VulkanImage& image : history)
    {
        if (!image.Create(historyInfo))
        {
            Destroy();
            return false;
        }
    }

    VulkanImageCreateInfo depthHistoryInfo{};
    depthHistoryInfo.device = createInfo.device;
    depthHistoryInfo.extent = createInfo.extent;
    depthHistoryInfo.format = ImageFormat::D32_FLOAT;
    depthHistoryInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    for (VulkanImage& image : depthHistory)
    {
        if (!image.Create(depthHistoryInfo))
        {
            Destroy();
            return false;
        }
    }

    VulkanCamVelocityPassCreateInfo camVelocityInfo{};
    camVelocityInfo.device = createInfo.device;
    camVelocityInfo.depthImage = createInfo.depthImage;
    camVelocityInfo.outFormat = createInfo.velocityImage->GetFormat();

    if (!camVelocityPass.Create(camVelocityInfo))
    {
        Destroy();
        return false;
    }

    VulkanTAAPassCreateInfo taaInfo{};
    taaInfo.device = createInfo.device;
    taaInfo.currentColor = createInfo.currentColor;
    taaInfo.outFormat = history[0].GetFormat();
    taaInfo.velocityImage = createInfo.velocityImage;
    taaInfo.depthImage = createInfo.depthImage;

    if (!taaPass.Create(taaInfo))
    {
        Destroy();
        return false;
    }

    return true;
}

void VulkanTAA::Destroy()
{
    taaPass.Destroy();
    camVelocityPass.Destroy();

    for (VulkanImage& image : depthHistory)
    {
        image.Destroy();
    }

    for (VulkanImage& image : history)
    {
        image.Destroy();
    }

    historyLayouts = { VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED };
    depthHistoryLayouts = { VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED };

    ResetHistory();

    velocityImage = nullptr;
    depthImage = nullptr;

    extent = {};
}

void VulkanTAA::RecordCameraVelocity(VkCommandBuffer cmdBuffer, const Matrix4f& invViewProj, const Matrix4f& prevViewProj)
{
    if (!cmdBuffer || !velocityImage) return;
    if (extent.width == 0 || extent.height == 0) return;

    const VkImageMemoryBarrier2 velocityWriteBarrier = VulkanInitializers::ImageMemoryBarrier(velocityImage->GetImage(), 
        VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &velocityWriteBarrier;

    vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);

    VulkanCamVelocityPassRenderInfo camVelocityInfo{};
    camVelocityInfo.cmdBuffer = cmdBuffer;
    camVelocityInfo.targetView = velocityImage->GetImageView();
    camVelocityInfo.extent = extent;
    camVelocityInfo.invViewProj = invViewProj;
    camVelocityInfo.prevViewProj = prevViewProj;

    camVelocityPass.Record(camVelocityInfo);

    const VkImageMemoryBarrier2 velocityReadBarrier = VulkanInitializers::ImageMemoryBarrier(velocityImage->GetImage(), 
        VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    dependencyInfo.pImageMemoryBarriers = &velocityReadBarrier;

    vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);
}

VkImageView VulkanTAA::Resolve(const VulkanTAAResolveInfo& resolveInfo)
{
    if (!resolveInfo.cmdBuffer || !depthImage) return VK_NULL_HANDLE;
    if (extent.width == 0 || extent.height == 0) return VK_NULL_HANDLE;

    const uint32 historyWriteIndex = 1u - historyReadIndex;

    VulkanImage& historyRead = history[historyReadIndex];
    VulkanImage& historyWrite = history[historyWriteIndex];

    VulkanImage& depthHistoryRead = depthHistory[historyReadIndex];
    VulkanImage& depthHistoryWrite = depthHistory[historyWriteIndex];

    VkImageLayout& historyReadLayout = historyLayouts[historyReadIndex];
    VkImageLayout& historyWriteLayout = historyLayouts[historyWriteIndex];
    VkImageLayout& depthHistoryWriteLayout = depthHistoryLayouts[historyWriteIndex];

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;

    VkPipelineStageFlags2 depthHistoryWriteSrcStage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 depthHistoryWriteSrcAccess = VK_ACCESS_2_NONE;

    if (depthHistoryWriteLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        depthHistoryWriteSrcStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        depthHistoryWriteSrcAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    }

    const VkImageMemoryBarrier2 depthCopyBarriers[2] =
    {
        VulkanInitializers::ImageMemoryBarrier(depthImage->GetImage(), VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, 
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT),

        VulkanInitializers::ImageMemoryBarrier(depthHistoryWrite.GetImage(), VK_IMAGE_ASPECT_DEPTH_BIT, depthHistoryWriteLayout, 
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, depthHistoryWriteSrcStage, depthHistoryWriteSrcAccess, VK_PIPELINE_STAGE_2_TRANSFER_BIT, 
            VK_ACCESS_2_TRANSFER_WRITE_BIT)
    };

    dependencyInfo.imageMemoryBarrierCount = 2;
    dependencyInfo.pImageMemoryBarriers = depthCopyBarriers;

    vkCmdPipelineBarrier2(resolveInfo.cmdBuffer, &dependencyInfo);

    VkImageCopy depthCopy{};
    depthCopy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthCopy.srcSubresource.mipLevel = 0;
    depthCopy.srcSubresource.baseArrayLayer = 0;
    depthCopy.srcSubresource.layerCount = 1;
    depthCopy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthCopy.dstSubresource.mipLevel = 0;
    depthCopy.dstSubresource.baseArrayLayer = 0;
    depthCopy.dstSubresource.layerCount = 1;
    depthCopy.extent = { extent.width, extent.height, 1 };

    vkCmdCopyImage(resolveInfo.cmdBuffer, depthImage->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, depthHistoryWrite.GetImage(), 
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &depthCopy);

    const VkImageMemoryBarrier2 depthReadBarriers[2] =
    {
        VulkanInitializers::ImageMemoryBarrier(depthImage->GetImage(), VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, 
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT),

        VulkanInitializers::ImageMemoryBarrier(depthHistoryWrite.GetImage(), VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, 
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT)
    };

    dependencyInfo.imageMemoryBarrierCount = 2;
    dependencyInfo.pImageMemoryBarriers = depthReadBarriers;

    vkCmdPipelineBarrier2(resolveInfo.cmdBuffer, &dependencyInfo);

    depthHistoryWriteLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    if (historyReadLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        const VkImageMemoryBarrier2 historyReadBarrier = VulkanInitializers::ImageMemoryBarrier(historyRead.GetImage(), VK_IMAGE_ASPECT_COLOR_BIT, 
            historyReadLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, 
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &historyReadBarrier;

        vkCmdPipelineBarrier2(resolveInfo.cmdBuffer, &dependencyInfo);

        historyReadLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkPipelineStageFlags2 historyWriteSrcStage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 historyWriteSrcAccess = VK_ACCESS_2_NONE;

    if (historyWriteLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        historyWriteSrcStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        historyWriteSrcAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    }

    const VkImageMemoryBarrier2 historyWriteBarrier = VulkanInitializers::ImageMemoryBarrier(historyWrite.GetImage(), VK_IMAGE_ASPECT_COLOR_BIT, 
        historyWriteLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, historyWriteSrcStage, historyWriteSrcAccess, 
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &historyWriteBarrier;

    vkCmdPipelineBarrier2(resolveInfo.cmdBuffer, &dependencyInfo);

    historyWriteLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VulkanTAAPassRenderInfo taaInfo{};
    taaInfo.cmdBuffer = resolveInfo.cmdBuffer;
    taaInfo.historyView = historyRead.GetImageView();
    taaInfo.targetView = historyWrite.GetImageView();
    taaInfo.previousDepthView = historyValid ? depthHistoryRead.GetImageView() : depthHistoryWrite.GetImageView();
    taaInfo.extent = extent;
    taaInfo.jitterUV = resolveInfo.jitterUV;
    taaInfo.prevJitterUV = prevJitterUV;
    taaInfo.feedbackMin = resolveInfo.feedbackMin;
    taaInfo.feedbackMax = resolveInfo.feedbackMax;
    taaInfo.historyValid = historyValid;

    taaPass.Record(taaInfo);

    const VkImageMemoryBarrier2 outputBarrier = VulkanInitializers::ImageMemoryBarrier(historyWrite.GetImage(), VK_IMAGE_ASPECT_COLOR_BIT, 
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    dependencyInfo.pImageMemoryBarriers = &outputBarrier;

    vkCmdPipelineBarrier2(resolveInfo.cmdBuffer, &dependencyInfo);

    historyWriteLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    historyReadIndex = historyWriteIndex;
    frameIndex++;
    historyValid = true;
    prevJitterUV = resolveInfo.jitterUV;

    return historyWrite.GetImageView();
}

void VulkanTAA::ResetHistory()
{
    historyReadIndex = 0;
    frameIndex = 0;
    historyValid = false;
    prevJitterUV = {};
}

Vector2f VulkanTAA::GetProjectionJitter() const
{
    if (!historyValid || frameIndex == 0) return Vector2f(0.0f);
    if (extent.width == 0 || extent.height == 0) return Vector2f(0.0f);

    return CalculateJitter(frameIndex, extent);
}
#endif
