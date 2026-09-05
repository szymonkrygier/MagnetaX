// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#version 450

layout(set = 0, binding = 0) uniform sampler2D currentColor;

layout(location = 0) out float outLuminanceContext;

float Luminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

void main()
{
    ivec2 sourceSize = textureSize(currentColor, 0);
    ivec2 baseCoord = ivec2(gl_FragCoord.xy) * 8;

    float luminanceSum = 0.0;
    float sampleCount = 0.0;

    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            ivec2 coord = baseCoord + ivec2(x, y);

            if (coord.x >= sourceSize.x || coord.y >= sourceSize.y) continue;

            luminanceSum += Luminance(texelFetch(currentColor, coord, 0).rgb);
            sampleCount += 1.0;
        }
    }

    float meanLuminance = luminanceSum / max(sampleCount, 1.0);
    outLuminanceContext = log2(max(meanLuminance, 0.0001));
}
