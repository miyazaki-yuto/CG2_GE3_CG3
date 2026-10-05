#include "Editor.h"
#include "CameraComponent.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"

void Editor::DrawCameraInspector(CameraComponent& camera) {
    bool inputEnabled = camera.IsInputEnabled();
    if (ImGui::Checkbox("入力有効###InputEnabled", &inputEnabled)) {
        camera.SetInputEnabled(inputEnabled);
    }

    bool projectionChanged = false;
    bool orthographic = camera.IsOrthographic();
    if (ImGui::Checkbox("平行投影###Orthographic", &orthographic)) {
        projectionChanged = true;
    }

    constexpr float kRadiansToDegrees = 57.29577951308232f;
    constexpr float kDegreesToRadians = 0.017453292519943295f;
    float fovDegrees = camera.GetFovY() * kRadiansToDegrees;
    if (ImGui::DragFloat("視野角###FieldOfView", &fovDegrees, 0.5f, 1.0f, 179.0f)) {
        projectionChanged = true;
    }
    float nearClip = camera.GetNearClip();
    if (ImGui::DragFloat("ニアクリップ###NearClip", &nearClip, 0.01f, 0.001f, 1000.0f)) {
        projectionChanged = true;
    }
    float farClip = camera.GetFarClip();
    if (ImGui::DragFloat("ファークリップ###FarClip", &farClip, 1.0f, 0.002f, 100000.0f)) {
        projectionChanged = true;
    }
    if (projectionChanged) {
        camera.SetProjectionSettings(
            orthographic,
            fovDegrees * kDegreesToRadians,
            nearClip,
            farClip);
    }

    ImGui::BeginDisabled(isPlaying_);
    const bool alignToView = ImGui::Button("ゲームカメラをシーンビューに合わせる###AlignCameraToView");
    ImGui::EndDisabled();
    if (alignToView) {
        camera.CaptureCurrentView();
        lastMessage_ = "ゲームカメラをシーンビューに合わせました。";
    }
}
#endif
