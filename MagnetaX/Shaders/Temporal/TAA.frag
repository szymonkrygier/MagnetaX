// Copyright (c) 2026 PulsaX Szymon Krygier
// SPDX-License-Identifier: MPL-2.0
#version 450

layout(location = 0) in vec2 fragUV;

layout(set = 0, binding = 0) uniform sampler2D currentColor;
layout(set = 0, binding = 1) uniform sampler2D historyColor;
layout(set = 0, binding = 2) uniform sampler2D velocityTexture;
layout(set = 0, binding = 3) uniform sampler2D depthTexture;
layout(set = 0, binding = 4) uniform sampler2D previousDepthTexture;
layout(set = 0, binding = 5) uniform sampler2D previousMetadata;
layout(set = 0, binding = 6) uniform sampler2D luminanceContext;

layout(push_constant) uniform PushConstants
{
    vec2 jitterUV;
    vec2 prevJitterUV;
    float feedbackMin;
    float feedbackMax;
    uint historyValid;
} pc;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outMetadata;

float Luminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

vec3 RGBToYCoCg(vec3 color)
{
    return vec3(dot(color, vec3(0.25, 0.5, 0.25)), dot(color, vec3(0.5, 0.0, -0.5)),
        dot(color, vec3(-0.25, 0.5, -0.25)));
}

vec3 YCoCgToRGB(vec3 color)
{
    return vec3(color.x + color.y - color.z, color.x + color.z, color.x - color.y - color.z);
}

bool DetectThinFeature(float luma[9], float relativeThreshold, float absoluteThreshold)
{
    const uint blockMasks[4] = uint[4](0x01Bu, 0x036u, 0x0D8u, 0x1B0u);

    float center = luma[4];
    uint similarMask = 1u << 4u;
    bool hasLower = false;
    bool hasHigher = false;

    for (int i = 0; i < 9; ++i)
    {
        if (i == 4) continue;

        float delta = luma[i] - center;
        float threshold = max(absoluteThreshold, relativeThreshold * max(center, luma[i]));

        if (abs(delta) <= threshold)
        {
            similarMask |= 1u << uint(i);
        }
        else
        {
            hasLower = hasLower || delta < 0.0;
            hasHigher = hasHigher || delta > 0.0;
        }
    }

    if (hasLower == hasHigher) return false;

    for (int i = 0; i < 4; ++i)
    {
        if ((similarMask & blockMasks[i]) == blockMasks[i]) return false;
    }

    return true;
}

void GetRoundedNeighborhood(vec2 uv, vec2 texelSize, out vec3 mean, out vec3 stdDev, out float luma[9])
{
    vec3 topLeft = texture(currentColor, uv + vec2(-texelSize.x, -texelSize.y)).rgb;
    vec3 top = texture(currentColor, uv + vec2(0.0, -texelSize.y)).rgb;
    vec3 topRight = texture(currentColor, uv + vec2(texelSize.x, -texelSize.y)).rgb;

    vec3 left = texture(currentColor, uv + vec2(-texelSize.x, 0.0)).rgb;
    vec3 center = texture(currentColor, uv).rgb;
    vec3 right = texture(currentColor, uv + vec2(texelSize.x, 0.0)).rgb;

    vec3 bottomLeft = texture(currentColor, uv + vec2(-texelSize.x, texelSize.y)).rgb;
    vec3 bottom = texture(currentColor, uv + vec2(0.0, texelSize.y)).rgb;
    vec3 bottomRight = texture(currentColor, uv + vec2(texelSize.x, texelSize.y)).rgb;

    vec3 topLeftYCoCg = RGBToYCoCg(topLeft);
    vec3 topYCoCg = RGBToYCoCg(top);
    vec3 topRightYCoCg = RGBToYCoCg(topRight);
    vec3 leftYCoCg = RGBToYCoCg(left);
    vec3 centerYCoCg = RGBToYCoCg(center);
    vec3 rightYCoCg = RGBToYCoCg(right);
    vec3 bottomLeftYCoCg = RGBToYCoCg(bottomLeft);
    vec3 bottomYCoCg = RGBToYCoCg(bottom);
    vec3 bottomRightYCoCg = RGBToYCoCg(bottomRight);

    luma[0] = max(topLeftYCoCg.x, 0.0);
    luma[1] = max(topYCoCg.x, 0.0);
    luma[2] = max(topRightYCoCg.x, 0.0);
    luma[3] = max(leftYCoCg.x, 0.0);
    luma[4] = max(centerYCoCg.x, 0.0);
    luma[5] = max(rightYCoCg.x, 0.0);
    luma[6] = max(bottomLeftYCoCg.x, 0.0);
    luma[7] = max(bottomYCoCg.x, 0.0);
    luma[8] = max(bottomRightYCoCg.x, 0.0);

    vec3 moment1 = topLeftYCoCg + topYCoCg + topRightYCoCg + leftYCoCg + centerYCoCg + rightYCoCg +
        bottomLeftYCoCg + bottomYCoCg + bottomRightYCoCg;

    vec3 moment2 = topLeftYCoCg * topLeftYCoCg + topYCoCg * topYCoCg + topRightYCoCg * topRightYCoCg + 
        leftYCoCg * leftYCoCg + centerYCoCg * centerYCoCg + rightYCoCg * rightYCoCg + bottomLeftYCoCg * 
        bottomLeftYCoCg + bottomYCoCg * bottomYCoCg + bottomRightYCoCg * bottomRightYCoCg;

    mean = moment1 / 9.0;

    vec3 variance = max(moment2 / 9.0 - mean * mean, vec3(0.0));
    stdDev = sqrt(variance);
}

vec4 GetClosestVelocity(vec2 uv, out float closestDepth, out vec2 closestUV)
{
    ivec2 imageSize = textureSize(depthTexture, 0);
    ivec2 maxCoord = imageSize - ivec2(1);
    ivec2 centerCoord = clamp(ivec2(uv * vec2(imageSize)), ivec2(0), maxCoord);

    ivec2 closestCoord = centerCoord;
    closestDepth = texelFetch(depthTexture, centerCoord, 0).r;

    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            ivec2 coord = clamp(centerCoord + ivec2(x, y), ivec2(0), maxCoord);
            float depth = texelFetch(depthTexture, coord, 0).r;

            if (depth < closestDepth)
            {
                closestDepth = depth;
                closestCoord = coord;
            }
        }
    }

    closestUV = (vec2(closestCoord) + vec2(0.5)) / vec2(imageSize);

    return texelFetch(velocityTexture, closestCoord, 0);
}

vec4 SampleHistoryCatmullRom(vec2 uv)
{
    vec2 imageSize = vec2(textureSize(historyColor, 0));
    vec2 rcpResolution = 1.0 / imageSize;
    vec2 historyST = uv * imageSize - vec2(0.5);
    vec2 fractional = fract(historyST);
    vec2 baseUV = (floor(historyST) + vec2(0.5)) * rcpResolution;

    vec2 t = fractional;
    vec2 t2 = t * t;
    vec2 t3 = t2 * t;

    const float s = 0.5;

    vec2 w0 = -s * t3 + 2.0 * s * t2 - s * t;
    vec2 w1 = (2.0 - s) * t3 + (s - 3.0) * t2 + vec2(1.0);
    vec2 w2 = (s - 2.0) * t3 + (3.0 - 2.0 * s) * t2 + s * t;
    vec2 w3 = s * t3 - s * t2;

    vec2 s0 = w1 + w2;
    vec2 f0 = w2 / s0;

    vec2 m0 = baseUV + f0 * rcpResolution;
    vec2 tc0 = baseUV - rcpResolution;
    vec2 tc3 = baseUV + 2.0 * rcpResolution;

    vec4 a = texture(historyColor, vec2(m0.x, tc0.y));
    vec4 b = texture(historyColor, vec2(tc0.x, m0.y));
    vec4 c = texture(historyColor, m0);
    vec4 d = texture(historyColor, vec2(tc3.x, m0.y));
    vec4 e = texture(historyColor, vec2(m0.x, tc3.y));

    //return (0.5 * (a + b) * w0.x + a * s0.x + 0.5 * (a + b) * w3.x) * w0.y + (b * w0.x + c * s0.x + d * w3.x) *
    //    s0.y + (0.5 * (b + e) * w0.x + e * s0.x + 0.5 * (d + e) * w3.x) * w3.y;

    //return (0.5 * (a + b) * w0.x + a * s0.x + 0.5 * (a + d) * w3.x) * w0.y + (b * w0.x + c * s0.x + d * w3.x) * s0.y + 
    //    (0.5 * (b + e) * w0.x + e * s0.x + 0.5 * (d + e) * w3.x) * w3.y;

    float weightA = s0.x * w0.y;
    float weightB = w0.x * s0.y;
    float weightC = s0.x * s0.y;
    float weightD = w3.x * s0.y;
    float weightE = s0.x * w3.y;
    float weightSum = weightA + weightB + weightC + weightD + weightE;

    vec4 result = (a * weightA + b * weightB + c * weightC + d * weightD + e * weightE) / weightSum;

    vec3 minColor = min(min(a.rgb, b.rgb), min(c.rgb, min(d.rgb, e.rgb)));
    vec3 maxColor = max(max(a.rgb, b.rgb), max(c.rgb, max(d.rgb, e.rgb)));

    result.rgb = clamp(result.rgb, minColor, maxColor);

    return result;
}

float GetPreviousDepth(vec2 uv)
{
    vec4 depths = textureGather(previousDepthTexture, uv, 0);
    return max(max(depths.x, depths.y), max(depths.z, depths.w));
}

float DecodeThinReferenceContext(vec2 metadata)
{
    if (metadata.r <= 0.0) return 0.0;
    return metadata.g / metadata.r;
}

bool ResolveThinLock(bool thinFeatureDetected, vec2 previousThinMetadata, float previousThinFeatureSupport, float currentLuminanceContext, 
    float lifetimeStep, out vec2 thinMetadata)
{
    float lifetime = max(previousThinMetadata.r - lifetimeStep, 0.0);
    float support = max(previousThinFeatureSupport - lifetimeStep, 0.0);
    float referenceContext = DecodeThinReferenceContext(previousThinMetadata);

    if (thinFeatureDetected)
    {
        if (previousThinMetadata.r <= 0.0) referenceContext = currentLuminanceContext;

        lifetime = 1.0;
        support = 1.0;
    }

    if (lifetime <= 0.0) referenceContext = 0.0;

    thinMetadata = vec2(lifetime, lifetime * referenceContext);

    return lifetime > 0.0 || support > 0.0;
}

void main()
{
    vec2 currentUV = fragUV + pc.jitterUV;
    vec4 current = texture(currentColor, currentUV);
    current.a = 1.0;

    vec2 texelSize = 1.0 / vec2(textureSize(currentColor, 0));

    vec3 neighborhoodMean;
    vec3 neighborhoodStdDev;
    float neighborhoodLuma[9];

    GetRoundedNeighborhood(currentUV, texelSize, neighborhoodMean, neighborhoodStdDev, neighborhoodLuma);

    const float thinFeatureRelativeThreshold = 0.05;
    const float thinFeatureAbsoluteThreshold = 0.001;

    bool thinFeatureDetected = DetectThinFeature(neighborhoodLuma, thinFeatureRelativeThreshold, thinFeatureAbsoluteThreshold);

    vec2 luminanceContextSize = vec2(textureSize(luminanceContext, 0));
    vec2 luminanceContextUV = currentUV * vec2(textureSize(currentColor, 0)) / (luminanceContextSize * 8.0);
    float currentLuminanceContext = textureLod(luminanceContext, luminanceContextUV, 0.0).r;

    float surroundingLuminance = (neighborhoodLuma[0] + neighborhoodLuma[1] + neighborhoodLuma[2] + neighborhoodLuma[3] + 
        neighborhoodLuma[5] + neighborhoodLuma[6] + neighborhoodLuma[7] + neighborhoodLuma[8]) * 0.125;

    const float thinFeatureLifetimeStep = 1.0 / 16.0;

    outMetadata = vec2(thinFeatureDetected ? 1.0 : 0.0, thinFeatureDetected ? currentLuminanceContext : 0.0);

    if (pc.historyValid == 0)
    {
        outColor = current;
        return;
    }

    float closestDepth = 1.0;
    vec2 closestUV;
    vec4 velocity = GetClosestVelocity(currentUV, closestDepth, closestUV);

    if (velocity.w < 0.5)
    {
        outColor = current;
        return;
    }

    vec2 previousUV = fragUV - velocity.xy;
    float expectedPreviousDepth = closestDepth + velocity.z;

    if (any(lessThan(previousUV, vec2(0.0))) || any(greaterThanEqual(previousUV, vec2(1.0))))
    {
        outColor = current;
        return;
    }

    vec2 previousDepthUV = closestUV - pc.jitterUV - velocity.xy + pc.prevJitterUV;

    if (any(lessThan(previousDepthUV, vec2(0.0))) || any(greaterThanEqual(previousDepthUV, vec2(1.0))))
    {
        outColor = current;
        return;
    }

    float previousDepth = GetPreviousDepth(previousDepthUV);

    const float depthFloatEpsilon = 0.0000002;
    const float velocityDepthEpsilon = abs(velocity.z) / 512.0;
    float depthEpsilon = max(depthFloatEpsilon, velocityDepthEpsilon);

    if (expectedPreviousDepth > previousDepth + depthEpsilon)
    {
        outColor = current;
        return;
    }

    vec2 previousThinMetadata = textureLod(previousMetadata, previousUV, 0.0).rg;
    vec4 previousThinLifetimeSamples = textureGather(previousMetadata, previousUV, 0);
    float previousThinFeatureSupport = max(max(previousThinLifetimeSamples.x, previousThinLifetimeSamples.y), max(previousThinLifetimeSamples.z, previousThinLifetimeSamples.w));
    
    vec4 history = SampleHistoryCatmullRom(previousUV);
    float previousHistoryMass = textureLod(historyColor, previousUV, 0.0).a;
    vec3 historyYCoCg = RGBToYCoCg(history.rgb);

    vec2 thinMetadata;
    bool thinLockValid = ResolveThinLock(thinFeatureDetected, previousThinMetadata, previousThinFeatureSupport, currentLuminanceContext, thinFeatureLifetimeStep, thinMetadata);

    outMetadata = thinMetadata;

    vec2 imageSize = vec2(textureSize(currentColor, 0));
    vec2 velocityPixels = velocity.xy * imageSize;

    float velocityConfidence = clamp(1.0 - length(velocityPixels) / 128.0, 0.0, 1.0);
    float varianceGamma = mix(0.75, 2.0, velocityConfidence * velocityConfidence);

    vec3 varianceExtent = neighborhoodStdDev * varianceGamma;
    vec3 varianceMin = neighborhoodMean - varianceExtent;
    vec3 varianceMax = neighborhoodMean + varianceExtent;

    vec3 clippedHistoryYCoCg = clamp(historyYCoCg, varianceMin, varianceMax);

    vec3 historyDeviation = abs(historyYCoCg - neighborhoodMean);
    vec3 acceptedDeviation = abs(clippedHistoryYCoCg - neighborhoodMean);
    vec3 historyAcceptance = min(acceptedDeviation / max(historyDeviation, vec3(0.000001)), vec3(1.0));
    historyAcceptance = mix(vec3(1.0), historyAcceptance, greaterThan(historyDeviation, vec3(0.000001)));
    float acceptedHistory = min(historyAcceptance.x, min(historyAcceptance.y, historyAcceptance.z));

    float historyContrast = abs(historyYCoCg.x - surroundingLuminance) / max(max(abs(historyYCoCg.x), surroundingLuminance), 0.05);
    float historyDetailConfidence = smoothstep(0.05, 0.20, historyContrast);

    if (thinLockValid) clippedHistoryYCoCg.x = historyYCoCg.x;

    historyYCoCg = clippedHistoryYCoCg;
    history.rgb = YCoCgToRGB(historyYCoCg);

    float currentLuminance = Luminance(current.rgb);
    float historyLuminance = Luminance(history.rgb);

    float luminanceDifference = abs(currentLuminance - historyLuminance) / max(currentLuminance, max(historyLuminance, 0.2));
    float similarity = clamp(1.0 - luminanceDifference, 0.0, 1.0);

    float feedback = mix(pc.feedbackMin, pc.feedbackMax, similarity * similarity);
    float thinFeatureFeedback = pow(pc.feedbackMax, thinFeatureLifetimeStep);

    if (thinLockValid) feedback = thinFeatureFeedback;

    const float maxHistoryMass = 65504.0;
    float historyBudget = min(feedback / max(1.0 - feedback, 0.000001), maxHistoryMass);
    float acceptedHistoryMass = min(max(previousHistoryMass, 0.0), historyBudget);

    //if (!thinLockValid && historyWasClipped) acceptedHistoryMass = min(acceptedHistoryMass, 1.0);
    if (!thinLockValid) acceptedHistoryMass *= acceptedHistory;

    float currentWeight = 1.0 / (acceptedHistoryMass + 1.0);

    outColor.rgb = mix(history.rgb, current.rgb, currentWeight);
    outColor.a = min(acceptedHistoryMass + 1.0, historyBudget);
}
