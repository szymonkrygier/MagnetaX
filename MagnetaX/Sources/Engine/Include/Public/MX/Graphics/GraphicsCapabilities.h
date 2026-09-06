// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <MX/Core/CoreMinimal.h>

struct GraphicsCapabilities
{
    bool samplerAniso = false;
    float32 maxSamplerAniso = 1.0f;
    bool sampleRateShading = false;
};
