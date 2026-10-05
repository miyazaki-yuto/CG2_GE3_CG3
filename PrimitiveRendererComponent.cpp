#include "PrimitiveRendererComponent.h"

#include "GameObject.h"
#include "PrimitiveDrawer.h"

PrimitiveRendererComponent::PrimitiveRendererComponent(
    PrimitiveDrawer* primitiveDrawer,
    PrimitiveType primitiveType,
    int textureHandle,
    std::string textureGuid)
    : primitiveDrawer_(primitiveDrawer),
      primitiveType_(primitiveType),
      textureHandle_(textureHandle),
      textureGuid_(std::move(textureGuid)) {
    SetRenderOrder(kOpaqueRenderOrder);
}

void PrimitiveRendererComponent::Render() {
    GameObject* owner = GetOwner();
    if (owner == nullptr || primitiveDrawer_ == nullptr ||
        textureHandle_ < 0) {
        return;
    }

    // TransformをComponent内に複製せず、GameObjectが持つ座標を描画に使用する。
    const Matrix4x4 worldMatrix =
        owner->GetTransform().GetWorldMatrix();

    switch (primitiveType_) {
    case PrimitiveType::Triangle:
        primitiveDrawer_->DrawTriangle(
            triangleVertices_.data(),
            worldMatrix,
            color_,
            textureHandle_,
            uvTransform_,
            GetBlendMode());
        break;

    case PrimitiveType::Sphere:
        primitiveDrawer_->DrawSphere(
            worldMatrix,
            color_,
            textureHandle_,
            uvTransform_,
            GetBlendMode());
        break;
    }
}

void PrimitiveRendererComponent::RenderShadow(
    const Matrix4x4& lightViewProjection) {
    GameObject* owner = GetOwner();
    if (owner == nullptr || primitiveDrawer_ == nullptr) {
        return;
    }

    const Matrix4x4 worldMatrix =
        owner->GetTransform().GetWorldMatrix();
    switch (primitiveType_) {
    case PrimitiveType::Triangle:
        primitiveDrawer_->DrawTriangleShadow(
            triangleVertices_.data(), worldMatrix, lightViewProjection);
        break;
    case PrimitiveType::Sphere:
        primitiveDrawer_->DrawSphereShadow(
            worldMatrix, lightViewProjection);
        break;
    }
}
