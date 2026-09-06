// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#include "VulkanImageFormat.h"

#if MX_GRAPHICS_VULKAN
VkFormat VulkanImageFormat::FromImageFormat(ImageFormat format)
{
    switch (format)
    {
    case ImageFormat::RGBA8_SRGB:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case ImageFormat::BGRA8_SRGB:
        return VK_FORMAT_B8G8R8A8_SRGB;
    case ImageFormat::RGBA8_UNORM:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case ImageFormat::R16_FLOAT:
        return VK_FORMAT_R16_SFLOAT;
    case ImageFormat::RG16_FLOAT:
        return VK_FORMAT_R16G16_SFLOAT;
    case ImageFormat::RGBA16_FLOAT:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case ImageFormat::RGBA32_FLOAT:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case ImageFormat::R32_FLOAT:
        return VK_FORMAT_R32_SFLOAT;
    case ImageFormat::R32_UINT:
        return VK_FORMAT_R32_UINT;
    case ImageFormat::D32_FLOAT:
        return VK_FORMAT_D32_SFLOAT;
    case ImageFormat::R8_UNORM:
        return VK_FORMAT_R8_UNORM;
    default:
        return VK_FORMAT_UNDEFINED;
    }
}

ImageFormat VulkanImageFormat::ToImageFormat(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_R8G8B8A8_SRGB:
        return ImageFormat::RGBA8_SRGB;
    case VK_FORMAT_B8G8R8A8_SRGB:
        return ImageFormat::BGRA8_SRGB;
    case VK_FORMAT_R8G8B8A8_UNORM:
        return ImageFormat::RGBA8_UNORM;
    case VK_FORMAT_R16_SFLOAT:
        return ImageFormat::R16_FLOAT;
    case VK_FORMAT_R16G16_SFLOAT:
        return ImageFormat::RG16_FLOAT;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return ImageFormat::RGBA16_FLOAT;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return ImageFormat::RGBA32_FLOAT;
    case VK_FORMAT_R32_SFLOAT:
        return ImageFormat::R32_FLOAT;
    case VK_FORMAT_R32_UINT:
        return ImageFormat::R32_UINT;
    case VK_FORMAT_D32_SFLOAT:
        return ImageFormat::D32_FLOAT;
    case VK_FORMAT_R8_UNORM:
        return ImageFormat::R8_UNORM;
    default:
        return ImageFormat::UNKNOWN;
    }
}

VkImageAspectFlags VulkanImageFormat::GetImageAspect(ImageFormat format)
{
    switch (format)
    {
    case ImageFormat::RGBA8_SRGB:
    case ImageFormat::BGRA8_SRGB:
    case ImageFormat::RGBA8_UNORM:
    case ImageFormat::R16_FLOAT:
    case ImageFormat::RG16_FLOAT:
    case ImageFormat::RGBA16_FLOAT:
    case ImageFormat::RGBA32_FLOAT:
    case ImageFormat::R32_FLOAT:
    case ImageFormat::R32_UINT:
    case ImageFormat::R8_UNORM:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    case ImageFormat::D32_FLOAT:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    default:
        return 0;
    }
}

bool VulkanImageFormat::SupportsLinearBlit(VkPhysicalDevice physicalDevice, VkFormat format)
{
    if (!physicalDevice || format == VK_FORMAT_UNDEFINED) return false;

    VkFormatProperties2 formatProps{};
    formatProps.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;

    vkGetPhysicalDeviceFormatProperties2(physicalDevice, format, &formatProps);

    const VkFormatFeatureFlags requiredFeatures = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

    return (formatProps.formatProperties.optimalTilingFeatures & requiredFeatures) == requiredFeatures;
}
#endif
