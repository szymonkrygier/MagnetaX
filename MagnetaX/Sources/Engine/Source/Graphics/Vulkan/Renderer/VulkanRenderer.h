// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <MX/Core/Math/Matrix.h>
#include <MX/Core/Math/Vector.h>
#include <MX/Graphics/GraphicsDebugView.h>
#include <MX/Graphics/Renderer/RendererConfig.h>
#include <MX/Graphics/Renderer/UI/UITexture.h>
#include <Graphics/Vulkan/VulkanCommon.h>
#include <Graphics/Vulkan/Resources/VulkanImage.h>
#include "VulkanCommandPool.h"
#include "VulkanDrawItem.h"
#include "Deferred/VulkanGBufferDebugPass.h"
#include "Deferred/VulkanGBufferPass.h"
#include "Deferred/VulkanLightingPass.h"
#include "PostFX/VulkanPostFXPass.h"
#include "PostFX/VulkanToneMapPass.h"
#include "Temporal/VulkanTAA.h"
#include "Shadow/VulkanShadowDepthPass.h"
#include "UI/VulkanUIPass.h"
#include "Environment/VulkanEnvironmentRenderData.h"
#include "Environment/VulkanSkyPass.h"
#include <span>
#include <vector>
#include <array>
#include <unordered_map>

class VulkanPresentContext;
struct RenderSceneData;
struct UIRenderData;
class VulkanUIRenderer;

enum class VulkanFrameResult
{
    SUCCESS,
    RECREATE,
    FAILED
};

struct VulkanRendererCreateInfo
{
    VulkanDevice* device = nullptr;
    VulkanPresentContext* presentContext = nullptr;

    VkDescriptorSetLayout materialDescSetLayout = VK_NULL_HANDLE;

    RendererConfig config{};

    VulkanUIRenderer* uiRenderer = nullptr;
};

struct VulkanRendererFrameInfo
{
    std::span<const VulkanDrawItem> drawItems;

    const RenderSceneData* sceneData = nullptr;
    const UIRenderData* uiData = nullptr;

    VulkanEnvironmentRenderData environment{};

    bool resetTemporalHistory = false;
};

// Since MagnetaX is under active dev and doesn't have
// proper documentation yet, I will keep current rendering
// flow there and edit it as I change something
// 
// This flow will be later a part of documentation! :)
//
//   Scene (ECS layer)
//     |
//     v
// RenderSceneData + VulkanDrawItems (Graphics layer)
//     |
//     --------------------------------
//     |                              |
//     v                              v
// Directional shadows           Spot shadows
// (directionalShadowPass)     (spotShadowPass)
//     |                              |
//     |                              |
//     ----------------+---------------
//                     |
//                     v
//           GBuffer (gBufferPass)
//                     |
//              Cam velocity fill
//                    (taa)
//                     |
//          -----------+-----------
//          |                     |
//          | FINAL               | DEBUG
//          v                     v
//       Lighting             Debug pass
//    (lightingPass)      (gBufferDebugPass)
//          |                     |
//      Sky  (skyPass)            |
//          |                     |
//          v                     |
//   sceneColor (HDR)             |
//          |                     |
//    TAA (taa)*                  |
//          |                     |
//          v                     |
// temporal/sceneColor            |
//          |                     |
//   Tone mapping                 |
//   (toneMapPass)                |
//          |                     |
//          v                     |
//     ldrColor (LDR)             |
//          |                     |
// PostFX (postFXPass)            |
//          |                     |
//          -----------------------
//                     |
//                     v
//         displayColor (display ready)
//                     |
//                     v
//            UI composition (uiPass)
//                     |
//                     v
//              swapchainImage
//                     |
//                     v
//                  Present
//
// * - optional pass (only if enabled) 
// 
// sceneColor is the HDR scene result
// produced by lighting and sky
// 
// ldrColor is the tone mapped LDR image
// used as input for PostFX pass
// 
// display color is used when the display
// ready scene needs to be sampled by UI
// before final copmosition
class VulkanRenderer
{
public:
    VulkanRenderer() = default;
    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    bool Create(const VulkanRendererCreateInfo& createInfo);
    void Destroy();

    VulkanFrameResult DrawFrame(const VulkanRendererFrameInfo& frameInfo);

    void SetDebugView(GraphicsDebugView view) { debugView = view; }

    Vector2f GetProjectionJitter(VkExtent2D extent, bool temporalReset = false) const;

private:
    VulkanDevice* device = nullptr;

    VulkanPresentContext* presentContext = nullptr;
    RendererConfig config{};

    VulkanCommandPool commandPool;
    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;

    // Shadow passes
    VulkanShadowDepthPass directionalShadowPass;
    VulkanShadowDepthPass spotShadowPass;
    //VulkanShadowDepthPass pointShadowPass;

    // GBuffer pass
    VulkanGBufferPass gBufferPass;

    // Lighting and sky passes
    VulkanLightingPass lightingPass;
    VulkanSkyPass skyPass;

    // Scene color
    VulkanImage sceneColor;
    VkImageLayout sceneColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Temporal AA (TAA)
    VulkanTAA taa;
    bool prevFrameValid = false;
    Matrix4f prevViewProj = Matrix4f::Identity();
    Matrix4f prevProj = Matrix4f::Identity();
    uint32 prevCameraId = 0;
    std::unordered_map<uint32, Matrix4f> prevObjectModels;
    std::vector<Matrix4f> framePrevModels;

    // Tone mapping pass
    VulkanImage ldrColor;
    VkImageLayout ldrColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VulkanToneMapPass toneMapPass;

    // PostFX pass
    VulkanPostFXPass postFXPass;

    // Debug pass
    VulkanGBufferDebugPass gBufferDebugPass;

    // Display color
    VulkanImage displayColor;
    VkImageLayout displayColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkSampler displayColorSampler = VK_NULL_HANDLE;
    UITextureHandle displayColorUITexture;

    // UI pass
    VulkanUIPass uiPass;
    VulkanUIRenderer* uiRenderer = nullptr;

    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    VkFence inFlight = VK_NULL_HANDLE;

    std::vector<VkImageLayout> swapchainImageLayouts;

    GraphicsDebugView debugView = GraphicsDebugView::FINAL;

    void ResetTemporalHistory();
};
