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
};

struct TransformData {
    Vector3 scale;
    Vector3 rotate;
    Vector3 translate;
};

struct TransformationMatrix {
    Matrix4x4 WVP;
};

struct VertexData {
    Vector4 position;
    float uv[2];
};

struct Color4 {
    float r, g, b, a;
};