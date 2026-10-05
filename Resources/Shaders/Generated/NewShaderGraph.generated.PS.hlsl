#include "../../../Object3d.hlsli"
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);
cbuffer CustomMaterialParameters : register(b3)
{
    float4 gParameter2; // offset 0
}
float4 main(VertexShaderOutput input) : SV_TARGET0
{
    float4 transformedUV = mul(float4(input.texcoord, 0, 1), gUVTransform);
    float4 sampledTexture = gTexture.Sample(gSampler, transformedUV.xy) * gMaterialColor;
    return (sampledTexture * gParameter2);
}
