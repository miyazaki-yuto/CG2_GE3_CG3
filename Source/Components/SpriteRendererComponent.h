#pragma once

#include "CommonTypes.h"
#include "RendererComponent.h"

#include <string>
#include <utility>

class Sprite;

// GameObjectのTransformをピクセル座標としてSpriteを描画するComponent。
// Sprite本体はGraphicsが共有所有するため、このComponentは非所有ポインタだけを保持する。
class SpriteRendererComponent final : public RendererComponent {
public:
    SpriteRendererComponent(
        Sprite* sprite,
        int textureHandle,
        std::string textureGuid = {});
    ~SpriteRendererComponent() override = default;

    SpriteRendererComponent(const SpriteRendererComponent&) = delete;
    SpriteRendererComponent& operator=(const SpriteRendererComponent&) = delete;

    void Render() override;

    Sprite* GetSprite() const { return sprite_; }
    void SetSprite(Sprite* sprite) { sprite_ = sprite; }

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
    Sprite* sprite_ = nullptr;
    int textureHandle_ = -1;
    std::string textureGuid_;
    Vector4 color_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    UVTransform uvTransform_ = {
        { 1.0f, 1.0f }, 0.0f, { 0.0f, 0.0f }
    };
};
