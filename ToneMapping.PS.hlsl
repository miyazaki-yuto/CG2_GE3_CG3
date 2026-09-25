Texture2D<float4> gHdrSceneTexture : register(t0);
Texture2D<float> gSceneDepthTexture : register(t1);
SamplerState gLinearClampSampler : register(s0);

cbuffer ToneMappingSettings : register(b0)
{
    float gExposure;
    int gToneMappingMode;
    float2 gSourceUvScale;
    int gAntiAliasingMode;
    int gSsaoEnabled;
    float gSsaoStrength;
    int gBloomEnabled;
    float gBloomIntensity;
    float gBloomThreshold;
    float2 gInverseSourceSize;
    uint gFrameIndex;
    float3 gPadding;
}

static const int kToneMappingReinhard = 1;
static const int kToneMappingACES = 2;
static const int kAaFXAA = 1;
static const int kAaTAA = 2;
static const int kAaMAA = 3;

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 SampleHdr(float2 uv)
{
    return gHdrSceneTexture.Sample(
        gLinearClampSampler, saturate(uv) * gSourceUvScale).rgb;
}

float3 ResolveAntiAliasing(float2 uv)
{
    float3 center = SampleHdr(uv);
    if (gAntiAliasingMode == 0)
    {
        return center;
    }

    float2 texel = gInverseSourceSize;
    float3 north = SampleHdr(uv + float2(0.0f, -texel.y));
    float3 south = SampleHdr(uv + float2(0.0f, texel.y));
    float3 west = SampleHdr(uv + float2(-texel.x, 0.0f));
    float3 east = SampleHdr(uv + float2(texel.x, 0.0f));
    float centerLuma = Luminance(center);
    float edgeRange = max(
        max(abs(Luminance(north) - centerLuma),
            abs(Luminance(south) - centerLuma)),
        max(abs(Luminance(west) - centerLuma),
            abs(Luminance(east) - centerLuma)));

    if (edgeRange < max(0.0312f, centerLuma * 0.08f))
    {
        return center;
    }

    if (gAntiAliasingMode == kAaFXAA)
    {
        // FXAAは高コントラストの輪郭だけを近傍色へ寄せる。
        return lerp(center, (north + south + west + east) * 0.25f, 0.55f);
    }
    if (gAntiAliasingMode == kAaMAA)
    {
        // MAAは輪郭と平行な方向を選び、輪郭を横切らずに補間する。
        float horizontal = abs(Luminance(west) - Luminance(east));
        float vertical = abs(Luminance(north) - Luminance(south));
        float3 alongEdge = horizontal < vertical
            ? (west + east) * 0.5f
            : (north + south) * 0.5f;
        return lerp(center, alongEdge, 0.65f);
    }

    // History Bufferを増やさずに使える軽量TAA。
    // フレームごとに回転するSub-pixel位置をResolveして細いちらつきを抑える。
    float2 jitterPattern[4] = {
        float2(-0.375f, -0.125f), float2(0.125f, -0.375f),
        float2(0.375f, 0.125f), float2(-0.125f, 0.375f)
    };
    float2 jitter = jitterPattern[gFrameIndex & 3] * texel;
    float3 temporalSamples =
        SampleHdr(uv + jitter) +
        SampleHdr(uv - jitter) +
        north + south + west + east;
    return lerp(center, temporalSamples / 6.0f, 0.55f);
}

float CalculateSsao(float2 uv)
{
    if (gSsaoEnabled == 0)
    {
        return 1.0f;
    }
    float centerDepth = gSceneDepthTexture.Sample(
        gLinearClampSampler, uv * gSourceUvScale);
    if (centerDepth >= 0.9999f)
    {
        return 1.0f;
    }

    float occlusion = 0.0f;
    const float2 directions[8] = {
        float2(1, 0), float2(-1, 0), float2(0, 1), float2(0, -1),
        float2(0.707f, 0.707f), float2(-0.707f, 0.707f),
        float2(0.707f, -0.707f), float2(-0.707f, -0.707f)
    };
    [unroll]
    for (int index = 0; index < 8; ++index)
    {
        float sampleDepth = gSceneDepthTexture.Sample(
            gLinearClampSampler,
            (uv + directions[index] * gInverseSourceSize * 4.0f) *
                gSourceUvScale);
        float depthDifference = centerDepth - sampleDepth;
        occlusion += saturate((depthDifference - 0.00015f) * 900.0f);
    }
    return saturate(1.0f - (occlusion / 8.0f) * gSsaoStrength);
}

float3 CalculateBloom(float2 uv)
{
    if (gBloomEnabled == 0)
    {
        return 0.0f;
    }
    float3 bloom = 0.0f;
    float weightSum = 0.0f;
    [unroll]
    for (int y = -2; y <= 2; ++y)
    {
        [unroll]
        for (int x = -2; x <= 2; ++x)
        {
            float weight = 1.0f / (1.0f + float(x * x + y * y));
            float3 sampleColor = SampleHdr(
                uv + float2(x, y) * gInverseSourceSize * 3.0f);
            float brightness = Luminance(sampleColor);
            bloom += sampleColor *
                saturate((brightness - gBloomThreshold) /
                    max(brightness, 0.0001f)) * weight;
            weightSum += weight;
        }
    }
    return bloom / weightSum * gBloomIntensity;
}

float3 ApplyAcesFilm(float3 color)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate(
        (color * (a * color + b)) /
        (color * (c * color + d) + e));
}

float4 main(float4 position : SV_POSITION, float2 texcoord : TEXCOORD0)
    : SV_TARGET
{
    float3 hdrColor = ResolveAntiAliasing(texcoord);
    hdrColor *= CalculateSsao(texcoord);
    hdrColor += CalculateBloom(texcoord);
    float3 exposedColor = max(hdrColor, 0.0f) * exp2(gExposure);
    float3 mappedColor = saturate(exposedColor);
    if (gToneMappingMode == kToneMappingReinhard)
    {
        mappedColor = exposedColor / (1.0f + exposedColor);
    }
    else if (gToneMappingMode == kToneMappingACES)
    {
        mappedColor = ApplyAcesFilm(exposedColor);
    }
    return float4(mappedColor, 1.0f);
}
