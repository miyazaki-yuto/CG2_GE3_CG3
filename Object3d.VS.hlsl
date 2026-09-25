#include "Object3d.hlsli"

struct TransformationMatrix
{
    // WVPは頂点位置を画面座標へ、Worldは物体のワールド変換を表す。
    float4x4 WVP;
    float4x4 World;
    // 非均一スケール後も法線を面に対して垂直に保つための逆転置行列。
    float4x4 WorldInverseTranspose;
};

cbuffer gTransformationMatrix : register(b1)
{
    TransformationMatrix gTransformationMatrixData;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    
    // 座標をWVP行列でスクリーン空間に変換
    output.position = mul(input.position, gTransformationMatrixData.WVP);
    output.texcoord = input.texcoord;
    output.worldPosition = mul(
        input.position, gTransformationMatrixData.World).xyz;
    
    // 通常のWorld行列ではなく逆転置行列を使い、X/Y/Zで異なる拡大率にも対応する。
    output.normal = normalize(mul(
        input.normal,
        (float3x3) gTransformationMatrixData.WorldInverseTranspose));

    // Tangentは面に沿う方向なのでWorld行列で変換し、PS側でNormalと直交化する。
    float3 worldTangent = mul(
        input.tangent.xyz,
        (float3x3) gTransformationMatrixData.World);
    float tangentLengthSquared = dot(worldTangent, worldTangent);
    worldTangent = tangentLengthSquared > 0.000001f
        ? worldTangent * rsqrt(tangentLengthSquared)
        : float3(1.0f, 0.0f, 0.0f);
    output.tangent = float4(worldTangent, input.tangent.w);
    
    return output;
}
