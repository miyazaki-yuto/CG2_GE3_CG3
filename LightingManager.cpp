#include "LightingManager.h"

#include "DirectXCommon.h"

#include <algorithm>
#include <cassert>

namespace {

DirectionalLight SanitizeDirectionalLight(const DirectionalLight& light) {
    DirectionalLight result = light;
    if (result.direction.Length() <= 0.0001f) {
        result.direction = { 0.0f, -1.0f, 0.0f };
    } else {
        result.direction.Normalize();
    }
    result.intensity = (std::max)(result.intensity, 0.0f);
    result.enabled = result.enabled != 0 ? 1 : 0;
    result.padding[0] = 0.0f;
    result.padding[1] = 0.0f;
    result.padding[2] = 0.0f;
    return result;
}

PointLight SanitizePointLight(const PointLight& light) {
    PointLight result = light;
    result.intensity = (std::max)(result.intensity, 0.0f);
    result.radius = (std::max)(result.radius, 0.001f);
    result.decay = (std::max)(result.decay, 0.001f);
    result.enabled = result.enabled != 0 ? 1 : 0;
    result.padding = 0.0f;
    return result;
}

SpotLight SanitizeSpotLight(const SpotLight& light) {
    SpotLight result = light;
    if (result.direction.Length() <= 0.0001f) {
        result.direction = { 0.0f, -1.0f, 0.0f };
    } else {
        result.direction.Normalize();
    }
    result.intensity = (std::max)(result.intensity, 0.0f);
    result.radius = (std::max)(result.radius, 0.001f);
    result.decay = (std::max)(result.decay, 0.001f);
    result.cosOuterAngle = (std::clamp)(result.cosOuterAngle, -1.0f, 1.0f);
    result.cosInnerAngle = (std::clamp)(result.cosInnerAngle, -1.0f, 1.0f);
    result.cosInnerAngle = (std::max)(
        result.cosInnerAngle, result.cosOuterAngle);
    result.enabled = result.enabled != 0 ? 1 : 0;
    return result;
}

bool IsHandleInRange(
    LightingManager::LightHandle handle,
    uint32_t capacity) {
    return handle >= 0 && static_cast<uint32_t>(handle) < capacity;
}

} // namespace

void LightingManager::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon != nullptr);
    dxCommon_ = dxCommon;

    // ライト実体はLightComponentが登録する。Manager初期化時は全スロットを空にする。
    lightingData_ = {};
    directionalLightSlots_.fill(false);
    pointLightSlots_.fill(false);
    spotLightSlots_.fill(false);

    SetCameraPosition({ 0.0f, 0.0f, -10.0f });
    SetLightingMode(LightingMode::Current);
    SetSpecularStrength(0.5f);
    SetSpecularShininess(32.0f);
    SetEnvironmentEnabled(false);
    SetEnvironmentIntensity(0.25f);
    SetEnvironmentRotation(0.0f);
    SetDirectionalShadow(
        nullptr, { 10.0f, 25.0f, 60.0f, 120.0f },
        false, 0, { 1.0f, 1.0f }, 0.0015f);
    SetPointShadow(
        { 0.0f, 0.0f, 0.0f }, false, 0, 0.1f, 1.0f, 0.002f,
        1.0f / 1024.0f);
}

void LightingManager::BeginFrame() {
    assert(dxCommon_ != nullptr);

    // BounceLightは移動するモデルから、そのフレームだけ生成される一時ライト。
    ClearBounceLights();
    lightingGpuAddress_ = 0;
}

LightingManager::LightHandle LightingManager::RegisterDirectionalLight(
    const DirectionalLight& light) {
    for (uint32_t index = 0; index < kMaxDirectionalLights; ++index) {
        if (directionalLightSlots_[index]) {
            continue;
        }

        directionalLightSlots_[index] = true;
        lightingData_.directionalLights[index] =
            SanitizeDirectionalLight(light);
        lightingGpuAddress_ = 0;
        return static_cast<LightHandle>(index);
    }

    return kInvalidLightHandle;
}

bool LightingManager::UpdateDirectionalLight(
    LightHandle handle,
    const DirectionalLight& light) {
    if (!IsHandleInRange(handle, kMaxDirectionalLights) ||
        !directionalLightSlots_[handle]) {
        return false;
    }

    lightingData_.directionalLights[handle] =
        SanitizeDirectionalLight(light);
    lightingGpuAddress_ = 0;
    return true;
}

void LightingManager::UnregisterDirectionalLight(LightHandle handle) {
    if (!IsHandleInRange(handle, kMaxDirectionalLights) ||
        !directionalLightSlots_[handle]) {
        return;
    }

    directionalLightSlots_[handle] = false;
    lightingData_.directionalLights[handle] = {};
    lightingGpuAddress_ = 0;
}

const DirectionalLight* LightingManager::GetDirectionalLight(
    LightHandle handle) const {
    if (!IsHandleInRange(handle, kMaxDirectionalLights) ||
        !directionalLightSlots_[handle]) {
        return nullptr;
    }
    return &lightingData_.directionalLights[handle];
}

LightingManager::LightHandle LightingManager::RegisterPointLight(
    const PointLight& light) {
    for (uint32_t index = 0; index < kMaxPointLights; ++index) {
        if (pointLightSlots_[index]) {
            continue;
        }

        pointLightSlots_[index] = true;
        lightingData_.pointLights[index] = SanitizePointLight(light);
        lightingGpuAddress_ = 0;
        return static_cast<LightHandle>(index);
    }

    return kInvalidLightHandle;
}

bool LightingManager::UpdatePointLight(
    LightHandle handle,
    const PointLight& light) {
    if (!IsHandleInRange(handle, kMaxPointLights) ||
        !pointLightSlots_[handle]) {
        return false;
    }

    lightingData_.pointLights[handle] = SanitizePointLight(light);
    lightingGpuAddress_ = 0;
    return true;
}

void LightingManager::UnregisterPointLight(LightHandle handle) {
    if (!IsHandleInRange(handle, kMaxPointLights) ||
        !pointLightSlots_[handle]) {
        return;
    }

    pointLightSlots_[handle] = false;
    lightingData_.pointLights[handle] = {};
    lightingGpuAddress_ = 0;
}

const PointLight* LightingManager::GetPointLight(
    LightHandle handle) const {
    if (!IsHandleInRange(handle, kMaxPointLights) ||
        !pointLightSlots_[handle]) {
        return nullptr;
    }
    return &lightingData_.pointLights[handle];
}

LightingManager::LightHandle LightingManager::RegisterSpotLight(
    const SpotLight& light) {
    for (uint32_t index = 0; index < kMaxSpotLights; ++index) {
        if (spotLightSlots_[index]) {
            continue;
        }
        spotLightSlots_[index] = true;
        lightingData_.spotLights[index] = SanitizeSpotLight(light);
        lightingGpuAddress_ = 0;
        return static_cast<LightHandle>(index);
    }
    return kInvalidLightHandle;
}

bool LightingManager::UpdateSpotLight(
    LightHandle handle,
    const SpotLight& light) {
    if (!IsHandleInRange(handle, kMaxSpotLights) ||
        !spotLightSlots_[handle]) {
        return false;
    }
    lightingData_.spotLights[handle] = SanitizeSpotLight(light);
    lightingGpuAddress_ = 0;
    return true;
}

void LightingManager::UnregisterSpotLight(LightHandle handle) {
    if (!IsHandleInRange(handle, kMaxSpotLights) ||
        !spotLightSlots_[handle]) {
        return;
    }
    spotLightSlots_[handle] = false;
    lightingData_.spotLights[handle] = {};
    lightingGpuAddress_ = 0;
}

const SpotLight* LightingManager::GetSpotLight(LightHandle handle) const {
    if (!IsHandleInRange(handle, kMaxSpotLights) ||
        !spotLightSlots_[handle]) {
        return nullptr;
    }
    return &lightingData_.spotLights[handle];
}

uint32_t LightingManager::GetDirectionalLightCount() const {
    return static_cast<uint32_t>(std::count(
        directionalLightSlots_.begin(),
        directionalLightSlots_.end(),
        true));
}

uint32_t LightingManager::GetPointLightCount() const {
    return static_cast<uint32_t>(std::count(
        pointLightSlots_.begin(),
        pointLightSlots_.end(),
        true));
}

uint32_t LightingManager::GetSpotLightCount() const {
    return static_cast<uint32_t>(std::count(
        spotLightSlots_.begin(), spotLightSlots_.end(), true));
}

bool LightingManager::GetFirstEnabledDirectionalLight(
    DirectionalLight& light,
    uint32_t& lightIndex) const {
    for (uint32_t index = 0; index < kMaxDirectionalLights; ++index) {
        if (!directionalLightSlots_[index] ||
            lightingData_.directionalLights[index].enabled == 0) {
            continue;
        }
        light = lightingData_.directionalLights[index];
        lightIndex = index;
        return true;
    }
    return false;
}

bool LightingManager::GetFirstEnabledPointLight(
    PointLight& light,
    uint32_t& lightIndex) const {
    for (uint32_t index = 0; index < kMaxPointLights; ++index) {
        if (!pointLightSlots_[index] ||
            lightingData_.pointLights[index].enabled == 0) {
            continue;
        }
        light = lightingData_.pointLights[index];
        lightIndex = index;
        return true;
    }
    return false;
}

void LightingManager::SetCameraPosition(const Vector3& cameraPosition) {
    lightingData_.cameraPosition = cameraPosition;
    lightingGpuAddress_ = 0;
}

void LightingManager::SetLightingMode(LightingMode mode) {
    switch (mode) {
    case LightingMode::Lambert:
    case LightingMode::HalfLambert:
    case LightingMode::Current:
    case LightingMode::PBR:
        lightingData_.lightingMode = static_cast<int32_t>(mode);
        break;
    default:
        lightingData_.lightingMode =
            static_cast<int32_t>(LightingMode::Current);
        break;
    }
    lightingGpuAddress_ = 0;
}

void LightingManager::SetSpecularStrength(float strength) {
    lightingData_.specularStrength = (std::max)(strength, 0.0f);
    lightingGpuAddress_ = 0;
}

void LightingManager::SetSpecularShininess(float shininess) {
    lightingData_.specularShininess = (std::max)(shininess, 1.0f);
    lightingGpuAddress_ = 0;
}

void LightingManager::SetEnvironmentTextureAsset(
    int textureHandle,
    std::string textureGuid) {
    environmentTextureHandle_ = textureHandle;
    environmentTextureGuid_ = std::move(textureGuid);
    if (textureHandle < 0) {
        lightingData_.environmentEnabled = 0;
    }
    lightingGpuAddress_ = 0;
}

void LightingManager::SetEnvironmentEnabled(bool enabled) {
    lightingData_.environmentEnabled =
        enabled && environmentTextureHandle_ >= 0 ? 1 : 0;
    lightingGpuAddress_ = 0;
}

void LightingManager::SetEnvironmentIntensity(float intensity) {
    lightingData_.environmentIntensity = (std::max)(intensity, 0.0f);
    lightingGpuAddress_ = 0;
}

void LightingManager::SetEnvironmentRotation(float radians) {
    lightingData_.environmentRotation = radians;
    lightingGpuAddress_ = 0;
}

void LightingManager::SetDirectionalShadow(
    const Matrix4x4* viewProjections,
    const Vector4& cascadeSplits,
    bool enabled,
    uint32_t lightIndex,
    const Vector2& texelSize,
    float bias) {
    for (uint32_t cascadeIndex = 0; cascadeIndex < 4; ++cascadeIndex) {
        lightingData_.directionalShadowViewProjections[cascadeIndex] =
            viewProjections != nullptr
            ? viewProjections[cascadeIndex]
            : MakeIdentity4x4();
    }
    lightingData_.directionalShadowCascadeSplits = cascadeSplits;
    lightingData_.directionalShadowTexelSize = texelSize;
    lightingData_.directionalShadowBias = (std::max)(bias, 0.0f);
    lightingData_.directionalShadowEnabled = enabled ? 1 : 0;
    lightingData_.directionalShadowLightIndex = enabled
        ? static_cast<int32_t>(lightIndex)
        : -1;
    lightingData_.directionalShadowCascadeCount = enabled ? 4 : 0;
    lightingData_.directionalShadowPadding[0] = 0.0f;
    lightingData_.directionalShadowPadding[1] = 0.0f;
    lightingGpuAddress_ = 0;
}

void LightingManager::SetPointShadow(
    const Vector3& lightPosition,
    bool enabled,
    uint32_t lightIndex,
    float nearClip,
    float farClip,
    float bias,
    float texelSize) {
    lightingData_.pointShadowPosition = lightPosition;
    lightingData_.pointShadowNearClip =
        (std::max)(nearClip, 0.001f);
    lightingData_.pointShadowFarClip =
        (std::max)(farClip, lightingData_.pointShadowNearClip + 0.001f);
    lightingData_.pointShadowBias = (std::max)(bias, 0.0f);
    lightingData_.pointShadowTexelSize =
        (std::max)(texelSize, 0.000001f);
    lightingData_.pointShadowEnabled = enabled ? 1 : 0;
    lightingData_.pointShadowLightIndex = static_cast<int32_t>(
        (std::min)(lightIndex, kMaxPointLights - 1));
    lightingData_.pointShadowPadding[0] = 0.0f;
    lightingData_.pointShadowPadding[1] = 0.0f;
    lightingData_.pointShadowPadding[2] = 0.0f;
    lightingGpuAddress_ = 0;
}

void LightingManager::ClearBounceLights() {
    lightingData_.bounceLightCount = 0;
    lightingGpuAddress_ = 0;
}

bool LightingManager::GenerateBounceLight(
    const Vector3& position,
    const Color4& color,
    float intensity,
    float radius,
    float decay) {
    if (lightingData_.bounceLightCount >=
        static_cast<int32_t>(kMaxBounceLights)) {
        return false;
    }

    BounceLight bounceLight{};
    bounceLight.color = color;
    bounceLight.position = position;
    bounceLight.intensity = (std::max)(intensity, 0.0f);
    bounceLight.radius = (std::max)(radius, 0.001f);
    bounceLight.decay = (std::max)(decay, 0.001f);
    bounceLight.enabled = bounceLight.intensity > 0.0f ? 1 : 0;
    bounceLight.padding = 0.0f;

    lightingData_.bounceLights[lightingData_.bounceLightCount] = bounceLight;
    ++lightingData_.bounceLightCount;
    lightingGpuAddress_ = 0;
    return true;
}

D3D12_GPU_VIRTUAL_ADDRESS LightingManager::GetLightingGpuAddress() {
    assert(dxCommon_ != nullptr);
    if (lightingGpuAddress_ != 0) {
        return lightingGpuAddress_;
    }

    // 現在フレーム専用領域へ全ライト配列を1回だけコピーする。
    const DynamicBufferAllocation allocation =
        dxCommon_->AllocateDynamicBuffer(sizeof(LightingData));
    *static_cast<LightingData*>(allocation.cpuAddress) = lightingData_;
    lightingGpuAddress_ = allocation.gpuAddress;
    return lightingGpuAddress_;
}
