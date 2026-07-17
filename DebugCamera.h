#pragma once

#include "Matrix4x4.h"

#include <cstdint>

class InputManager;

// Blenderのビューポートに近い操作で、シーンを確認するためのデバッグカメラ。
// カメラは「注視点(target)・水平方向(yaw)・垂直方向(pitch)・距離(distance)」で管理する。
// この方式にすると、常に注視点の周囲を回るオービット操作を簡単に実装できる。
class DebugCamera {
public:
    DebugCamera() = default;
    ~DebugCamera() = default;

    DebugCamera(const DebugCamera&) = delete;
    DebugCamera& operator=(const DebugCamera&) = delete;

    // 画面サイズを受け取り、射影行列を含むカメラ行列を初期化する。
    void Initialize(uint32_t windowWidth, uint32_t windowHeight);

    // カメラ位置は維持したまま、アスペクト比だけを新しい画面サイズへ合わせる。
    void Resize(uint32_t windowWidth, uint32_t windowHeight);

    // InputManagerが収集した1フレーム分の入力をカメラへ反映する。
    // allowMouseControl=falseのときは、ImGui操作中にカメラが動かないようマウス入力だけ無視する。
    void Update(const InputManager& inputManager, bool allowMouseControl = true);

    // 注視点・角度・距離・射影方式を起動時の状態に戻す。
    void Reset();

    const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }
    const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }
    const Matrix4x4& GetViewProjectionMatrix() const { return viewProjectionMatrix_; }
    const Vector3& GetPosition() const { return position_; }
    const Vector3& GetTarget() const { return target_; }
    float GetYaw() const { return yaw_; }
    float GetPitch() const { return pitch_; }
    float GetDistance() const { return distance_; }
    bool IsOrthographic() const { return isOrthographic_; }

private:
    // yaw・pitch・distanceからワールド空間上のカメラ位置を求める。
    Vector3 CalculatePosition() const;

    // カメラ位置から注視点を見るビュー行列を作る。
    Matrix4x4 MakeLookAtMatrix(const Vector3& eye, const Vector3& target) const;

    // 現在のパラメータからView・Projection・ViewProjectionを作り直す。
    void UpdateMatrices();

    uint32_t windowWidth_ = 0;
    uint32_t windowHeight_ = 0;

    Vector3 target_ = { 0.0f, 0.0f, 0.0f };
    Vector3 position_ = { 0.0f, 0.0f, -8.0f };
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
    float distance_ = 8.0f;
    float fovY_ = 0.45f;
    float nearClip_ = 0.1f;
    float farClip_ = 1000.0f;
    bool isOrthographic_ = false;

    Matrix4x4 viewMatrix_ = MakeIdentity4x4();
    Matrix4x4 projectionMatrix_ = MakeIdentity4x4();
    Matrix4x4 viewProjectionMatrix_ = MakeIdentity4x4();
};
