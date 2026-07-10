
// 頂点シェーダーへの入力
struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0; // 頂点バッファから法線を受け取る
};

// 頂点シェーダーからの出力
struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0; // ピクセルシェーダーへ法線を渡す
};

struct DirectionalLight
{
    float4 color; 
    float3 direction;
    float intensity;
};

// 定数バッファ b0 をマテリアル用とする
cbuffer MaterialBuffer : register(b0)
{
    float4 gMaterialColor;
    int gEnableLighting;
}

cbuffer LightBuffer : register(b2)
{
    DirectionalLight gDirectionalLight;
}