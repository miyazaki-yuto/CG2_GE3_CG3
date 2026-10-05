#include "Skinning.hlsli"

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 tangent : TANGENT0;
    uint4 boneIndices : BLENDINDICES0;
    float4 boneWeights : BLENDWEIGHT0;
};

struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
    float4x4 WorldInverseTranspose;
};

cbuffer gTransformationMatrix : register(b1)
{
    TransformationMatrix gTransformationMatrixData;
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    const float4 skinnedPosition = SkinPosition(
        input.position, input.boneIndices, input.boneWeights);
    const float3 skinnedNormal = SkinDirection(
        input.normal, input.boneIndices, input.boneWeights);

    float4 clipPosition = mul(
        skinnedPosition, gTransformationMatrixData.WVP);
    const float4 clipNormalEnd = mul(
        float4(skinnedPosition.xyz + skinnedNormal, 1.0f),
        gTransformationMatrixData.WVP);

    const float2 screenPosition =
        clipPosition.xy / max(abs(clipPosition.w), 0.0001f);
    const float2 screenNormalEnd =
        clipNormalEnd.xy / max(abs(clipNormalEnd.w), 0.0001f);
    const float2 projectedNormal = screenNormalEnd - screenPosition;
    const float normalLengthSquared =
        dot(projectedNormal, projectedNormal);
    const float2 outlineDirection = normalLengthSquared > 0.000001f
        ? projectedNormal * rsqrt(normalLengthSquared)
        : float2(0.0f, -1.0f);

    // NDC上で一定量だけ広げるため、距離に依存しにくい輪郭幅になる。
    clipPosition.xy += outlineDirection * 0.006f * clipPosition.w;
    output.position = clipPosition;
    return output;
}
