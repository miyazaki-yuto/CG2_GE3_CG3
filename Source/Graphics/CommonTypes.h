#pragma once
#include "Matrix4x4.h" 

#include <cstdint>

struct Vector2 {
    float x;
    float y;
};

struct Vector4 {
    float x;
    float y;
    float z;
    float w;
};

struct TextureVertexData {
    Vector4 position;
    Vector2 texcoord;
    Vector3 normal;
    // xyzは接線、wはBitangentを復元するための向き（+1／-1）。
    Vector4 tangent;
    // 1頂点に最大4本のボーンを影響させる。ウェイトが0の頂点は
    // 非スキニング頂点として、従来どおりの座標を使用する。
    uint32_t boneIndices[4]{};
    Vector4 boneWeights{};
};

struct TransformData {
    Vector3 scale;
    Vector3 rotate;
    Vector3 translate;
};

// テクスチャ座標(UV)専用の変換パラメータ。
// scaleで画像の繰り返し、rotateで回転、translateで表示位置を変更する。
struct UVTransform {
    Vector2 scale;
    float rotate;
    Vector2 translate;
};

// 2DのUVTransformを、シェーダーへ渡せる4x4行列へ変換する。
inline Matrix4x4 MakeUVTransformMatrix(const UVTransform& transform) {
    return MakeAffineMatrix(
        { transform.scale.x, transform.scale.y, 1.0f },
        { 0.0f, 0.0f, transform.rotate },
        { transform.translate.x, transform.translate.y, 0.0f });
}

struct TransformationMatrix {
    Matrix4x4 WVP;
    Matrix4x4 World;
    // 法線は位置と違い、非均一スケール時にWorld行列をそのまま掛けると方向が歪む。
    // Worldの逆転置行列を使うことで、面に対して垂直な向きを保つ。
    Matrix4x4 WorldInverseTranspose;
};

struct VertexData {
    Vector4 position;
    float uv[2];
    Vector3 normal;
    Vector4 tangent;
};

struct Color4 {
    float r, g, b, a;
};

struct MaterialConstants {
    Color4 color;                  // 16バイト
    int32_t enableLighting;        // 4バイト
    float padding[3];              // 12バイト（16バイト境界に揃える）
    Matrix4x4 uvTransform;         // 64バイト（テクスチャ座標の変換行列）
    Color4 specularColor;          // MTLのKs。鏡面反射の色と強さ
    float specularShininess;       // MTLのNs。0ならシーン共通値を使用
    int32_t normalMapEnabled;      // 1ならt2のNormal Mapを使用
    float metallic;                // PBRの金属度。0=非金属、1=金属
    float roughness;               // PBRの粗さ。0=鏡面、1=粗い表面
    int32_t metallicRoughnessMapEnabled; // 1ならt5のG=Roughness、B=Metallicを使用
    float pbrPadding[3];
};

static_assert(sizeof(MaterialConstants) == 144);

struct DirectionalLight
{
    Color4 color;
    Vector3 direction;
    float intensity;
    int32_t enabled;
    float padding[3];
};

// BlenderのPoint Lightに相当する、ワールド座標上へ配置できる点光源。
struct PointLight
{
    Color4 color;
    Vector3 position;
    float intensity;
    float radius;      // 光が届く最大距離
    float decay;       // 距離による減衰カーブ。大きいほど急に暗くなる
    int32_t enabled;
    float padding;
};

// 位置・向き・内外2つの角度を持つ円錐状の光源。
struct SpotLight
{
    Color4 color;
    Vector3 position;
    float intensity;
    Vector3 direction;
    float radius;
    float decay;
    float cosOuterAngle;
    float cosInnerAngle;
    int32_t enabled;
};

// モデル表面で反射した光を、弱い色付きPoint Lightとして近似する。
// GPU側のBounceLightと同じ並び・サイズにして定数バッファへそのままコピーする。
struct BounceLight
{
    Color4 color;
    Vector3 position;
    float intensity;
    float radius;
    float decay;
    int32_t enabled;
    float padding;
};

// Scene-wide diffuse and specular lighting model.
// Values must match the kLightingMode* constants in Object3d.hlsli.
enum class LightingMode : int32_t
{
    Lambert = 0,
    HalfLambert = 1,
    Current = 2,
    PBR = 3
};

// HDR Scene Textureを画面表示できる範囲へ圧縮する方式。
enum class ToneMappingMode : int32_t
{
    None = 0,
    Reinhard = 1,
    ACES = 2
};

// 画面全体へ適用するアンチエイリアス方式。
enum class AntiAliasingMode : int32_t
{
    None = 0,
    FXAA = 1,
    TAA = 2,
    MAA = 3
};

// GPU定数バッファは固定長配列にする。
// CPU側とObject3d.hlsli側で、この個数を必ず一致させること。
constexpr uint32_t kMaxDirectionalLights = 4;
constexpr uint32_t kMaxPointLights = 16;
constexpr uint32_t kMaxSpotLights = 8;
constexpr uint32_t kMaxBounceLights = 3;
constexpr uint32_t kMaxSkinningBones = 128;

// b4で頂点シェーダーへ渡すボーンパレット。
// HLSLのSkinning.hlsliと配置を必ず一致させる。
struct SkinningConstants
{
    Matrix4x4 boneMatrices[kMaxSkinningBones];
    int32_t enabled;
    uint32_t boneCount;
    float padding[2];
};

static_assert(sizeof(SkinningConstants) == 8208);

// b2へまとめて渡す、シーン共通のライトデータ。
struct LightingData
{
    DirectionalLight directionalLights[kMaxDirectionalLights];
    PointLight pointLights[kMaxPointLights];
    SpotLight spotLights[kMaxSpotLights];
    // スペキュラ計算では「表面からカメラへ向かう方向」が必要になる。
    Vector3 cameraPosition;
    float specularStrength; // ハイライトの明るさ。0でスペキュラを無効化する。
    float specularShininess; // 大きいほどハイライトが小さく鋭くなる。
    int32_t lightingMode;
    float padding[2];
    // Sky Sphere画像を経緯度環境マップとして利用する簡易IBL設定。
    float environmentIntensity;
    float environmentRotation;
    int32_t environmentEnabled;
    float environmentPadding;
    // Directional Light shadow data. The matrix uses the same row-vector
    // convention as the rest of the renderer.
    Matrix4x4 directionalShadowViewProjections[4];
    Vector4 directionalShadowCascadeSplits;
    Vector2 directionalShadowTexelSize;
    float directionalShadowBias;
    int32_t directionalShadowEnabled;
    int32_t directionalShadowLightIndex;
    int32_t directionalShadowCascadeCount;
    float directionalShadowPadding[2];
    Vector3 pointShadowPosition;
    float pointShadowNearClip;
    float pointShadowFarClip;
    float pointShadowBias;
    float pointShadowTexelSize;
    int32_t pointShadowEnabled;
    int32_t pointShadowLightIndex;
    float pointShadowPadding[3];
    BounceLight bounceLights[kMaxBounceLights];
    int32_t bounceLightCount;
    float bounceLightPadding[3];
};

static_assert(sizeof(DirectionalLight) == 48);
static_assert(sizeof(PointLight) == 48);
static_assert(sizeof(SpotLight) == 64);
static_assert(sizeof(BounceLight) == 48);
// HLSLのLightBufferも同じ配置にする。
static_assert(sizeof(LightingData) == 2032);
