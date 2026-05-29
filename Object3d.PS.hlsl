// Object3d.PS.hlsl
#include "Object3d.hlsli"

struct Material
{
    float4 color;
};

cbuffer gMaterial : register(b0)
{
    Material gMaterialData;
};

// --- ここから追加 ---
// テクスチャとサンプラー（ピクセルをどう読むかの設定）の宣言
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);
// --- ここまで追加 ---

float4 main(VertexShaderOutput input) : SV_TARGET
{
    // 1. テクスチャをサンプリングして色を取得
    float4 textureColor = gTexture.Sample(gSampler, input.texcoord);
    
    // 2. マテリアルの色とテクスチャの色を乗算して出力
    return gMaterialData.color * textureColor;
}