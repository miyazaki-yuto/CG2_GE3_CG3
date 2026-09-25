#pragma once

#include "CommonTypes.h"
#include "Component.h"

// 全GameObjectが必ず1つ所有する座標Component。
// 自分基準のLocal座標を保持し、親階層を掛けたWorldMatrixを必要時に計算する。
class TransformComponent final : public Component {
public:
    TransformComponent() = default;
    ~TransformComponent() override = default;

    TransformData& GetLocalTransform() { return localTransform_; }
    const TransformData& GetLocalTransform() const { return localTransform_; }

    Matrix4x4 GetLocalMatrix() const;
    Matrix4x4 GetWorldMatrix() const;

    // World行列を、現在の親を基準にしたLocalの拡縮・回転・座標へ戻して保存する。
    // Editorで親を変更しても、画面上の位置をできるだけ維持するために使用する。
    void SetWorldMatrix(const Matrix4x4& worldMatrix);

    const Vector3& GetLocalScale() const { return localTransform_.scale; }
    const Vector3& GetLocalRotation() const { return localTransform_.rotate; }
    const Vector3& GetLocalPosition() const { return localTransform_.translate; }
    Vector3 GetWorldPosition() const;

    void SetLocalScale(const Vector3& scale) { localTransform_.scale = scale; }
    void SetLocalRotation(const Vector3& rotation) { localTransform_.rotate = rotation; }
    void SetLocalPosition(const Vector3& position) {
        localTransform_.translate = position;
    }
    void SetWorldPosition(const Vector3& position);

    // 既存コードとの互換API。Scale/Rotation/Positionはローカル値を指す。
    const Vector3& GetScale() const { return localTransform_.scale; }
    const Vector3& GetRotation() const { return localTransform_.rotate; }
    const Vector3& GetPosition() const { return localTransform_.translate; }

    void SetScale(const Vector3& scale) { localTransform_.scale = scale; }
    void SetRotation(const Vector3& rotation) { localTransform_.rotate = rotation; }
    void SetPosition(const Vector3& position) { localTransform_.translate = position; }

    void Translate(const Vector3& movement);
    void Rotate(const Vector3& rotation);

private:
    TransformData localTransform_ = {
        { 1.0f, 1.0f, 1.0f },
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f }
    };
};
