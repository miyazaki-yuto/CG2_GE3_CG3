#include "Object3d.hlsli"

struct TransformationMatrix
{
    float4x4 WVP;
};

cbuffer gTransformationMatrix : register(b0)
{
    TransformationMatrix gTransformationMatrixData;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    
    output.position = mul(input.position, gTransformationMatrixData.WVP);
    output.texcoord = input.texcoord; 
    
    return output;
}