#include "Editor.h"

#include "Graphics.h"
#include "Scene.h"

#include <algorithm>
#include <cstdint>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"

void Editor::DrawViewportWindows(Scene& scene, bool isPlaying) {
    viewportVisible_ = false;
    viewportHovered_ = false;
    transformGizmoHovered_ = false;
    if (isPlaying && transformGizmoActive_) {
        transformGizmoActive_ = false;
        transformGizmoBeforeSnapshot_.clear();
        transformGizmoGameObjectId_ = 0;
    }

    const bool modeChanged =
        !hasViewportMode_ || lastViewportWasPlay_ != isPlaying;
    hasViewportMode_ = true;
    lastViewportWasPlay_ = isPlaying;

    const auto drawViewportWindow = [this, &scene, modeChanged](
        const char* title,
        bool activeView,
        bool isSceneView,
        const char* inactiveMessage) {
        if (modeChanged && activeView) {
            ImGui::SetNextWindowFocus();
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        const bool contentsVisible = ImGui::Begin(
            title,
            nullptr,
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse);

        if (contentsVisible) {
            if (activeView && graphics_ != nullptr) {
                const ImVec2 available = ImGui::GetContentRegionAvail();
                const uint32_t textureWidth =
                    graphics_->GetEditorViewportTextureWidth();
                const uint32_t textureHeight =
                    graphics_->GetEditorViewportTextureHeight();
                const uint64_t textureId =
                    graphics_->GetEditorViewportTextureId();

                if (available.x >= 1.0f && available.y >= 1.0f &&
                    textureWidth > 0 && textureHeight > 0 && textureId != 0) {
                    viewportWidth_ = (std::min)(
                        static_cast<uint32_t>(available.x), textureWidth);
                    viewportHeight_ = (std::min)(
                        static_cast<uint32_t>(available.y), textureHeight);
                    const ImVec2 uvMax = {
                        static_cast<float>(viewportWidth_) /
                            static_cast<float>(textureWidth),
                        static_cast<float>(viewportHeight_) /
                            static_cast<float>(textureHeight)
                    };
                    ImGui::Image(
                        reinterpret_cast<ImTextureID>(
                            static_cast<uintptr_t>(textureId)),
                        ImVec2(
                            static_cast<float>(viewportWidth_),
                            static_cast<float>(viewportHeight_)),
                        ImVec2(0.0f, 0.0f),
                        uvMax);
                    const ImVec2 imageMin = ImGui::GetItemRectMin();
                    const bool imageHovered = ImGui::IsItemHovered();
                    viewportVisible_ = true;

                    bool gizmoControlHovered = false;
                    if (isSceneView) {
                        if (showSceneGizmos_ || transformGizmoActive_) {
                            DrawSceneGizmos(
                                scene,
                                imageMin.x,
                                imageMin.y,
                                static_cast<float>(viewportWidth_),
                                static_cast<float>(viewportHeight_));
                        }

                        ImGui::SetCursorScreenPos(ImVec2(
                            imageMin.x + 10.0f, imageMin.y + 10.0f));
                        ImGui::PushStyleVar(
                            ImGuiStyleVar_FramePadding, ImVec2(7.0f, 4.0f));
                        ImGui::PushStyleColor(
                            ImGuiCol_FrameBg, ImVec4(0.08f, 0.09f, 0.12f, 0.88f));
                        ImGui::PushStyleColor(
                            ImGuiCol_FrameBgHovered,
                            ImVec4(0.14f, 0.16f, 0.21f, 0.95f));
                        ImGui::Checkbox("ギズモ###Gizmos", &showSceneGizmos_);
                        gizmoControlHovered =
                            ImGui::IsItemHovered() || ImGui::IsItemActive();
                        const auto drawModeButton =
                            [this, &gizmoControlHovered](
                                const char* label,
                                TransformGizmoOperation operation) {
                                ImGui::SameLine();
                                const bool selected =
                                    transformGizmoOperation_ == operation;
                                if (selected) {
                                    ImGui::PushStyleColor(
                                        ImGuiCol_Button,
                                        ImVec4(
                                            0.18f, 0.42f, 0.68f, 0.96f));
                                }
                                ImGui::BeginDisabled(
                                    transformGizmoActive_);
                                if (ImGui::Button(label)) {
                                    transformGizmoOperation_ = operation;
                                }
                                gizmoControlHovered =
                                    gizmoControlHovered ||
                                    ImGui::IsItemHovered() ||
                                    ImGui::IsItemActive();
                                ImGui::EndDisabled();
                                if (selected) {
                                    ImGui::PopStyleColor();
                                }
                            };
                        drawModeButton(
                            "移動 (G)###MoveGizmo",
                            TransformGizmoOperation::Translate);
                        drawModeButton(
                            "回転 (R)###RotateGizmo",
                            TransformGizmoOperation::Rotate);
                        drawModeButton(
                            "拡縮 (S)###ScaleGizmo",
                            TransformGizmoOperation::Scale);
                        ImGui::SameLine();
                        const char* spaceLabel =
                            transformGizmoSpace_ ==
                                TransformGizmoSpace::Global
                            ? "グローバル###GizmoSpace"
                            : "ローカル###GizmoSpace";
                        ImGui::BeginDisabled(transformGizmoActive_);
                        if (ImGui::Button(spaceLabel)) {
                            transformGizmoSpace_ =
                                transformGizmoSpace_ ==
                                    TransformGizmoSpace::Global
                                ? TransformGizmoSpace::Local
                                : TransformGizmoSpace::Global;
                        }
                        gizmoControlHovered =
                            gizmoControlHovered ||
                            ImGui::IsItemHovered() ||
                            ImGui::IsItemActive();
                        ImGui::EndDisabled();
                        ImGui::PopStyleColor(2);
                        ImGui::PopStyleVar();

                        // Scene View上のモデルを左クリックで選択する。
                        // Alt+マウスのカメラ操作、ギズモ、上部ボタン操作とは競合させない。
                        if (imageHovered && !gizmoControlHovered &&
                            !transformGizmoHovered_ &&
                            !transformGizmoActive_ &&
                            !ImGui::GetIO().KeyAlt &&
                            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                            const ImVec2 mousePosition = ImGui::GetMousePos();
                            PickGameObjectAtViewport(
                                scene,
                                mousePosition.x - imageMin.x,
                                mousePosition.y - imageMin.y,
                                static_cast<float>(viewportWidth_),
                                static_cast<float>(viewportHeight_));
                        }
                    }
                    viewportHovered_ =
                        imageHovered &&
                        !gizmoControlHovered &&
                        !transformGizmoHovered_ &&
                        !transformGizmoActive_;
                }
            } else {
                ImGui::SetCursorPos(ImVec2(16.0f, 16.0f));
                ImGui::TextDisabled("%s", inactiveMessage);
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();
    };

    drawViewportWindow(
        "シーンビュー###Scene View",
        !isPlaying,
        true,
        "シーンビューは編集モードで使用できます。");
    drawViewportWindow(
        "ゲームビュー###Game View",
        isPlaying,
        false,
        "再生するとゲームカメラが表示されます。");
}
#endif
