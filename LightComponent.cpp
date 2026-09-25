#include "LightComponent.h"

#include "GameObject.h"
#include "LightingManager.h"
#include "Matrix4x4.h"

LightComponent::LightComponent(
    LightingManager* lightingManager,
    LightType lightType)
    : lightingManager_(lightingManager),
      lightType_(lightType) {
    InitializeDefaultValues();
}

void LightComponent::Awake() {
    RegisterLight();
}

void LightComponent::OnEnable() {
    RegisterLight();
    ApplyLight();
}

void LightComponent::OnDisable() {
    UnregisterLight();
}

void LightComponent::Update(float /*deltaTime*/) {
    ApplyLight();
}

void LightComponent::OnDestroy() {
    UnregisterLight();
}

void LightComponent::SetLightType(LightType lightType) {
    if (lightType_ == lightType) {
        return;
    }

    // 種類ごとにスロット配列が異なるため、古い登録を解放して取り直す。
    UnregisterLight();
    lightType_ = lightType;
    InitializeDefaultValues();
    RegisterLight();
}

void LightComponent::ApplyLight() {
	// Componentが無効な間はGPU用ライト配列へ再登録しない。
	// Inspectorは無効なComponentも表示するため、ここでも状態を確認する。
	if (!IsEnabled() || lightingManager_ == nullptr || GetOwner() == nullptr) {
		return;
	}

    // 上限到達時に登録できなかったComponentは、空きができたフレームで再試行する。
    if (!IsRegistered()) {
        RegisterLight();
        return;
    }

    if (lightType_ == LightType::Directional) {
        DirectionalLight light{};
        light.color = color_;
        light.direction = GetDirection();
        light.intensity = intensity_;
        light.enabled = lightEnabled_ ? 1 : 0;
        lightingManager_->UpdateDirectionalLight(lightHandle_, light);
        return;
    }

    PointLight light{};
    light.color = color_;
    light.position = GetOwner()->GetTransform().GetWorldPosition();
    light.intensity = intensity_;
    light.radius = radius_;
    light.decay = decay_;
    light.enabled = lightEnabled_ ? 1 : 0;
    lightingManager_->UpdatePointLight(lightHandle_, light);
}

Vector3 LightComponent::GetDirection() const {
    if (GetOwner() == nullptr) {
        return { 0.0f, -1.0f, 0.0f };
    }

    const Matrix4x4 worldMatrix =
        GetOwner()->GetTransform().GetWorldMatrix();
    const Vector3 worldOrigin = Transform(
        { 0.0f, 0.0f, 0.0f }, worldMatrix);
    const Vector3 worldForward = Transform(
        { 0.0f, 0.0f, 1.0f }, worldMatrix);

    // 2点の差を取ることで平行移動を除外し、親の回転・拡縮だけを方向へ反映する。
    Vector3 direction = worldForward - worldOrigin;
    direction.Normalize();
    return direction;
}

void LightComponent::InitializeDefaultValues() {
    color_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    intensity_ = 1.0f;
    radius_ = 10.0f;
    decay_ = 2.0f;

    if (lightType_ == LightType::Directional) {
        lightEnabled_ = false;
        return;
    }

    // BlenderのPoint Lightに近い、少し暖色の初期値。
    color_ = { 1.0f, 0.9f, 0.75f, 1.0f };
    intensity_ = 2.0f;
    lightEnabled_ = true;
}

void LightComponent::RegisterLight() {
	// 無効なComponentはライトの登録枠を消費しない。
	if (!IsEnabled() || lightingManager_ == nullptr || GetOwner() == nullptr ||
		IsRegistered()) {
		return;
	}

    if (lightType_ == LightType::Directional) {
        DirectionalLight light{};
        light.color = color_;
        light.direction = GetDirection();
        light.intensity = intensity_;
        light.enabled = lightEnabled_ ? 1 : 0;
        lightHandle_ = lightingManager_->RegisterDirectionalLight(light);
        return;
    }

    PointLight light{};
    light.color = color_;
    light.position = GetOwner()->GetTransform().GetWorldPosition();
    light.intensity = intensity_;
    light.radius = radius_;
    light.decay = decay_;
    light.enabled = lightEnabled_ ? 1 : 0;
    lightHandle_ = lightingManager_->RegisterPointLight(light);
}

void LightComponent::UnregisterLight() {
    if (lightingManager_ == nullptr || !IsRegistered()) {
        lightHandle_ = LightingManager::kInvalidLightHandle;
        return;
    }

    if (lightType_ == LightType::Directional) {
        lightingManager_->UnregisterDirectionalLight(lightHandle_);
    } else {
        lightingManager_->UnregisterPointLight(lightHandle_);
    }
    lightHandle_ = LightingManager::kInvalidLightHandle;
}
