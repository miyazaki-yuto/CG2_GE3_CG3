#pragma once

#include "CommonTypes.h"
#include "RendererComponent.h"

#include <array>
#include <cstddef>
#include <string>
#include <utility>

class PrimitiveDrawer;

// PrimitiveDrawerを利用して、三角形または球を描画するComponent。
// 座標は所有者GameObjectのTransform、見た目はこのComponentが管理する。
class PrimitiveRendererComponent final : public RendererComponent {
public:
    enum class PrimitiveType {
        Triangle,
        Sphere
    };

    static constexpr std::size_t kTriangleVertexCount = 3;
    using TriangleVertices =
        std::array<TextureVertexData, kTriangleVertexCount>;

    PrimitiveRendererComponent(
        PrimitiveDrawer* primitiveDrawer,
        PrimitiveType primitiveType,
        int textureHandle,
        std::string textureGuid = {});
    ~PrimitiveRendererComponent() override = default;

    PrimitiveRendererComponent(const PrimitiveRendererComponent&) = delete;
    PrimitiveRendererComponent& operator=(
        const PrimitiveRendererComponent&) = delete;

    void Render() override;
    void RenderShadow(const Matrix4x4& lightViewProjection) override;

    PrimitiveType GetPrimitiveType() const { return primitiveType_; }
    void SetPrimitiveType(PrimitiveType primitiveType) {
        primitiveType_ = primitiveType;
    }

    TriangleVertices& GetTriangleVertices() { return triangleVertices_; }
    const TriangleVertices& GetTriangleVertices() const {
        return triangleVertices_;
    }
    void SetTriangleVertices(const TriangleVertices& vertices) {
        triangleVertices_ = vertices;
    }

    int GetTextureHandle() const { return textureHandle_; }
    const std::string& GetTextureGuid() const { return textureGuid_; }
    void SetTextureHandle(int textureHandle) {
        textureHandle_ = textureHandle;
        textureGuid_.clear();
    }
    void SetTextureAsset(int textureHandle, std::string textureGuid) {
        textureHandle_ = textureHandle;
        textureGuid_ = std::move(textureGuid);
    }

    Vector4& GetColor() { return color_; }
    const Vector4& GetColor() const { return color_; }
    void SetColor(const Vector4& color) { color_ = color; }

    UVTransform& GetUVTransform() { return uvTransform_; }
    const UVTransform& GetUVTransform() const { return uvTransform_; }
    void SetUVTransform(const UVTransform& uvTransform) {
        uvTransform_ = uvTransform;
    }

private:
    // PrimitiveDrawerはGraphicsが所有するため、ここでは解放しない。
    PrimitiveDrawer* primitiveDrawer_ = nullptr;
    PrimitiveType primitiveType_ = PrimitiveType::Triangle;
    int textureHandle_ = -1;
    std::string textureGuid_;

    // 三角形を選んだときに使うローカル頂点。球の頂点はPrimitiveDrawerが共有する。
    TriangleVertices triangleVertices_ = {
        TextureVertexData{
            { -0.5f, -0.5f, 0.0f, 1.0f },
            { 0.0f, 1.0f },
            { 0.0f, 0.0f, -1.0f } },
        TextureVertexData{
            { 0.0f, 0.5f, 0.0f, 1.0f },
            { 0.5f, 0.0f },
            { 0.0f, 0.0f, -1.0f } },
        TextureVertexData{
            { 0.5f, -0.5f, 0.0f, 1.0f },
            { 1.0f, 1.0f },
            { 0.0f, 0.0f, -1.0f } }
    };

    Vector4 color_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    UVTransform uvTransform_ = {
        { 1.0f, 1.0f }, 0.0f, { 0.0f, 0.0f }
    };
};
