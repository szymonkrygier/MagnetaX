// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#include "VulkanRenderer.h"

#if MX_GRAPHICS_VULKAN
#include <MX/Graphics/Renderer/UI/UIRenderData.h>
#include <Graphics/Renderer/Scene/RenderSceneData.h>
#include <Graphics/Renderer/Shadow/ShadowFrameBuilder.h>
#include <Graphics/Renderer/Shadow/ShadowFrameData.h>
#include <Graphics/Vulkan/Renderer/UI/VulkanUIRenderer.h>
#include "../Present/VulkanPresentContext.h"
#include "../VulkanDevice.h"
#include "../VulkanInitializers.h"
#include <array>
#include <span>
#include <cmath>

namespace
{
    bool HasProjectionChanged(const Matrix4f& a, const Matrix4f& b)
    {
        return a.m00 != b.m00 || a.m01 != b.m01 || a.m02 != b.m02 || a.m03 != b.m03 || a.m10 != b.m10 || a.m11 != b.m11 || a.m12 != b.m12 || a.m13 != b.m13 || a.m20 != b.m20 || a.m21 != b.m21 || a.m22 != b.m22 || a.m23 != b.m23 || a.m30 != b.m30 || a.m31 != b.m31 || a.m32 != b.m32 || a.m33 != b.m33;
    }
}

bool VulkanRenderer::Create(const VulkanRendererCreateInfo& createInfo)
{
    if (!createInfo.device || !createInfo.presentContext || !createInfo.materialDescSetLayout || !createInfo.uiRenderer) return false;

    const VkDevice buffDevice = createInfo.device->GetDevice();
    if (!buffDevice) return false;

    VulkanSwapchain& swapchain = createInfo.presentContext->GetSwapchain();

    const VkExtent2D extent = swapchain.GetExtent();
    const VkFormat swapchainFormat = swapchain.GetFormat();
    ImageFormat displayColorFormat = VulkanImageFormat::ToImageFormat(swapchainFormat);

    if (extent.width == 0 || extent.height == 0) return false;
    if (swapchainFormat == VK_FORMAT_UNDEFINED || swapchain.GetImages().empty()) return false;
    if (displayColorFormat != ImageFormat::BGRA8_SRGB && displayColorFormat != ImageFormat::RGBA8_SRGB) return false;
    if (createInfo.config.shadows.directional.resolution == 0 || createInfo.config.shadows.spot.resolution == 0) return false;

    if (createInfo.config.aa.mode == AAMode::TAA)
    {
        const TAAConfig& taa = createInfo.config.aa.taa;

        if (!std::isfinite(taa.feedbackMin) || !std::isfinite(taa.feedbackMax) || taa.feedbackMin < 0.0f || taa.feedbackMax > 1.0f || taa.feedbackMin > taa.feedbackMax) return false;
    }

    Destroy();

    device = createInfo.device;
    presentContext = createInfo.presentContext;
    config = createInfo.config;
    uiRenderer = createInfo.uiRenderer;

    if (!commandPool.Create(buffDevice, device->GetGraphicsQueueFamily()))
    {
        Destroy();
        return false;
    }

    cmdBuffer = commandPool.AllocateCommandBuffer();

    if (!cmdBuffer)
    {
        Destroy();
        return false;
    }

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreInfo.pNext = nullptr;

    if (vkCreateSemaphore(buffDevice, &semaphoreInfo, nullptr, &imageAvailable) != VK_SUCCESS)
    {
        Destroy();
        return false;
    }

    renderFinishedSemaphores.resize(swapchain.GetImages().size());

    for (VkSemaphore& semaphore : renderFinishedSemaphores)
    {
        if (vkCreateSemaphore(buffDevice, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS)
        {
            Destroy();
            return false;
        }
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    fenceInfo.pNext = nullptr;

    if (vkCreateFence(buffDevice, &fenceInfo, nullptr, &inFlight) != VK_SUCCESS)
    {
        Destroy();
        return false;
    }

    VulkanShadowDepthPassCreateInfo directionalShadowInfo{};
    directionalShadowInfo.device = device;
    directionalShadowInfo.extent = { config.shadows.directional.resolution, config.shadows.directional.resolution };
    directionalShadowInfo.layerCount = MX_GRAPHICS_DIRECTIONAL_SHADOW_CASCADE_COUNT;

    if (!directionalShadowPass.Create(directionalShadowInfo))
    {
        Destroy();
        return false;
    }

    VulkanShadowDepthPassCreateInfo spotShadowInfo{};
    spotShadowInfo.device = device;
    spotShadowInfo.extent = { config.shadows.spot.resolution, config.shadows.spot.resolution };
    spotShadowInfo.layerCount = 1;

    if (!spotShadowPass.Create(spotShadowInfo))
    {
        Destroy();
        return false;
    }

    VulkanGBufferPassCreateInfo gBufferInfo{};
    gBufferInfo.device = device;
    gBufferInfo.extent = extent;
    gBufferInfo.materialDescSetLayout = createInfo.materialDescSetLayout;
    gBufferInfo.velocityEnabled = config.aa.mode == AAMode::TAA;

    if (!gBufferPass.Create(gBufferInfo))
    {
        Destroy();
        return false;
    }

    const VkImageUsageFlags sceneColorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    VulkanImageCreateInfo sceneColorImgInfo{};
    sceneColorImgInfo.device = device;
    sceneColorImgInfo.extent = extent;
    sceneColorImgInfo.format = ImageFormat::RGBA16_FLOAT;
    sceneColorImgInfo.usage = sceneColorUsage;

    if (!sceneColor.Create(sceneColorImgInfo))
    {
        Destroy();
        return false;
    }

    if (config.aa.mode == AAMode::TAA)
    {
        VulkanTAACreateInfo taaInfo{};
        taaInfo.device = device;
        taaInfo.extent = extent;
        taaInfo.currentColor = &sceneColor;
        taaInfo.velocityImage = &gBufferPass.GetGBuffer().GetVelocityImage();
        taaInfo.depthImage = &gBufferPass.GetGBuffer().GetDepthImage();

        if (!taa.Create(taaInfo))
        {
            Destroy();
            return false;
        }
    }

    VulkanImageCreateInfo ldrColorInfo{};
    ldrColorInfo.device = device;
    ldrColorInfo.extent = extent;
    ldrColorInfo.format = ImageFormat::RGBA8_SRGB;
    ldrColorInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    if (!ldrColor.Create(ldrColorInfo))
    {
        Destroy();
        return false;
    }

    VulkanImageCreateInfo displayColorInfo{};
    displayColorInfo.device = device;
    displayColorInfo.extent = extent;
    displayColorInfo.format = displayColorFormat;
    displayColorInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    if (!displayColor.Create(displayColorInfo))
    {
        Destroy();
        return false;
    }

    VkSamplerCreateInfo displaySamplerInfo{};
    displaySamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    displaySamplerInfo.magFilter = VK_FILTER_LINEAR;
    displaySamplerInfo.minFilter = VK_FILTER_LINEAR;
    displaySamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    displaySamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    displaySamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    displaySamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    displaySamplerInfo.minLod = 0.0f;
    displaySamplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(buffDevice, &displaySamplerInfo, nullptr, &displayColorSampler) != VK_SUCCESS)
    {
        Destroy();
        return false;
    }

    displayColorUITexture = uiRenderer->RegisterExternalTexture(displayColor.GetImageView(), displayColorSampler);

    if (!displayColorUITexture)
    {
        Destroy();
        return false;
    }

    VulkanLightingPassCreateInfo lightingInfo{};
    lightingInfo.device = device;
    lightingInfo.gBuffer = &gBufferPass.GetGBuffer();
    lightingInfo.outFormat = sceneColor.GetFormat();
    lightingInfo.directionalShadowView = directionalShadowPass.GetShadowMapArrayView();
    lightingInfo.directionalShadowSampler = directionalShadowPass.GetSampler();
    lightingInfo.spotShadowView = spotShadowPass.GetShadowMapView();
    lightingInfo.spotShadowSampler = spotShadowPass.GetSampler();

    if (!lightingPass.Create(lightingInfo))
    {
        Destroy();
        return false;
    }

    VulkanSkyPassCreateInfo skyInfo{};
    skyInfo.device = device;
    skyInfo.gBuffer = &gBufferPass.GetGBuffer();
    skyInfo.outFormat = sceneColor.GetFormat();

    if (!skyPass.Create(skyInfo))
    {
        Destroy();
        return false;
    }

    VulkanGBufferDebugPassCreateInfo gBufferDebugInfo{};
    gBufferDebugInfo.device = device;
    gBufferDebugInfo.gBuffer = &gBufferPass.GetGBuffer();
    gBufferDebugInfo.outFormat = swapchainFormat;

    if (!gBufferDebugPass.Create(gBufferDebugInfo))
    {
        Destroy();
        return false;
    }

    VulkanToneMapPassCreateInfo toneMapInfo{};
    toneMapInfo.device = device;
    toneMapInfo.srcImage = config.aa.mode == AAMode::TAA ? &taa.GetHistoryImage(0) : &sceneColor;
    toneMapInfo.outFormat = ldrColor.GetFormat();

    if (!toneMapPass.Create(toneMapInfo))
    {
        Destroy();
        return false;
    }

    VulkanPostFXPassCreateInfo postFXInfo{};
    postFXInfo.device = device;
    postFXInfo.srcImage = &ldrColor;
    postFXInfo.outFormat = swapchainFormat;

    if (!postFXPass.Create(postFXInfo))
    {
        Destroy();
        return false;
    }

    VulkanUIPassCreateInfo uiInfo{};
    uiInfo.device = device;
    uiInfo.outFormat = swapchainFormat;
    uiInfo.uiRenderer = createInfo.uiRenderer;

    if (!uiPass.Create(uiInfo))
    {
        Destroy();
        return false;
    }

    swapchainImageLayouts.assign(swapchain.GetImages().size(), VK_IMAGE_LAYOUT_UNDEFINED);

    return true;
}

void VulkanRenderer::Destroy()
{
    if (device && device->GetDevice()) vkDeviceWaitIdle(device->GetDevice());

    if (uiRenderer && displayColorUITexture) uiRenderer->UnregisterExternalTexture(displayColorUITexture);
    displayColorUITexture = {};

    if (device && device->GetDevice() && displayColorSampler) vkDestroySampler(device->GetDevice(), displayColorSampler, nullptr);
    displayColorSampler = VK_NULL_HANDLE;

    displayColor.Destroy();
    displayColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    uiPass.Destroy();

    postFXPass.Destroy();
    toneMapPass.Destroy();

    taa.Destroy();

    gBufferDebugPass.Destroy();

    skyPass.Destroy();

    lightingPass.Destroy();

    ldrColor.Destroy();
    ldrColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    sceneColor.Destroy();
    sceneColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    gBufferPass.Destroy();

    spotShadowPass.Destroy();
    directionalShadowPass.Destroy();

    if (device)
    {
        const VkDevice buffDevice = device->GetDevice();

        if (buffDevice)
        {
            if (inFlight) vkDestroyFence(buffDevice, inFlight, nullptr);

            for (VkSemaphore semaphore : renderFinishedSemaphores)
            {
                if (semaphore) vkDestroySemaphore(buffDevice, semaphore, nullptr);
            }

            if (imageAvailable) vkDestroySemaphore(buffDevice, imageAvailable, nullptr);
        }
    }

    inFlight = VK_NULL_HANDLE;

    renderFinishedSemaphores.clear();

    imageAvailable = VK_NULL_HANDLE;

    cmdBuffer = VK_NULL_HANDLE;
    commandPool.Destroy();

    swapchainImageLayouts.clear();

    ResetTemporalHistory();

    config = {};
    presentContext = nullptr;
    uiRenderer = nullptr;
    device = nullptr;
}

VulkanFrameResult VulkanRenderer::DrawFrame(const VulkanRendererFrameInfo& frameInfo)
{
    if (!device || !presentContext || !cmdBuffer) return VulkanFrameResult::FAILED;
    if (!imageAvailable || !inFlight) return VulkanFrameResult::FAILED;
    if (!frameInfo.sceneData || !frameInfo.uiData) return VulkanFrameResult::FAILED;

    const RenderSceneData& sceneData = *frameInfo.sceneData;
    const UIRenderData& uiData = *frameInfo.uiData;
    const std::span<const VulkanDrawItem> drawItems = frameInfo.drawItems;
    const VulkanEnvironmentRenderData& env = frameInfo.environment;
    const bool hasEnvironment = env.environmentView && env.environmentSampler && sceneData.viewData.valid;

    if (frameInfo.resetTemporalHistory) ResetTemporalHistory();

    if (config.aa.mode == AAMode::TAA && debugView == GraphicsDebugView::FINAL && sceneData.viewData.valid && prevFrameValid)
    {
        const bool cameraChanged = sceneData.viewData.cameraId != prevCameraId;
        const bool projectionChanged = HasProjectionChanged(sceneData.viewData.proj, prevProj);

        if (cameraChanged || projectionChanged) ResetTemporalHistory();
    }

    bool displayColorUsedByUI = false;

    for (const UIDrawCommand& command : uiRenderer->GetDrawData().commands)
    {
        if (command.texture.id == displayColorUITexture.id)
        {
            displayColorUsedByUI = true;
            break;
        }
    }

    const VkDevice buffDevice = device->GetDevice();
    if (!buffDevice) return VulkanFrameResult::FAILED;

    VulkanSwapchain& swapchain = presentContext->GetSwapchain();

    if (swapchain.GetImages().empty()) return VulkanFrameResult::FAILED;
    if (swapchainImageLayouts.size() != swapchain.GetImages().size()) return VulkanFrameResult::FAILED;
    if (renderFinishedSemaphores.size() != swapchain.GetImages().size()) return VulkanFrameResult::FAILED;

    const VkExtent2D extent = swapchain.GetExtent();

    if (extent.width == 0 || extent.height == 0) return VulkanFrameResult::FAILED;

    if (vkWaitForFences(buffDevice, 1, &inFlight, VK_TRUE, UINT64_MAX) != VK_SUCCESS) return VulkanFrameResult::FAILED;

    uint32 imageIndex = 0;

    const VkResult acquireResult = vkAcquireNextImageKHR(
        buffDevice, swapchain.GetSwapchain(), UINT64_MAX, imageAvailable, VK_NULL_HANDLE, &imageIndex
    );

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) return VulkanFrameResult::RECREATE;
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) return VulkanFrameResult::FAILED;
    if (imageIndex >= swapchain.GetImages().size()) return VulkanFrameResult::FAILED;

    const bool suboptimal = acquireResult == VK_SUBOPTIMAL_KHR;

    const VulkanImage& swapchainImage = swapchain.GetImages()[imageIndex];
    const VkSemaphore renderFinished = renderFinishedSemaphores[imageIndex];

    if (vkResetCommandBuffer(cmdBuffer, 0) != VK_SUCCESS) return VulkanFrameResult::FAILED;

    const VkCommandBufferBeginInfo beginInfo = VulkanInitializers::CommandBufferBeginInfo();

    if (vkBeginCommandBuffer(cmdBuffer, &beginInfo) != VK_SUCCESS) return VulkanFrameResult::FAILED;

    const ShadowFrameData shadowData = BuildShadowFrameData(sceneData, config.shadows);

    VulkanShadowDepthPassRenderInfo directionalShadowInfo{};
    directionalShadowInfo.cmdBuffer = cmdBuffer;
    directionalShadowInfo.drawItems = shadowData.directional.lightIndex != MX_GRAPHICS_INVALID_SHADOW_LIGHT_INDEX ?
        drawItems : std::span<const VulkanDrawItem>{};
    directionalShadowInfo.lightViewProjs = shadowData.directional.viewProjs;

    directionalShadowPass.Record(directionalShadowInfo);

    const std::array<Matrix4f, 1> spotViewProjs{ shadowData.spot.viewProj };

    VulkanShadowDepthPassRenderInfo spotShadowInfo{};
    spotShadowInfo.cmdBuffer = cmdBuffer;
    spotShadowInfo.drawItems = shadowData.spot.lightIndex != MX_GRAPHICS_INVALID_SHADOW_LIGHT_INDEX ?
        drawItems : std::span<const VulkanDrawItem>{};
    spotShadowInfo.lightViewProjs = spotViewProjs;

    spotShadowPass.Record(spotShadowInfo);

    framePrevModels.clear();

    if (config.aa.mode == AAMode::TAA)
    {
        framePrevModels.resize(drawItems.size());

        for (usize i = 0; i < drawItems.size(); ++i)
        {
            const VulkanDrawItem& drawItem = drawItems[i];

            Matrix4f prevModel = drawItem.model;

            if (prevFrameValid && drawItem.id != 0)
            {
                const auto it = prevObjectModels.find(drawItem.id);
                if (it != prevObjectModels.end()) prevModel = it->second;
            }

            framePrevModels[i] = prevModel;
        }
    }

    VulkanGBufferPassRenderInfo gBufferInfo{};
    gBufferInfo.cmdBuffer = cmdBuffer;
    gBufferInfo.drawItems = drawItems;
    gBufferInfo.prevModels = framePrevModels;
    gBufferInfo.viewData = &sceneData.viewData;
    gBufferInfo.prevViewProj = prevFrameValid ? prevViewProj : sceneData.viewData.viewProj;

    gBufferPass.Record(gBufferInfo);

    if (config.aa.mode == AAMode::TAA && sceneData.viewData.valid)
    {
        taa.RecordCameraVelocity(cmdBuffer, sceneData.viewData.invViewProj, prevFrameValid ? prevViewProj : sceneData.viewData.viewProj);
    }

    VkImageLayout& swapchainLayout = swapchainImageLayouts[imageIndex];

    const VulkanImage& displayTarget = displayColorUsedByUI ? displayColor : swapchainImage;
    VkImageLayout& displayTargetLayout = displayColorUsedByUI ? displayColorLayout : swapchainLayout;

    VkPipelineStageFlags2 displayTargetSrcStage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 displayTargetSrcAccess = VK_ACCESS_2_NONE;

    if (displayTargetLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        displayTargetSrcStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        displayTargetSrcAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    }

    const VkImageMemoryBarrier2 displayTargetWriteBarrier = VulkanInitializers::ImageMemoryBarrier(displayTarget.GetImage(),
        VK_IMAGE_ASPECT_COLOR_BIT, displayTargetLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, displayTargetSrcStage,
        displayTargetSrcAccess, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &displayTargetWriteBarrier;
    dependencyInfo.pNext = nullptr;

    vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);

    displayTargetLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    if (debugView == GraphicsDebugView::FINAL)
    {
        VkPipelineStageFlags2 sceneColorSrcStage = VK_PIPELINE_STAGE_2_NONE;
        VkAccessFlags2 sceneColorSrcAccess = VK_ACCESS_2_NONE;

        if (sceneColorLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            sceneColorSrcStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            sceneColorSrcAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        }

        const VkImageMemoryBarrier2 sceneColorWriteBarrier = VulkanInitializers::ImageMemoryBarrier(
            sceneColor.GetImage(), VK_IMAGE_ASPECT_COLOR_BIT, sceneColorLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            sceneColorSrcStage, sceneColorSrcAccess, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

        dependencyInfo.pImageMemoryBarriers = &sceneColorWriteBarrier;

        vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);

        sceneColorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VulkanLightingPassRenderInfo lightingInfo{};
        lightingInfo.cmdBuffer = cmdBuffer;
        lightingInfo.targetView = sceneColor.GetImageView();
        lightingInfo.extent = extent;
        lightingInfo.sceneData = &sceneData;
        lightingInfo.shadowData = &shadowData;
        lightingInfo.specularEnvView = env.specularView;
        lightingInfo.specularEnvSampler = env.specularSampler;
        lightingInfo.brdfLUTView = env.brdfLUTView;
        lightingInfo.brdfLUTSampler = env.brdfLUTSampler;

        lightingPass.Record(lightingInfo);

        if (hasEnvironment)
        {
            const VkImageMemoryBarrier2 sceneColorSkyBarrier = VulkanInitializers::ImageMemoryBarrier(
                sceneColor.GetImage(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

            dependencyInfo.pImageMemoryBarriers = &sceneColorSkyBarrier;

            vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);

            VulkanSkyPassRenderInfo skyInfo{};
            skyInfo.cmdBuffer = cmdBuffer;
            skyInfo.targetView = sceneColor.GetImageView();
            skyInfo.extent = extent;
            skyInfo.environmentView = env.environmentView;
            skyInfo.environmentSampler = env.environmentSampler;
            skyInfo.sceneData = &sceneData;

            skyPass.Record(skyInfo);
        }

        const VkImageMemoryBarrier2 sceneColorReadBarrier = VulkanInitializers::ImageMemoryBarrier(
            sceneColor.GetImage(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        dependencyInfo.pImageMemoryBarriers = &sceneColorReadBarrier;

        vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);

        sceneColorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkImageView toneMapSourceView = sceneColor.GetImageView();

        if (config.aa.mode == AAMode::TAA && sceneData.viewData.valid)
        {
            VulkanTAAResolveInfo taaInfo{};
            taaInfo.cmdBuffer = cmdBuffer;
            taaInfo.jitterUV = sceneData.viewData.jitter * 0.5f;
            taaInfo.feedbackMin = config.aa.taa.feedbackMin;
            taaInfo.feedbackMax = config.aa.taa.feedbackMax;

            toneMapSourceView = taa.Resolve(taaInfo);
        }
        else
        {
            ResetTemporalHistory();
        }

        VkPipelineStageFlags2 ldrColorSrcStage = VK_PIPELINE_STAGE_2_NONE;
        VkAccessFlags2 ldrColorSrcAccess = VK_ACCESS_2_NONE;

        if (ldrColorLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            ldrColorSrcStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            ldrColorSrcAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        }

        const VkImageMemoryBarrier2 ldrColorWriteBarrier = VulkanInitializers::ImageMemoryBarrier(
            ldrColor.GetImage(), VK_IMAGE_ASPECT_COLOR_BIT, ldrColorLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            ldrColorSrcStage, ldrColorSrcAccess, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &ldrColorWriteBarrier;

        vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);

        ldrColorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VulkanToneMapPassRenderInfo toneMapInfo{};
        toneMapInfo.cmdBuffer = cmdBuffer;
        toneMapInfo.targetView = ldrColor.GetImageView();
        toneMapInfo.extent = extent;
        toneMapInfo.exposureEV = sceneData.viewData.exposureEV;
        toneMapInfo.srcView = toneMapSourceView;

        toneMapPass.Record(toneMapInfo);

        const VkImageMemoryBarrier2 ldrColorReadBarrier = VulkanInitializers::ImageMemoryBarrier(
            ldrColor.GetImage(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        dependencyInfo.pImageMemoryBarriers = &ldrColorReadBarrier;

        vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);

        ldrColorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VulkanPostFXPassRenderInfo postFXInfo{};
        postFXInfo.cmdBuffer = cmdBuffer;
        postFXInfo.targetView = displayTarget.GetImageView();
        postFXInfo.extent = extent;
        postFXInfo.fxaaEnabled = config.aa.mode == AAMode::FXAA;
        postFXInfo.fxaaConfig = config.aa.fxaa;

        postFXPass.Record(postFXInfo);
    }
    else
    {
        taa.ResetHistory();

        GBufferDebugView gBufferView = GBufferDebugView::ALBEDO;

        switch (debugView)
        {
        case GraphicsDebugView::NORMAL:
        {
            gBufferView = GBufferDebugView::NORMAL;
            break;
        }
        case GraphicsDebugView::MATERIAL:
        {
            gBufferView = GBufferDebugView::MATERIAL;
            break;
        }
        case GraphicsDebugView::VELOCITY:
        {
            gBufferView = GBufferDebugView::VELOCITY;
            break;
        }
        case GraphicsDebugView::DEPTH:
        {
            gBufferView = GBufferDebugView::DEPTH;
            break;
        }
        default:
            break;
        }

        VulkanGBufferDebugPassRenderInfo gBufferDebugInfo{};
        gBufferDebugInfo.cmdBuffer = cmdBuffer;
        gBufferDebugInfo.targetView = displayTarget.GetImageView();
        gBufferDebugInfo.extent = extent;
        gBufferDebugInfo.debugView = gBufferView;

        gBufferDebugPass.Record(gBufferDebugInfo);
    }

    if (config.aa.mode == AAMode::TAA && sceneData.viewData.valid)
    {
        prevViewProj = sceneData.viewData.viewProj;
        prevProj = sceneData.viewData.proj;
        prevCameraId = sceneData.viewData.cameraId;

        prevObjectModels.clear();
        prevObjectModels.reserve(drawItems.size());

        for (const VulkanDrawItem& drawItem : drawItems)
        {
            if (drawItem.id == 0) continue;

            prevObjectModels[drawItem.id] = drawItem.model;
        }

        prevFrameValid = true;
    }
    else
    {
        prevFrameValid = false;
        prevObjectModels.clear();
    }

    if (displayColorUsedByUI)
    {
        const VkImageMemoryBarrier2 displayColorReadBarrier = VulkanInitializers::ImageMemoryBarrier(displayColor.GetImage(),
            VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        dependencyInfo.pImageMemoryBarriers = &displayColorReadBarrier;

        vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);

        displayColorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        const VkImageMemoryBarrier2 swapchainUIWriteBarrier = VulkanInitializers::ImageMemoryBarrier(swapchainImage.GetImage(),
            VK_IMAGE_ASPECT_COLOR_BIT, swapchainLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

        dependencyInfo.pImageMemoryBarriers = &swapchainUIWriteBarrier;

        vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);

        swapchainLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    else
    {
        const VkImageMemoryBarrier2 swapchainUIBarrier = VulkanInitializers::ImageMemoryBarrier(swapchainImage.GetImage(),
            VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

        dependencyInfo.pImageMemoryBarriers = &swapchainUIBarrier;

        vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);
    }

    VulkanUIPassRenderInfo uiInfo{};
    uiInfo.cmdBuffer = cmdBuffer;
    uiInfo.targetView = swapchainImage.GetImageView();
    uiInfo.extent = extent;
    uiInfo.clearTarget = displayColorUsedByUI;

    uiPass.Record(uiInfo);

    const VkImageMemoryBarrier2 swapchainPresentBarrier = VulkanInitializers::ImageMemoryBarrier(
        swapchainImage.GetImage(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);

    dependencyInfo.pImageMemoryBarriers = &swapchainPresentBarrier;

    vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);

    swapchainLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    if (vkEndCommandBuffer(cmdBuffer) != VK_SUCCESS) return VulkanFrameResult::FAILED;

    VkSemaphoreSubmitInfo waitSemaphoreInfo{};
    waitSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitSemaphoreInfo.semaphore = imageAvailable;
    waitSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    waitSemaphoreInfo.pNext = nullptr;

    VkCommandBufferSubmitInfo cmdBufferInfo{};
    cmdBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdBufferInfo.commandBuffer = cmdBuffer;
    cmdBufferInfo.pNext = nullptr;

    VkSemaphoreSubmitInfo signalSemaphoreInfo{};
    signalSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalSemaphoreInfo.semaphore = renderFinished;
    signalSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    signalSemaphoreInfo.pNext = nullptr;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitSemaphoreInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdBufferInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalSemaphoreInfo;
    submitInfo.pNext = nullptr;

    if (vkResetFences(buffDevice, 1, &inFlight) != VK_SUCCESS) return VulkanFrameResult::FAILED;

    if (vkQueueSubmit2(device->GetGraphicsQueue(), 1, &submitInfo, inFlight) != VK_SUCCESS) return VulkanFrameResult::FAILED;

    const VkSwapchainKHR buffSwapchain = swapchain.GetSwapchain();

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &buffSwapchain;
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.pNext = nullptr;

    const VkResult presentResult = vkQueuePresentKHR(device->GetPresentQueue(), &presentInfo);

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) return VulkanFrameResult::RECREATE;
    if (presentResult != VK_SUCCESS) return VulkanFrameResult::FAILED;

    return suboptimal ? VulkanFrameResult::RECREATE : VulkanFrameResult::SUCCESS;
}

Vector2f VulkanRenderer::GetProjectionJitter(VkExtent2D extent, bool temporalReset) const
{
    if (config.aa.mode != AAMode::TAA) return Vector2f(0.0f);
    if (temporalReset) return Vector2f(0.0f);
    if (debugView != GraphicsDebugView::FINAL) return Vector2f(0.0f);
    if (extent.width == 0 || extent.height == 0) return Vector2f(0.0f);

    return taa.GetProjectionJitter();
}

void VulkanRenderer::ResetTemporalHistory()
{
    taa.ResetHistory();

    prevFrameValid = false;
    prevViewProj = Matrix4f::Identity();
    prevObjectModels.clear();
    framePrevModels.clear();
    prevProj = Matrix4f::Identity();
    prevCameraId = 0;
}
#endif
