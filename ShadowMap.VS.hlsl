#include "Skinning.hlsli"

cbuffer ShadowTransformBuffer : register(b0)
{
    float4x4 gLightWorldViewProjection;
}

struct ShadowVertexInput
{
    float4 position : POSITION0;
    uint4 boneIndices : BLENDINDICES0;
    float4 boneWeights : BLENDWEIGHT0;
};

float4 main(ShadowVertexInput input) : SV_POSITION
{
    const float4 skinnedPosition = SkinPosition(
        input.position, input.boneIndices, input.boneWeights);
    return mul(skinnedPosition, gLightWorldViewProjection);
}
