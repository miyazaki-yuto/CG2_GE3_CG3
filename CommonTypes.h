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
};

struct Color4 {
    float r, g, b, a;
};

struct Material {
    Color4 color;                  // 16バイト
    int32_t enableLighting;        // 4バイト
    float padding[3];              // 12バイト（16バイト境界に揃える）
    Matrix4x4 uvTransform;         // 64バイト（テクスチャ座標の変換行列）
};

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

// b2へまとめて渡す、シーン共通のライトデータ。
struct LightingData
{
    DirectionalLight directionalLight;
    PointLight pointLight;
};

static_assert(sizeof(DirectionalLight) == 48);
static_assert(sizeof(PointLight) == 48);
static_assert(sizeof(LightingData) == 96);
