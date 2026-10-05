#include "SpriteRendererComponent.h"

#include "GameObject.h"
#include "Sprite.h"

SpriteRendererComponent::SpriteRendererComponent(
    Sprite* sprite,
    int textureHandle,
    std::string textureGuid)
    : sprite_(sprite),
      textureHandle_(textureHandle),
      textureGuid_(std::move(textureGuid)) {
    // 2D UIはすべての3D描画が終わった後へ重ねる。
    SetRenderOrder(kOverlayRenderOrder);
}

void SpriteRendererComponent::Render() {
    GameObject* owner = GetOwner();
    if (owner == nullptr || sprite_ == nullptr || textureHandle_ < 0) {
        return;
    }

    sprite_->Draw(
        owner->GetTransform().GetWorldMatrix(),
        color_,
        textureHandle_,
        uvTransform_,
        GetBlendMode());
}
