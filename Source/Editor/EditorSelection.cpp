#include "Editor.h"

#include "DebugCamera.h"
#include "GameObject.h"
#include "Graphics.h"
#include "Model.h"
#include "ModelRendererComponent.h"
#include "Scene.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

bool IntersectRayAabb(
    const Vector3& origin,
    const Vector3& direction,
    const Vector3& minimum,
    const Vector3& maximum,
    float& hitDistance) {
    float nearDistance = 0.0f;
    float farDistance = (std::numeric_limits<float>::max)();
    const float origins[3] = { origin.x, origin.y, origin.z };
    const float directions[3] = { direction.x, direction.y, direction.z };
    const float minimums[3] = { minimum.x, minimum.y, minimum.z };
    const float maximums[3] = { maximum.x, maximum.y, maximum.z };

    for (size_t axis = 0; axis < 3; ++axis) {
        if (std::abs(directions[axis]) <= 0.000001f) {
            if (origins[axis] < minimums[axis] ||
                origins[axis] > maximums[axis]) {
                return false;
            }
            continue;
        }

        float first =
            (minimums[axis] - origins[axis]) / directions[axis];
        float second =
            (maximums[axis] - origins[axis]) / directions[axis];
        if (first > second) {
            std::swap(first, second);
        }
        nearDistance = (std::max)(nearDistance, first);
        farDistance = (std::min)(farDistance, second);
        if (nearDistance > farDistance) {
            return false;
        }
    }

    hitDistance = nearDistance;
    return farDistance >= 0.0f;
}

} // namespace

void Editor::PickGameObjectAtViewport(
    Scene& scene,
    float viewportX,
    float viewportY,
    float viewportWidth,
    float viewportHeight) {
    if (graphics_ == nullptr || viewportWidth <= 0.0f ||
        viewportHeight <= 0.0f) {
        return;
    }

    DebugCamera* camera = graphics_->GetEditorCamera();
    if (camera == nullptr) {
        return;
    }

    const float ndcX = viewportX / viewportWidth * 2.0f - 1.0f;
    const float ndcY = 1.0f - viewportY / viewportHeight * 2.0f;
    const Matrix4x4 inverseViewProjection =
        Inverse(camera->GetViewProjectionMatrix());
    const Vector3 nearPosition = Transform(
        { ndcX, ndcY, 0.0f }, inverseViewProjection);
    const Vector3 farPosition = Transform(
        { ndcX, ndcY, 1.0f }, inverseViewProjection);
    const Vector3 rayOrigin = camera->IsOrthographic()
        ? nearPosition
        : camera->GetPosition();
    Vector3 rayDirection = farPosition - rayOrigin;
    rayDirection.Normalize();
    if (rayDirection.Length() <= 0.000001f) {
        return;
    }

    GameObject* nearestObject = nullptr;
    float nearestDistance = (std::numeric_limits<float>::max)();
    for (const std::unique_ptr<GameObject>& gameObject :
        scene.GetGameObjects()) {
        if (gameObject == nullptr || gameObject->IsPendingDestroy() ||
            !gameObject->IsActiveInHierarchy()) {
            continue;
        }

        ModelRendererComponent* renderer =
            gameObject->GetComponent<ModelRendererComponent>();
        Model* model = renderer != nullptr ? renderer->GetModel() : nullptr;
        if (renderer == nullptr || !renderer->IsEnabled() || model == nullptr ||
            model->IsSkySphere() || !model->HasBounds()) {
            continue;
        }

        const Matrix4x4 inverseWorld =
            Inverse(gameObject->GetTransform().GetWorldMatrix());
        const Vector3 localOrigin = Transform(rayOrigin, inverseWorld);
        const Vector3 localRayEnd = Transform(
            rayOrigin + rayDirection, inverseWorld);
        Vector3 localDirection = localRayEnd - localOrigin;
        localDirection.Normalize();
        if (localDirection.Length() <= 0.000001f) {
            continue;
        }

        float localDistance = 0.0f;
        if (!IntersectRayAabb(
                localOrigin,
                localDirection,
                model->GetBoundsMin(),
                model->GetBoundsMax(),
                localDistance)) {
            continue;
        }

        const Vector3 localHit =
            localOrigin + localDirection * localDistance;
        const Vector3 worldHit = Transform(
            localHit, gameObject->GetTransform().GetWorldMatrix());
        const float worldDistance = Vector3::Distance(rayOrigin, worldHit);
        if (worldDistance < nearestDistance) {
            nearestDistance = worldDistance;
            nearestObject = gameObject.get();
        }
    }

    selectedGameObjectId_ = nearestObject != nullptr
        ? nearestObject->GetId()
        : 0;
    lastMessage_ = nearestObject != nullptr
        ? "モデルを選択しました: " + nearestObject->GetName()
        : "選択を解除しました。";
}

void Editor::RenderSelectionOutline(Scene& scene) {
    GameObject* selectedObject = scene.FindGameObject(selectedGameObjectId_);
    if (selectedObject == nullptr || selectedObject->IsPendingDestroy() ||
        !selectedObject->IsActiveInHierarchy()) {
        return;
    }

    ModelRendererComponent* renderer =
        selectedObject->GetComponent<ModelRendererComponent>();
    if (renderer != nullptr && renderer->IsEnabled()) {
        renderer->RenderOutline();
    }
}
