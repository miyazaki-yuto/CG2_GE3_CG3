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
    Color4 color;             // 16バイト 
    int32_t enableLighting;   // 4バイト
    float padding[3];         // 12バイト 
};

struct DirectionalLight
{
    Color4 color;
    Vector3 direction;
    float intensity;
};