#include "Editor.h"

#include "ColliderComponent.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"

#include <iterator>

void Editor::DrawColliderInspector(ColliderComponent& collider) {
    const char* colliderTypes[] = {
        "Sphere", "AABB", "OBB"
    };
    int colliderType = static_cast<int>(collider.GetColliderType());
    if (ImGui::Combo(
            "形状###ColliderType",
            &colliderType,
            colliderTypes,
            static_cast<int>(std::size(colliderTypes)))) {
        collider.SetColliderType(
            static_cast<ColliderComponent::ColliderType>(colliderType));
    }

    Vector3 center = collider.GetCenterOffset();
    if (ImGui::DragFloat3(
            "中心オフセット###ColliderCenter",
            &center.x,
            0.02f)) {
        collider.SetCenterOffset(center);
    }
    if (collider.GetColliderType() ==
        ColliderComponent::ColliderType::Sphere) {
        float radius = collider.GetRadius();
        if (ImGui::DragFloat(
                "半径###ColliderRadius", &radius, 0.02f, 0.001f, 10000.0f)) {
            collider.SetRadius(radius);
        }
    } else {
        Vector3 halfExtents = collider.GetHalfExtents();
        if (ImGui::DragFloat3(
                "半分サイズ###ColliderHalfExtents",
                &halfExtents.x,
                0.02f,
                0.001f,
                10000.0f)) {
            collider.SetHalfExtents(halfExtents);
        }
    }

    bool trigger = collider.IsTrigger();
    if (ImGui::Checkbox("トリガー###ColliderTrigger", &trigger)) {
        collider.SetTrigger(trigger);
    }
    bool debugDraw = collider.IsDebugDrawEnabled();
    if (ImGui::Checkbox(
            "デバッグ表示###ColliderDebugDraw", &debugDraw)) {
        collider.SetDebugDrawEnabled(debugDraw);
    }
    ImGui::TextColored(
        collider.IsColliding()
            ? ImVec4(1.0f, 0.2f, 0.1f, 1.0f)
            : ImVec4(0.2f, 1.0f, 0.35f, 1.0f),
        collider.IsColliding() ? "衝突中" : "非衝突");
}
#endif
