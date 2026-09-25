#pragma once

#include "CommonTypes.h"
#include "Component.h"

class LightingManager;

// GameObjectのTransformを使って配置できるライトComponent。
// Directionalは回転、Pointは位置をLightingManagerへ反映する。
class LightComponent final : public Component {
public:
    enum class LightType {
        Directional,
        Point
    };

    LightComponent(LightingManager* lightingManager, LightType lightType);
    ~LightComponent() override = default;

    LightComponent(const LightComponent&) = delete;
    LightComponent& operator=(const LightComponent&) = delete;

    void Awake() override;
    void OnEnable() override;
    void OnDisable() override;
    void Update(float deltaTime) override;
    void OnDestroy() override;

    LightType GetLightType() const { return lightType_; }
    void SetLightType(LightType lightType);

    int32_t GetLightHandle() const { return lightHandle_; }
    bool IsRegistered() const { return lightHandle_ >= 0; }

    bool IsLightEnabled() const { return lightEnabled_; }
    void SetLightEnabled(bool enabled) { lightEnabled_ = enabled; }

    Color4& GetColor() { return color_; }
    const Color4& GetColor() const { return color_; }
    void SetColor(const Color4& color) { color_ = color; }

    float GetIntensity() const { return intensity_; }
    void SetIntensity(float intensity) { intensity_ = intensity; }

    float GetRadius() const { return radius_; }
    void SetRadius(float radius) { radius_ = radius; }

    float GetDecay() const { return decay_; }
    void SetDecay(float decay) { decay_ = decay; }

    // ImGuiで値を変更した直後など、Updateを待たず反映したい場合に使用する。
    void ApplyLight();

    // Directional Lightが現在向いているワールド方向を取得する。
    Vector3 GetDirection() const;

private:
    void InitializeDefaultValues();
    void RegisterLight();
    void UnregisterLight();

    // LightingManagerの実体はGraphicsが所有する。
    LightingManager* lightingManager_ = nullptr;
    LightType lightType_ = LightType::Point;
    int32_t lightHandle_ = -1;
    bool lightEnabled_ = true;
    Color4 color_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    float intensity_ = 1.0f;
    float radius_ = 10.0f;
    float decay_ = 2.0f;
};
