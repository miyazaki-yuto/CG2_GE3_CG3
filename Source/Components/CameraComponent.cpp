#include "CameraComponent.h"

#include "DebugCamera.h"
#include "GameObject.h"
#include "InputManager.h"
#include "LightingManager.h"

#include <algorithm>
#include <cmath>

CameraComponent::CameraComponent(
    DebugCamera* camera,
    InputManager* inputManager,
    LightingManager* lightingManager)
    : camera_(camera),
      inputManager_(inputManager),
      lightingManager_(lightingManager) {
    CaptureCameraState();
}

void CameraComponent::Awake() {
    SynchronizeTransformAndLighting();
}

void CameraComponent::Update(float /*deltaTime*/) {
    if (camera_ == nullptr) {
        return;
    }

    // A loaded Camera keeps its serialized orbit state on the first frame.
    // Afterwards the GameObject Transform is the source pose, so scripts can
    // move and rotate the camera during Play Mode.
    if (!applyStoredStateOnNextUpdate_) {
        CaptureOwnerTransform();
    }
    ApplyStoredState();
    applyStoredStateOnNextUpdate_ = false;

    // InputManagerが取得した1フレーム分の入力を、Blender風カメラへ渡す。
    // ImGui操作中はmouseControlEnabled_をfalseにして誤操作を防ぐ。
    if (inputEnabled_ && inputManager_ != nullptr) {
        camera_->Update(*inputManager_, mouseControlEnabled_);
    }

    CaptureCameraState();
    SynchronizeTransformAndLighting();
}

void CameraComponent::SetCameraState(
    const Vector3& target,
    float yaw,
    float pitch,
    float distance,
    bool isOrthographic,
    float fovY,
    float nearClip,
    float farClip) {
    target_ = target;
    yaw_ = yaw;
    pitch_ = pitch;
    distance_ = (std::max)(distance, 0.1f);
    isOrthographic_ = isOrthographic;
    fovY_ = (std::max)(fovY, 0.01f);
    nearClip_ = (std::max)(nearClip, 0.001f);
    farClip_ = (std::max)(farClip, nearClip_ + 0.001f);
    applyStoredStateOnNextUpdate_ = true;
}

void CameraComponent::SetProjectionSettings(
    bool isOrthographic,
    float fovY,
    float nearClip,
    float farClip) {
    isOrthographic_ = isOrthographic;
    fovY_ = (std::max)(fovY, 0.01f);
    nearClip_ = (std::max)(nearClip, 0.001f);
    farClip_ = (std::max)(farClip, nearClip_ + 0.001f);
    applyStoredStateOnNextUpdate_ = true;
}

void CameraComponent::CaptureCurrentView() {
    if (camera_ == nullptr) {
        return;
    }
    CaptureCameraState();
    applyStoredStateOnNextUpdate_ = true;
    SynchronizeTransformAndLighting();
}

void CameraComponent::CaptureOwnerTransform() {
    GameObject* owner = GetOwner();
    if (owner == nullptr) {
        return;
    }

    const Matrix4x4 world = owner->GetTransform().GetWorldMatrix();
    Vector3 forward = {
        world.m[2][0], world.m[2][1], world.m[2][2]
    };
    forward.Normalize();
    if (forward.Length() <= 0.000001f) {
        forward = { 0.0f, 0.0f, 1.0f };
    }

    const Vector3 position = owner->GetTransform().GetWorldPosition();
    target_ = position + forward * distance_;
    pitch_ = -std::asin((std::clamp)(forward.y, -1.0f, 1.0f));
    yaw_ = std::atan2(-forward.x, forward.z);
    applyStoredStateOnNextUpdate_ = true;
}

void CameraComponent::Reset() {
    if (camera_ == nullptr) {
        return;
    }

    camera_->Reset();
    CaptureCameraState();
    applyStoredStateOnNextUpdate_ = false;
    SynchronizeTransformAndLighting();
}

void CameraComponent::ApplyStoredState() {
    if (camera_ == nullptr) {
        return;
    }
    camera_->SetState(
        target_,
        yaw_,
        pitch_,
        distance_,
        isOrthographic_,
        fovY_,
        nearClip_,
        farClip_);
}

void CameraComponent::CaptureCameraState() {
    if (camera_ == nullptr) {
        return;
    }
    target_ = camera_->GetTarget();
    yaw_ = camera_->GetYaw();
    pitch_ = camera_->GetPitch();
    distance_ = camera_->GetDistance();
    isOrthographic_ = camera_->IsOrthographic();
    fovY_ = camera_->GetFovY();
    nearClip_ = camera_->GetNearClip();
    farClip_ = camera_->GetFarClip();
}

void CameraComponent::SynchronizeTransformAndLighting() {
    if (camera_ == nullptr) {
        return;
    }

    GameObject* owner = GetOwner();
    if (owner != nullptr) {
        TransformComponent& transform = owner->GetTransform();

        // DebugCameraはtarget中心の軌道カメラなので、計算済みの位置と角度を
        // GameObject側へ書き戻し、Hierarchy上でも状態を確認できるようにする。
        transform.SetWorldPosition(camera_->GetPosition());
        transform.SetLocalRotation({
            camera_->GetPitch(), -camera_->GetYaw(), 0.0f
        });
    }

    if (lightingManager_ != nullptr) {
        // スペキュラ計算では、表面からカメラへ向かう方向が必要になる。
        lightingManager_->SetCameraPosition(camera_->GetPosition());
    }
}
