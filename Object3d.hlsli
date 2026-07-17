
// C++側のTextureVertexDataと同じ並びで受け取る、頂点シェーダーへの入力。
struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0; // 頂点バッファから法線を受け取る
};

// 頂点シェーダーからピクセルシェーダーへ補間して渡すデータ。
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

// b0: 描画物ごとの色、ライティングの有効／無効、UV変換行列。
cbuffer MaterialBuffer : register(b0)
{
    float4 gMaterialColor;
    int gEnableLighting;
    float3 gMaterialPadding;
    float4x4 gUVTransform;
}

cbuffer LightBuffer : register(b2)
{
    // b2: PrimitiveDrawerが設定する、全3D形状で共有の平行光源。
    DirectionalLight gDirectionalLight;
}
