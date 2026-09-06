// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <MX/Core/CoreMinimal.h>

enum class ImageFormat : uint8
{
    UNKNOWN,
    RGBA8_SRGB,
    BGRA8_SRGB,
    RGBA8_UNORM,
    R16_FLOAT,
    RG16_FLOAT,
    RGBA16_FLOAT,
    RGBA32_FLOAT,
    R32_FLOAT,
    R32_UINT,
    D32_FLOAT,
    R8_UNORM
};

struct ImageFormatUtils
{
    static constexpr uint32 GetBytesPerPixel(ImageFormat format)
    {
        switch (format)
        {
            case ImageFormat::R8_UNORM:
                return 1;
            case ImageFormat::R16_FLOAT:
                return 2;
            case ImageFormat::RGBA8_SRGB:
            case ImageFormat::BGRA8_SRGB:
            case ImageFormat::RGBA8_UNORM:
            case ImageFormat::D32_FLOAT:
            case ImageFormat::RG16_FLOAT:
            case ImageFormat::R32_FLOAT:
            case ImageFormat::R32_UINT:
                return 4;
            case ImageFormat::RGBA16_FLOAT:
                return 8;
            case ImageFormat::RGBA32_FLOAT:
                return 16;
            default:
            return 0;
        }
    }
};
