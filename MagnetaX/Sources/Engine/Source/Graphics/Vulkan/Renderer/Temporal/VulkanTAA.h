// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <Graphics/Vulkan/Resources/VulkanImage.h>
#include "VulkanCamVelocityPass.h"
#include "VulkanTAAPass.h"
#include <array>

struct VulkanTAACreateInfo
{
    VulkanDevice* device = nullptr;
    VkExtent2D extent{};

    VulkanImage* currentColor = nullptr;
    const VulkanImage* velocityImage = nullptr;
    const VulkanImage* depthImage = nullptr;
};

struct VulkanTAAResolveInfo
{
    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;

    Vector2f jitterUV{};

    float32 feedbackMin = 0.88f;
    float32 feedbackMax = 0.97f;
};

class VulkanTAA
{
public:
    VulkanTAA() = default;

    bool Create(const VulkanTAACreateInfo& createInfo);
    void Destroy();

    void RecordCameraVelocity(VkCommandBuffer cmdBuffer, const Matrix4f& invViewProj, const Matrix4f& prevViewProj);

    VkImageView Resolve(const VulkanTAAResolveInfo& resolveInfo);

    void ResetHistory();

    Vector2f GetProjectionJitter() const;

    const VulkanImage& GetHistoryImage(uint32 index) const { return history[index]; }
    bool IsHistoryValid() const { return historyValid; }
    

private:
    std::array<VulkanImage, 2> history;
    std::array<VkImageLayout, 2> historyLayouts{ VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED };

    std::array<VulkanImage, 2> depthHistory;
    std::array<VkImageLayout, 2> depthHistoryLayouts{ VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED };

    VulkanCamVelocityPass camVelocityPass;
    VulkanTAAPass taaPass;

    VkExtent2D extent{};

    uint64 frameIndex = 0;
    uint32 historyReadIndex = 0;
    bool historyValid = false;

    Vector2f prevJitterUV{};

    const VulkanImage* velocityImage = nullptr;
    const VulkanImage* depthImage = nullptr;
};
