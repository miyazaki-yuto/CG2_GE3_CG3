// Object3d.VS.hlsl
#include "Object3d.hlsli"

struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World; 
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
    
    // 法線をワールド空間に変換してピクセルシェーダーへ渡す
    output.normal = normalize(mul(input.normal, (float3x3) gTransformationMatrixData.World));
    
    return output;
}