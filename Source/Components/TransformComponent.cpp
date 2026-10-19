#include "TransformComponent.h"

#include "GameObject.h"

#include <algorithm>
#include <cmath>

namespace {

// このエンジンの S * Rx * Ry * Rz * T 形式の行列をTransformDataへ分解する。
// 親に非均一スケールと回転が同時に含まれる場合はせん断成分が生まれるため、
// TransformDataで表現可能な最も近い拡縮・回転として取り出す。
TransformData DecomposeAffineMatrix(const Matrix4x4& matrix) {
    TransformData result{};
    result.translate = {
        matrix.m[3][0], matrix.m[3][1], matrix.m[3][2]
    };

    const auto rowLength = [&matrix](int row) {
        return std::sqrt(
            matrix.m[row][0] * matrix.m[row][0] +
            matrix.m[row][1] * matrix.m[row][1] +
            matrix.m[row][2] * matrix.m[row][2]);
    };

    constexpr float kMinimumScale = 0.000001f;
    result.scale = {
        (std::max)(rowLength(0), kMinimumScale),
        (std::max)(rowLength(1), kMinimumScale),
        (std::max)(rowLength(2), kMinimumScale)
    };

    float rotation[3][3]{};
    for (int column = 0; column < 3; ++column) {
        rotation[0][column] = matrix.m[0][column] / result.scale.x;
        rotation[1][column] = matrix.m[1][column] / result.scale.y;
        rotation[2][column] = matrix.m[2][column] / result.scale.z;
    }

    // Rx * Ry * Rz の順序に対応するオイラー角を取り出す。
    const float sinY = (std::clamp)(-rotation[0][2], -1.0f, 1.0f);
    result.rotate.y = std::asin(sinY);
    const float cosY = std::cos(result.rotate.y);
    if (std::abs(cosY) > 0.00001f) {
        result.rotate.x = std::atan2(rotation[1][2], rotation[2][2]);
        result.rotate.z = std::atan2(rotation[0][1], rotation[0][0]);
    } else {
        // 真上・真下を向く特異点ではZ回転を0に固定して、一意な値へまとめる。
        result.rotate.x = sinY >= 0.0f
            ? std::atan2(rotation[1][0], rotation[1][1])
            : std::atan2(-rotation[1][0], rotation[1][1]);
        result.rotate.z = 0.0f;
    }

    return result;
}

} // namespace

Matrix4x4 TransformComponent::GetLocalMatrix() const {
    return MakeAffineMatrix(
        localTransform_.scale,
        localTransform_.rotate,
        localTransform_.translate);
}

Matrix4x4 TransformComponent::GetWorldMatrix() const {
    const Matrix4x4 localMatrix = GetLocalMatrix();
    const GameObject* owner = GetOwner();
    if (owner == nullptr || owner->GetParent() == nullptr) {
        return localMatrix;
    }

    // このエンジンは行ベクトル方式なので、Localの後ろへParent Worldを掛ける。
    return Multiply(
        localMatrix,
        owner->GetParent()->GetTransform().GetWorldMatrix());
}

void TransformComponent::SetWorldMatrix(const Matrix4x4& worldMatrix) {
    const GameObject* owner = GetOwner();
    Matrix4x4 localMatrix = worldMatrix;
    if (owner != nullptr && owner->GetParent() != nullptr) {
        // 行ベクトル方式では World = Local * ParentWorld なので、
        // Local = World * inverse(ParentWorld) になる。
        localMatrix = Multiply(
            worldMatrix,
            Inverse(owner->GetParent()->GetTransform().GetWorldMatrix()));
    }
    localTransform_ = DecomposeAffineMatrix(localMatrix);
}

Vector3 TransformComponent::GetWorldPosition() const {
    const Matrix4x4 worldMatrix = GetWorldMatrix();
    return {
        worldMatrix.m[3][0],
        worldMatrix.m[3][1],
        worldMatrix.m[3][2]
    };
}

void TransformComponent::SetWorldPosition(const Vector3& position) {
    GameObject* owner = GetOwner();
    if (owner == nullptr || owner->GetParent() == nullptr) {
        SetLocalPosition(position);
        return;
    }

    // 親空間へ逆変換すると、親が移動・回転・拡縮していても指定したWorld位置になる。
    const Matrix4x4 inverseParentWorld = Inverse(
        owner->GetParent()->GetTransform().GetWorldMatrix());
    SetLocalPosition(Transform(position, inverseParentWorld));
}

void TransformComponent::Translate(const Vector3& movement) {
    localTransform_.translate.x += movement.x;
    localTransform_.translate.y += movement.y;
    localTransform_.translate.z += movement.z;
}

void TransformComponent::Rotate(const Vector3& rotation) {
    localTransform_.rotate.x += rotation.x;
    localTransform_.rotate.y += rotation.y;
    localTransform_.rotate.z += rotation.z;
}
