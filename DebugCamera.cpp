#include "DebugCamera.h"

#include "InputManager.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kOrbitSensitivity = 0.005f;
constexpr float kDollySensitivity = 0.01f;
constexpr float kMinimumDistance = 0.1f;
constexpr float kMaximumDistance = 500.0f;
// 真上・真下では外積から右方向を求められないため、90度よりわずかに小さく制限する。
constexpr float kPitchLimit = kPi / 2.0f - 0.001f;

Vector3 NormalizeCopy(Vector3 vector) {
    vector.Normalize();
    return vector;
}

bool IsShiftPressed(const InputManager& inputManager) {
    return inputManager.IsKeyPressed(VK_SHIFT) ||
        inputManager.IsKeyPressed(VK_LSHIFT) ||
        inputManager.IsKeyPressed(VK_RSHIFT);
}

bool IsControlPressed(const InputManager& inputManager) {
    return inputManager.IsKeyPressed(VK_CONTROL) ||
        inputManager.IsKeyPressed(VK_LCONTROL) ||
        inputManager.IsKeyPressed(VK_RCONTROL);
}

} // namespace

void DebugCamera::Initialize(uint32_t windowWidth, uint32_t windowHeight) {
    assert(windowWidth > 0);
    assert(windowHeight > 0);

    windowWidth_ = windowWidth;
    windowHeight_ = windowHeight;
    Reset();
}

void DebugCamera::Resize(uint32_t windowWidth, uint32_t windowHeight) {
    assert(windowWidth > 0);
    assert(windowHeight > 0);
    windowWidth_ = windowWidth;
    windowHeight_ = windowHeight;
    UpdateMatrices();
}

void DebugCamera::Reset() {
    target_ = { 0.0f, 0.0f, 0.0f };
    yaw_ = 0.0f;
    pitch_ = 0.0f;
    distance_ = 8.0f;
    isOrthographic_ = false;
    UpdateMatrices();
}

void DebugCamera::Update(const InputManager& inputManager, bool allowMouseControl) {
    const bool isControlPressed = IsControlPressed(inputManager);

    // Blenderと同様にテンキーで決まった方向へ切り替える。
    // Ctrlを同時に押すと、正面→背面、右→左、上→下の反対側へ切り替える。
    if (inputManager.IsKeyTriggered(VK_NUMPAD1)) {
        yaw_ = isControlPressed ? kPi : 0.0f;
        pitch_ = 0.0f;
    }
    if (inputManager.IsKeyTriggered(VK_NUMPAD3)) {
        yaw_ = isControlPressed ? -kPi / 2.0f : kPi / 2.0f;
        pitch_ = 0.0f;
    }
    if (inputManager.IsKeyTriggered(VK_NUMPAD7)) {
        yaw_ = 0.0f;
        pitch_ = isControlPressed ? -kPitchLimit : kPitchLimit;
    }
    if (inputManager.IsKeyTriggered(VK_NUMPAD5)) {
        isOrthographic_ = !isOrthographic_;
    }
    if (inputManager.IsKeyTriggered(VK_HOME)) {
        Reset();
    }

    if (allowMouseControl) {
        const POINT mouseDelta = inputManager.GetMouseDelta();

        if (inputManager.IsMousePressed(MouseButton::Middle)) {
            if (isControlPressed) {
                // Ctrl+中ボタンの上下移動は、注視点へ近づく／離れるドリー操作。
                // expを使うことで、距離が遠いときは大きく、近いときは細かく動く。
                distance_ *= std::exp(static_cast<float>(mouseDelta.y) * kDollySensitivity);
            } else if (IsShiftPressed(inputManager)) {
                // Shift+中ボタンでは、画面上の右方向・上方向へ注視点を移動する。
                // 距離に比例させることで、ズーム状態にかかわらず操作感をそろえる。
                const Vector3 forward = NormalizeCopy(target_ - position_);
                const Vector3 right = NormalizeCopy(Cross({ 0.0f, 1.0f, 0.0f }, forward));
                const Vector3 up = NormalizeCopy(Cross(forward, right));
                const float panSpeed = distance_ * 0.0015f;
                target_ = target_ + right * (-static_cast<float>(mouseDelta.x) * panSpeed);
                target_ = target_ + up * (static_cast<float>(mouseDelta.y) * panSpeed);
            } else {
                // 中ボタンだけなら、注視点を中心にカメラを周回させる。
                yaw_ += static_cast<float>(mouseDelta.x) * kOrbitSensitivity;
                pitch_ += static_cast<float>(mouseDelta.y) * kOrbitSensitivity;
            }
        }

        // ホイール1目盛りは通常120。上回転で近づき、下回転で離れる。
        const float wheelSteps =
            static_cast<float>(inputManager.GetMouseWheelDelta()) / static_cast<float>(WHEEL_DELTA);
        distance_ *= std::pow(0.8f, wheelSteps);
    }

    pitch_ = (std::clamp)(pitch_, -kPitchLimit, kPitchLimit);
    distance_ = (std::clamp)(distance_, kMinimumDistance, kMaximumDistance);

    // yawが増え続けて精度が落ちないよう、-PI～PI付近へ収める。
    yaw_ = std::remainder(yaw_, 2.0f * kPi);
    UpdateMatrices();
}

Vector3 DebugCamera::CalculatePosition() const {
    // 球面座標をXYZ座標へ変換する。
    // yaw=0、pitch=0では、注視点の-Z側から+Z方向を見る。
    const float horizontalDistance = distance_ * std::cos(pitch_);
    return {
        target_.x + horizontalDistance * std::sin(yaw_),
        target_.y + distance_ * std::sin(pitch_),
        target_.z - horizontalDistance * std::cos(yaw_)
    };
}

Matrix4x4 DebugCamera::MakeLookAtMatrix(const Vector3& eye, const Vector3& target) const {
    // 左手座標系なので、eyeからtargetへ向かう方向をカメラの前方(+Z)にする。
    const Vector3 forward = NormalizeCopy(target - eye);
    const Vector3 right = NormalizeCopy(Cross({ 0.0f, 1.0f, 0.0f }, forward));
    const Vector3 up = Cross(forward, right);

    Matrix4x4 result = MakeIdentity4x4();
    result.m[0][0] = right.x;
    result.m[0][1] = up.x;
    result.m[0][2] = forward.x;
    result.m[1][0] = right.y;
    result.m[1][1] = up.y;
    result.m[1][2] = forward.y;
    result.m[2][0] = right.z;
    result.m[2][1] = up.z;
    result.m[2][2] = forward.z;
    result.m[3][0] = -Vector3::Dot(right, eye);
    result.m[3][1] = -Vector3::Dot(up, eye);
    result.m[3][2] = -Vector3::Dot(forward, eye);
    return result;
}

void DebugCamera::UpdateMatrices() {
    assert(windowWidth_ > 0);
    assert(windowHeight_ > 0);

    position_ = CalculatePosition();
    viewMatrix_ = MakeLookAtMatrix(position_, target_);

    const float aspectRatio =
        static_cast<float>(windowWidth_) / static_cast<float>(windowHeight_);
    if (isOrthographic_) {
        // 透視投影と切り替えたときに見た目の大きさが急変しないよう、
        // 現在距離と視野角から正射影の表示範囲を求める。
        const float halfHeight = distance_ * std::tan(fovY_ / 2.0f);
        const float halfWidth = halfHeight * aspectRatio;
        projectionMatrix_ = MakeOrthographicMatrix(
            -halfWidth, halfHeight, halfWidth, -halfHeight, nearClip_, farClip_);
    } else {
        projectionMatrix_ = MakePerspectiveFovMatrix(
            fovY_, aspectRatio, nearClip_, farClip_);
    }

    // このエンジンは行ベクトル方式なので、Viewの後にProjectionを掛ける。
    viewProjectionMatrix_ = Multiply(viewMatrix_, projectionMatrix_);
}
