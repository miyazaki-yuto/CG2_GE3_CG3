#pragma once
#include "Matrix4x4.h" 

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
};
