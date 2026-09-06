// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include "AAMode.h"
#include "FXAAConfig.h"
#include "TAAConfig.h"

struct AAConfig
{
    AAMode mode = AAMode::FXAA;

    FXAAConfig fxaa{};
    TAAConfig taa{};
};
