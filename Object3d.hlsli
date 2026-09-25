
// C++側のTextureVertexDataと同じ並びで受け取る、頂点シェーダーへの入力。
struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0; // 頂点バッファから法線を受け取る
    float4 tangent : TANGENT0;
};

// 頂点シェーダーからピクセルシェーダーへ補間して渡すデータ。
struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0; // ピクセルシェーダーへ法線を渡す
    float4 tangent : TANGENT0;
    // Point Lightとの距離と方向を計算するため、変換後のワールド座標も渡す。
    float3 worldPosition : TEXCOORD1;
};

struct DirectionalLight
{
    float4 color; 
    float3 direction;
    float intensity;
    int enabled;
    float3 padding;
};

struct PointLight
{
    float4 color;
    float3 position;
    float intensity;
    float radius;
    float decay;
    int enabled;
    float padding;
};

struct BounceLight
{
    float4 color;
    float3 position;
    float intensity;
    float radius;
    float decay;
    int enabled;
    float padding;
};

// CommonTypes.hの上限値と必ず一致させる。
static const int kMaxDirectionalLights = 4;
static const int kMaxPointLights = 16;
static const int kMaxBounceLights = 3;
static const int kLightingModeLambert = 0;
static const int kLightingModeHalfLambert = 1;
static const int kLightingModeCurrent = 2;
static const int kLightingModePBR = 3;

// b0: 描画物ごとの色、ライティングの有効／無効、UV変換行列。
cbuffer MaterialBuffer : register(b0)
{
    float4 gMaterialColor;
    int gEnableLighting;
    float3 gMaterialPadding;
    float4x4 gUVTransform;
    float4 gMaterialSpecularColor;
    float gMaterialSpecularShininess;
    int gNormalMapEnabled;
    float gMaterialMetallic;
    float gMaterialRoughness;
}

cbuffer LightBuffer : register(b2)
{
    // b2: LightingManagerが設定し、全ての描画クラスで共有するライト。
    DirectionalLight gDirectionalLights[kMaxDirectionalLights];
    PointLight gPointLights[kMaxPointLights];
    // Blinn-Phongスペキュラに必要なシーン共通パラメーター。
    float3 gCameraPosition;
    float gSpecularStrength;
    float gSpecularShininess;
    int gLightingMode;
    float2 gLightingPadding;
    float gEnvironmentIntensity;
    float gEnvironmentRotation;
    int gEnvironmentEnabled;
    float gEnvironmentPadding;
    float4x4 gDirectionalShadowViewProjections[4];
    float4 gDirectionalShadowCascadeSplits;
    float2 gDirectionalShadowTexelSize;
    float gDirectionalShadowBias;
    int gDirectionalShadowEnabled;
    int gDirectionalShadowLightIndex;
    int gDirectionalShadowCascadeCount;
    float2 gDirectionalShadowPadding;
    float3 gPointShadowPosition;
    float gPointShadowNearClip;
    float gPointShadowFarClip;
    float gPointShadowBias;
    float gPointShadowTexelSize;
    int gPointShadowEnabled;
    int gPointShadowLightIndex;
    float3 gPointShadowPadding;
    BounceLight gBounceLights[kMaxBounceLights];
    int gBounceLightCount;
    float3 gBounceLightPadding;
}
