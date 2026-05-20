struct TransformationMatrix
{
    float4x4 WVP;
};

// 頂点シェーダー用の定数バッファ (b0)
cbuffer gTransformationMatrix : register(b0)
{
    TransformationMatrix gTransformationMatrixData;
};

struct VertexShaderInput
{
    float4 position : POSITION0;
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    
    // 行列を使って座標変換を行う
    output.position = mul(input.position, gTransformationMatrixData.WVP);
    
    return output;
}