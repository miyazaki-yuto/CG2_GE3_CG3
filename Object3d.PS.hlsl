struct Material
{
    float4 color;
};

// ピクセルシェーダー用の定数バッファ (b0)
cbuffer gMaterial : register(b0)
{
    Material gMaterialData;
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
};

// SV_TARGET セマンティクスをつけて「色」を出力する
float4 main(VertexShaderOutput input) : SV_TARGET
{
    // C++側で設定した赤色を出力する
    return gMaterialData.color;
}