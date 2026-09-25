#include "../../Object3d.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// JSONのoffsetとHLSLの16バイト境界を一致させる。
cbuffer CustomMaterialParameters : register(b3)
{
    float4 gTint;       // offset 0
    float gBrightness;  // offset 16
    float3 gPadding;
}

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    float4 transformedUV = mul(
        float4(input.texcoord, 0.0f, 1.0f),
        gUVTransform);
    float4 textureColor = gTexture.Sample(
        gSampler, transformedUV.xy);
    float4 result = textureColor * gMaterialColor * gTint;
    result.rgb *= max(gBrightness, 0.0f);
    return result;
}
