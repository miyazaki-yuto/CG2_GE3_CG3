static const uint kMaxSkinningBones = 128;

// DXCは定数バッファの行列配列を動的参照すると、添字を範囲内へ
// clampしていてもX4000を出す。入力はCPU側で初期化・範囲制限済み。
#pragma warning(disable : 4000)

cbuffer SkinningBuffer : register(b4)
{
    float4x4 gBoneMatrices[kMaxSkinningBones];
    int gSkinningEnabled;
    uint gSkinningBoneCount;
    float2 gSkinningPadding;
}

float GetSkinWeightSum(float4 weights)
{
    return weights.x + weights.y + weights.z + weights.w;
}

float4 SkinPosition(float4 position, uint4 indices, float4 weights)
{
    if (gSkinningEnabled == 0 || GetSkinWeightSum(weights) <= 0.000001f)
    {
        return position;
    }

    const uint4 safeIndices = min(indices, kMaxSkinningBones - 1);
    return mul(position, gBoneMatrices[safeIndices.x]) * weights.x +
        mul(position, gBoneMatrices[safeIndices.y]) * weights.y +
        mul(position, gBoneMatrices[safeIndices.z]) * weights.z +
        mul(position, gBoneMatrices[safeIndices.w]) * weights.w;
}

float3 SkinDirection(float3 direction, uint4 indices, float4 weights)
{
    if (gSkinningEnabled == 0 || GetSkinWeightSum(weights) <= 0.000001f)
    {
        return direction;
    }

    const uint4 safeIndices = min(indices, kMaxSkinningBones - 1);
    return mul(direction, (float3x3)gBoneMatrices[safeIndices.x]) * weights.x +
        mul(direction, (float3x3)gBoneMatrices[safeIndices.y]) * weights.y +
        mul(direction, (float3x3)gBoneMatrices[safeIndices.z]) * weights.z +
        mul(direction, (float3x3)gBoneMatrices[safeIndices.w]) * weights.w;
}
