#pragma once

#include <d3d12.h>

#include <array>
#include <cstdint>
#include <string>
#include <utility>

#include "CommonTypes.h"

class DirectXCommon;

// シーンで共有するSunとPoint Lightを1か所で管理するクラス。
// Primitive・Model・Spriteが個別に同じ定数バッファを作る必要をなくす。
class LightingManager {
public:
    using LightHandle = int32_t;
    static constexpr LightHandle kInvalidLightHandle = -1;

    LightingManager() = default;
    ~LightingManager() = default;

    LightingManager(const LightingManager&) = delete;
    LightingManager& operator=(const LightingManager&) = delete;

    void Initialize(DirectXCommon* dxCommon);

    // フレーム開始時に、前フレームのGPUアドレスを無効化する。
    void BeginFrame();

    // LightComponentごとに専用スロットを確保する。
    // 上限に達した場合はkInvalidLightHandleを返す。
    LightHandle RegisterDirectionalLight(const DirectionalLight& light);
    bool UpdateDirectionalLight(
        LightHandle handle, const DirectionalLight& light);
    void UnregisterDirectionalLight(LightHandle handle);
    const DirectionalLight* GetDirectionalLight(LightHandle handle) const;

    LightHandle RegisterPointLight(const PointLight& light);
    bool UpdatePointLight(LightHandle handle, const PointLight& light);
    void UnregisterPointLight(LightHandle handle);
    const PointLight* GetPointLight(LightHandle handle) const;

    uint32_t GetDirectionalLightCount() const;
    uint32_t GetPointLightCount() const;
    bool GetFirstEnabledDirectionalLight(
        DirectionalLight& light,
        uint32_t& lightIndex) const;
    bool GetFirstEnabledPointLight(
        PointLight& light,
        uint32_t& lightIndex) const;

    // スペキュラ計算に使うカメラ位置と、見た目を調整する2つのパラメーター。
    void SetCameraPosition(const Vector3& cameraPosition);
    void SetLightingMode(LightingMode mode);
    LightingMode GetLightingMode() const {
        return static_cast<LightingMode>(lightingData_.lightingMode);
    }
    void SetSpecularStrength(float strength);
    float GetSpecularStrength() const { return lightingData_.specularStrength; }
    void SetSpecularShininess(float shininess);
    float GetSpecularShininess() const { return lightingData_.specularShininess; }
    void SetEnvironmentTextureAsset(
        int textureHandle,
        std::string textureGuid);
    int GetEnvironmentTextureHandle() const { return environmentTextureHandle_; }
    const std::string& GetEnvironmentTextureGuid() const {
        return environmentTextureGuid_;
    }
    void SetEnvironmentEnabled(bool enabled);
    bool IsEnvironmentEnabled() const {
        return lightingData_.environmentEnabled != 0;
    }
    void SetEnvironmentIntensity(float intensity);
    float GetEnvironmentIntensity() const {
        return lightingData_.environmentIntensity;
    }
    void SetEnvironmentRotation(float radians);
    float GetEnvironmentRotation() const {
        return lightingData_.environmentRotation;
    }
    void SetDirectionalShadow(
        const Matrix4x4* viewProjections,
        const Vector4& cascadeSplits,
        bool enabled,
        uint32_t lightIndex,
        const Vector2& texelSize,
        float bias);
    void SetPointShadow(
        const Vector3& lightPosition,
        bool enabled,
        uint32_t lightIndex,
        float nearClip,
        float farClip,
        float bias,
        float texelSize);

    // BounceLightは毎フレーム生成し直す一時ライト。
    // 上限に達していなければtrue、3個を超えた場合はfalseを返す。
    void ClearBounceLights();
    bool GenerateBounceLight(
        const Vector3& position,
        const Color4& color,
        float intensity,
        float radius,
        float decay);
    uint32_t GetBounceLightCount() const {
        return static_cast<uint32_t>(lightingData_.bounceLightCount);
    }

    // 最初のDraw時に現在値をGPUへコピーし、同一フレーム中は同じアドレスを共有する。
    D3D12_GPU_VIRTUAL_ADDRESS GetLightingGpuAddress();

private:
    DirectXCommon* dxCommon_ = nullptr;
    LightingData lightingData_{};
    // GPUへ送るenabledとは別に、CPU側でスロットの使用状態を管理する。
    std::array<bool, kMaxDirectionalLights> directionalLightSlots_{};
    std::array<bool, kMaxPointLights> pointLightSlots_{};
    int environmentTextureHandle_ = -1;
    std::string environmentTextureGuid_;
    D3D12_GPU_VIRTUAL_ADDRESS lightingGpuAddress_ = 0;
};
