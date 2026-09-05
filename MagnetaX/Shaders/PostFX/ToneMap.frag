// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#version 450

layout(location = 0) in vec2 fragUV;
layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants
{
    float exposureEV;
} pc;

vec3 ToneMapACES(vec3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;

    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

float Luminance(vec3 color)
{
    return sqrt(dot(color, vec3(0.299, 0.587, 0.114)));
}

void main()
{
    vec3 color = texture(sceneColor, fragUV).rgb;
    color *= exp2(pc.exposureEV);
    color = ToneMapACES(color);
    
    outColor = vec4(color, Luminance(color));
}
