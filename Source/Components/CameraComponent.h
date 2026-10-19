#pragma once

#include "Component.h"
#include "Vector3.h"

class DebugCamera;
class InputManager;
class LightingManager;

// DebugCameraをGameObjectへ接続するComponent。
// Blender風カメラ操作の結果を、所有者GameObjectのTransformへ同期する。
class CameraComponent final : public Component {
public:
    CameraComponent(
        DebugCamera* camera,
        InputManager* inputManager,
        LightingManager* lightingManager);
    ~CameraComponent() override = default;

    CameraComponent(const CameraComponent&) = delete;
    CameraComponent& operator=(const CameraComponent&) = delete;

    void Awake() override;
    void Update(float deltaTime) override;

    DebugCamera* GetCamera() const { return camera_; }

    const Vector3& GetTarget() const { return target_; }
    float GetYaw() const { return yaw_; }
    float GetPitch() const { return pitch_; }
    float GetDistance() const { return distance_; }
    float GetFovY() const { return fovY_; }
    float GetNearClip() const { return nearClip_; }
    float GetFarClip() const { return farClip_; }
    bool IsOrthographic() const { return isOrthographic_; }

    void SetCameraState(
        const Vector3& target,
        float yaw,
        float pitch,
        float distance,
        bool isOrthographic,
        float fovY,
        float nearClip,
        float farClip);
    void SetProjectionSettings(
        bool isOrthographic,
        float fovY,
        float nearClip,
        float farClip);

    // Stores the current Scene View as this component's Play camera.
    void CaptureCurrentView();
    // Rebuilds the Play camera pose after its GameObject Transform is edited.
    void CaptureOwnerTransform();

    bool IsInputEnabled() const { return inputEnabled_; }
    void SetInputEnabled(bool enabled) { inputEnabled_ = enabled; }

    bool IsMouseControlEnabled() const { return mouseControlEnabled_; }
    void SetMouseControlEnabled(bool enabled) {
        mouseControlEnabled_ = enabled;
    }

    // カメラを初期状態へ戻し、Transformと鏡面反射用カメラ座標も更新する。
    void Reset();

private:
    void ApplyStoredState();
    void CaptureCameraState();
    void SynchronizeTransformAndLighting();

    // 実体はGraphics・Engine側が所有するため、このComponentでは解放しない。
    DebugCamera* camera_ = nullptr;
    InputManager* inputManager_ = nullptr;
    LightingManager* lightingManager_ = nullptr;

    bool inputEnabled_ = true;
    bool mouseControlEnabled_ = true;

    Vector3 target_ = { 0.0f, 0.0f, 0.0f };
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
    float distance_ = 8.0f;
    float fovY_ = 0.45f;
    float nearClip_ = 0.1f;
    float farClip_ = 1000.0f;
    bool isOrthographic_ = false;
    bool applyStoredStateOnNextUpdate_ = true;
};
