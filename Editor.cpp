#include "Editor.h"

#include "AssetManager.h"
#include "CameraComponent.h"
#include "DebugCamera.h"
#include "GameObject.h"
#include "Graphics.h"
#include "InputManager.h"
#include "LightComponent.h"
#include "Material.h"
#include "Model.h"
#include "ModelRendererComponent.h"
#include "PrimitiveRendererComponent.h"
#include "PrefabInstanceComponent.h"
#include "PrefabManager.h"
#include "RendererComponent.h"
#include "Scene.h"
#include "ShaderManager.h"
#include "SpriteRendererComponent.h"
#include "TransformComponent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <memory>
#include <utility>

#ifdef USE_IMGUI
#include <commdlg.h>
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_internal.h"
#pragma comment(lib, "Comdlg32.lib")
#endif

namespace {

#ifdef USE_IMGUI

constexpr float kProjectionEpsilon = 0.0001f;
constexpr float kPi = 3.14159265358979323846f;

Vector3 NormalizeVector(Vector3 vector) {
    vector.Normalize();
    return vector;
}

float DistanceToLineSegment(
    const ImVec2& point,
    const ImVec2& start,
    const ImVec2& end) {
    const float deltaX = end.x - start.x;
    const float deltaY = end.y - start.y;
    const float lengthSquared = deltaX * deltaX + deltaY * deltaY;
    if (lengthSquared <= 0.000001f) {
        const float pointX = point.x - start.x;
        const float pointY = point.y - start.y;
        return std::sqrt(pointX * pointX + pointY * pointY);
    }
    const float projection = (std::clamp)(
        ((point.x - start.x) * deltaX +
         (point.y - start.y) * deltaY) / lengthSquared,
        0.0f,
        1.0f);
    const float closestX = start.x + deltaX * projection;
    const float closestY = start.y + deltaY * projection;
    const float distanceX = point.x - closestX;
    const float distanceY = point.y - closestY;
    return std::sqrt(
        distanceX * distanceX + distanceY * distanceY);
}

Matrix4x4 MakeAxisAngleMatrix(Vector3 axis, float angle) {
    axis.Normalize();
    if (axis.Length() <= kProjectionEpsilon) {
        return MakeIdentity4x4();
    }
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    const float oneMinusCosine = 1.0f - cosine;
    Matrix4x4 result = MakeIdentity4x4();
    result.m[0][0] =
        cosine + axis.x * axis.x * oneMinusCosine;
    result.m[0][1] =
        axis.x * axis.y * oneMinusCosine + axis.z * sine;
    result.m[0][2] =
        axis.x * axis.z * oneMinusCosine - axis.y * sine;
    result.m[1][0] =
        axis.y * axis.x * oneMinusCosine - axis.z * sine;
    result.m[1][1] =
        cosine + axis.y * axis.y * oneMinusCosine;
    result.m[1][2] =
        axis.y * axis.z * oneMinusCosine + axis.x * sine;
    result.m[2][0] =
        axis.z * axis.x * oneMinusCosine + axis.y * sine;
    result.m[2][1] =
        axis.z * axis.y * oneMinusCosine - axis.x * sine;
    result.m[2][2] =
        cosine + axis.z * axis.z * oneMinusCosine;
    return result;
}

float SnapValue(float value, float step) {
    return step > 0.0f
        ? std::round(value / step) * step
        : value;
}

bool ProjectWorldPoint(
    const Vector3& worldPosition,
    const Matrix4x4& viewProjection,
    const ImVec2& viewportMin,
    const ImVec2& viewportSize,
    ImVec2& screenPosition) {
    const float clipX =
        worldPosition.x * viewProjection.m[0][0] +
        worldPosition.y * viewProjection.m[1][0] +
        worldPosition.z * viewProjection.m[2][0] +
        viewProjection.m[3][0];
    const float clipY =
        worldPosition.x * viewProjection.m[0][1] +
        worldPosition.y * viewProjection.m[1][1] +
        worldPosition.z * viewProjection.m[2][1] +
        viewProjection.m[3][1];
    const float clipZ =
        worldPosition.x * viewProjection.m[0][2] +
        worldPosition.y * viewProjection.m[1][2] +
        worldPosition.z * viewProjection.m[2][2] +
        viewProjection.m[3][2];
    const float clipW =
        worldPosition.x * viewProjection.m[0][3] +
        worldPosition.y * viewProjection.m[1][3] +
        worldPosition.z * viewProjection.m[2][3] +
        viewProjection.m[3][3];

    if (clipW <= kProjectionEpsilon || !std::isfinite(clipW)) {
        return false;
    }

    const float ndcX = clipX / clipW;
    const float ndcY = clipY / clipW;
    const float ndcZ = clipZ / clipW;
    if (!std::isfinite(ndcX) || !std::isfinite(ndcY) ||
        !std::isfinite(ndcZ) || ndcZ < 0.0f || ndcZ > 1.0f) {
        return false;
    }

    screenPosition = {
        viewportMin.x + (ndcX + 1.0f) * 0.5f * viewportSize.x,
        viewportMin.y + (1.0f - ndcY) * 0.5f * viewportSize.y
    };
    return true;
}

Vector3 GetWorldDirection(
    const Matrix4x4& worldMatrix,
    const Vector3& localDirection) {
    Vector3 direction = {
        localDirection.x * worldMatrix.m[0][0] +
            localDirection.y * worldMatrix.m[1][0] +
            localDirection.z * worldMatrix.m[2][0],
        localDirection.x * worldMatrix.m[0][1] +
            localDirection.y * worldMatrix.m[1][1] +
            localDirection.z * worldMatrix.m[2][1],
        localDirection.x * worldMatrix.m[0][2] +
            localDirection.y * worldMatrix.m[1][2] +
            localDirection.z * worldMatrix.m[2][2]
    };
    direction.Normalize();
    return direction;
}

float GetDistance(const Vector3& first, const Vector3& second) {
    return (first - second).Length();
}

ImU32 MakeGizmoColor(const Color4& color, float alpha) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(
        color.r,
        color.g,
        color.b,
        alpha));
}

void DrawOutlinedLine(
    ImDrawList* drawList,
    const ImVec2& start,
    const ImVec2& end,
    ImU32 color,
    float thickness = 2.0f) {
    drawList->AddLine(
        start, end, IM_COL32(10, 12, 16, 220), thickness + 2.0f);
    drawList->AddLine(start, end, color, thickness);
}

void DrawArrowHead(
    ImDrawList* drawList,
    const ImVec2& start,
    const ImVec2& end,
    ImU32 color,
    float thickness = 2.0f) {
    const float deltaX = end.x - start.x;
    const float deltaY = end.y - start.y;
    const float length = std::sqrt(deltaX * deltaX + deltaY * deltaY);
    if (length <= 1.0f) {
        return;
    }

    const float normalX = deltaX / length;
    const float normalY = deltaY / length;
    const ImVec2 base = {
        end.x - normalX * 9.0f,
        end.y - normalY * 9.0f
    };
    const ImVec2 left = {
        base.x - normalY * 4.5f,
        base.y + normalX * 4.5f
    };
    const ImVec2 right = {
        base.x + normalY * 4.5f,
        base.y - normalX * 4.5f
    };
    DrawOutlinedLine(drawList, left, end, color, thickness);
    DrawOutlinedLine(drawList, right, end, color, thickness);
}

bool DrawProjectedLine(
    ImDrawList* drawList,
    const Vector3& worldStart,
    const Vector3& worldEnd,
    const Matrix4x4& viewProjection,
    const ImVec2& viewportMin,
    const ImVec2& viewportSize,
    ImU32 color,
    float thickness = 2.0f,
    bool arrow = false) {
    ImVec2 screenStart{};
    ImVec2 screenEnd{};
    if (!ProjectWorldPoint(
            worldStart, viewProjection, viewportMin, viewportSize,
            screenStart) ||
        !ProjectWorldPoint(
            worldEnd, viewProjection, viewportMin, viewportSize,
            screenEnd)) {
        return false;
    }

    DrawOutlinedLine(drawList, screenStart, screenEnd, color, thickness);
    if (arrow) {
        DrawArrowHead(drawList, screenStart, screenEnd, color, thickness);
    }
    return true;
}

void DrawGizmoLabel(
    ImDrawList* drawList,
    const ImVec2& anchor,
    const std::string& label,
    ImU32 color) {
    const ImVec2 textPosition = { anchor.x + 14.0f, anchor.y - 8.0f };
    const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
    drawList->AddRectFilled(
        ImVec2(textPosition.x - 4.0f, textPosition.y - 2.0f),
        ImVec2(
            textPosition.x + textSize.x + 4.0f,
            textPosition.y + textSize.y + 2.0f),
        IM_COL32(18, 20, 26, 205),
        3.0f);
    drawList->AddText(textPosition, color, label.c_str());
}

void DrawSceneOrientationCompass(
    ImDrawList* drawList,
    const Matrix4x4& viewMatrix,
    const ImVec2& viewportMin,
    const ImVec2& viewportSize) {
    const ImVec2 center = {
        viewportMin.x + viewportSize.x - 46.0f,
        viewportMin.y + 46.0f
    };
    constexpr float kAxisLength = 25.0f;
    drawList->AddCircleFilled(center, 34.0f, IM_COL32(18, 20, 26, 190));
    drawList->AddCircle(center, 34.0f, IM_COL32(130, 138, 150, 180), 32, 1.0f);

    struct AxisDisplay {
        Vector3 worldDirection;
        ImU32 color;
        const char* label;
    };
    const AxisDisplay axes[] = {
        { { 1.0f, 0.0f, 0.0f }, IM_COL32(245, 80, 80, 255), "X" },
        { { 0.0f, 1.0f, 0.0f }, IM_COL32(90, 220, 110, 255), "Y" },
        { { 0.0f, 0.0f, 1.0f }, IM_COL32(80, 145, 255, 255), "Z" }
    };

    for (const AxisDisplay& axis : axes) {
        const float viewX =
            axis.worldDirection.x * viewMatrix.m[0][0] +
            axis.worldDirection.y * viewMatrix.m[1][0] +
            axis.worldDirection.z * viewMatrix.m[2][0];
        const float viewY =
            axis.worldDirection.x * viewMatrix.m[0][1] +
            axis.worldDirection.y * viewMatrix.m[1][1] +
            axis.worldDirection.z * viewMatrix.m[2][1];
        const ImVec2 end = {
            center.x + viewX * kAxisLength,
            center.y - viewY * kAxisLength
        };
        DrawOutlinedLine(drawList, center, end, axis.color, 2.0f);
        drawList->AddCircleFilled(end, 4.0f, axis.color);
        drawList->AddText(
            ImVec2(end.x + 5.0f, end.y - 7.0f), axis.color, axis.label);
    }
}

const char* GetComponentName(const Component& component) {
    if (dynamic_cast<const TransformComponent*>(&component) != nullptr) {
        return "Transform";
    }
    if (dynamic_cast<const ModelRendererComponent*>(&component) != nullptr) {
        return "Model Renderer";
    }
    if (dynamic_cast<const SpriteRendererComponent*>(&component) != nullptr) {
        return "Sprite Renderer";
    }
    if (dynamic_cast<const PrimitiveRendererComponent*>(&component) != nullptr) {
        return "Primitive Renderer";
    }
    if (dynamic_cast<const PrefabInstanceComponent*>(&component) != nullptr) {
        return "Prefab Instance";
    }
    if (dynamic_cast<const CameraComponent*>(&component) != nullptr) {
        return "Camera";
    }
    if (dynamic_cast<const LightComponent*>(&component) != nullptr) {
        return "Light";
    }
    return "Component";
}

void DrawColor(Vector4& color) {
    ImGui::ColorEdit4("Color", &color.x);
}

void DrawUVTransform(UVTransform& uvTransform) {
    if (ImGui::TreeNode("UV Transform")) {
        ImGui::DragFloat2(
            "UV Scale", &uvTransform.scale.x, 0.01f, 0.001f, 100.0f);
        ImGui::SliderFloat(
            "UV Rotation", &uvTransform.rotate, -3.141592f, 3.141592f);
        ImGui::DragFloat2(
            "UV Position", &uvTransform.translate.x,
            0.01f, -100.0f, 100.0f);
        ImGui::TreePop();
    }
}

std::string OpenAssetFileDialog(AssetType type) {
    // Windows標準ダイアログを使うことで、Inspectorにパスを手入力する必要をなくす。
    std::array<wchar_t, 32768> selectedPath{};
    const wchar_t modelFilter[] =
        L"Wavefront OBJ (*.obj)\0*.obj\0All Files (*.*)\0*.*\0";
    const wchar_t textureFilter[] =
        L"Texture Files (*.png;*.jpg;*.bmp;*.dds)\0"
        L"*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.dds\0"
        L"All Files (*.*)\0*.*\0";
    const wchar_t prefabFilter[] =
        L"CG2 Prefab (*.prefab)\0*.prefab\0All Files (*.*)\0*.*\0";

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = selectedPath.data();
    dialog.nMaxFile = static_cast<DWORD>(selectedPath.size());
    dialog.lpstrFilter = type == AssetType::Model
        ? modelFilter
        : (type == AssetType::Texture ? textureFilter : prefabFilter);
    dialog.nFilterIndex = 1;
    dialog.lpstrInitialDir = L"Resources";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog) == FALSE) {
        return {};
    }

    const std::u8string utf8 =
        std::filesystem::path(selectedPath.data()).generic_u8string();
    return std::string(
        reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

std::string OpenPrefabSaveDialog() {
    std::array<wchar_t, 32768> selectedPath{};
    const wchar_t filter[] =
        L"CG2 Prefab (*.prefab)\0*.prefab\0All Files (*.*)\0*.*\0";

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = selectedPath.data();
    dialog.nMaxFile = static_cast<DWORD>(selectedPath.size());
    dialog.lpstrFilter = filter;
    dialog.lpstrDefExt = L"prefab";
    dialog.lpstrInitialDir = L"Resources\\Prefabs";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&dialog) == FALSE) {
        return {};
    }

    const std::u8string utf8 =
        std::filesystem::path(selectedPath.data()).generic_u8string();
    return std::string(
        reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

#endif

} // namespace

void Editor::Initialize(
    AssetManager* assetManager,
    PrefabManager* prefabManager,
    Graphics* graphics,
    InputManager* inputManager,
    int defaultTextureHandle,
    std::string defaultTextureGuid,
    std::string defaultModelGuid) {
    assetManager_ = assetManager;
    prefabManager_ = prefabManager;
    graphics_ = graphics;
    inputManager_ = inputManager;
    defaultTextureHandle_ = defaultTextureHandle;
    defaultTextureGuid_ = std::move(defaultTextureGuid);
    sceneSerializer_.Initialize(
        assetManager,
        graphics,
        inputManager,
        defaultTextureHandle,
        defaultTextureGuid_);
    shaderGraphEditor_.Initialize(graphics_);

    addModelGuid_ = std::move(defaultModelGuid);

    constexpr char kDefaultScenePath[] = "Resources/Scenes/MainScene.json";
    std::copy(
        std::begin(kDefaultScenePath),
        std::end(kDefaultScenePath),
        scenePath_.begin());
}

void Editor::Draw(
    Scene& scene,
    bool isPlaying,
    const std::string& playModeMessage) {
#ifdef USE_IMGUI
    isPlaying_ = isPlaying;
    DrawDockSpace();
    DrawViewportWindows(scene, isPlaying);
    DrawPlayModeToolbar(isPlaying, playModeMessage);
    HandleUndoRedoShortcuts(scene);
    DrawHierarchy(scene);
    DrawInspector(scene);
    shaderGraphEditor_.Draw();
#else
    (void)scene;
    (void)isPlaying;
    (void)playModeMessage;
#endif
}

Editor::PlayModeRequest Editor::ConsumePlayModeRequest() {
    const PlayModeRequest request = playModeRequest_;
    playModeRequest_ = PlayModeRequest::None;
    return request;
}

void Editor::ResetSceneContext(const Scene& scene) {
#ifdef USE_IMGUI
    ClearHistory();
    transformGizmoActive_ = false;
    transformGizmoKeyboardMode_ = false;
    transformGizmoGameObjectId_ = 0;
    transformGizmoBeforeSnapshot_.clear();
    // Scene複製ではIDを維持するため、同じIDがある場合は選択状態も維持できる。
    if (scene.FindGameObject(selectedGameObjectId_) == nullptr) {
        selectedGameObjectId_ = 0;
    }
#else
    (void)scene;
#endif
}

#ifdef USE_IMGUI

void Editor::DrawDockSpace() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Layout")) {
            if (ImGui::MenuItem("Reset to Default Layout")) {
                resetDockLayoutRequested_ = true;
            }
            ImGui::Separator();
            ImGui::TextDisabled("Drag a tab out to float it inside the editor.");
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags hostWindowFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("CG2 Editor DockSpace", nullptr, hostWindowFlags);
    ImGui::PopStyleVar(3);

    const ImGuiID dockSpaceId = ImGui::GetID("CG2EditorDockSpaceV2");
    if (resetDockLayoutRequested_ ||
        ImGui::DockBuilderGetNode(dockSpaceId) == nullptr) {
        resetDockLayoutRequested_ = false;
        ImGui::DockBuilderRemoveNode(dockSpaceId);
        ImGui::DockBuilderAddNode(
            dockSpaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockSpaceId, viewport->WorkSize);

        ImGuiID centerDockId = dockSpaceId;
        ImGuiID leftDockId = 0;
        ImGuiID rightDockId = 0;
        ImGuiID topDockId = 0;
        ImGuiID bottomDockId = 0;
        ImGui::DockBuilderSplitNode(
            centerDockId,
            ImGuiDir_Left,
            0.20f,
            &leftDockId,
            &centerDockId);
        ImGui::DockBuilderSplitNode(
            centerDockId,
            ImGuiDir_Right,
            0.25f,
            &rightDockId,
            &centerDockId);
        ImGui::DockBuilderSplitNode(
            centerDockId,
            ImGuiDir_Up,
            0.14f,
            &topDockId,
            &centerDockId);
        ImGui::DockBuilderSplitNode(
            centerDockId,
            ImGuiDir_Down,
            0.25f,
            &bottomDockId,
            &centerDockId);

        ImGui::DockBuilderDockWindow("Hierarchy", leftDockId);
        ImGui::DockBuilderDockWindow("Inspector", rightDockId);
        ImGui::DockBuilderDockWindow("Play Mode", topDockId);
        ImGui::DockBuilderDockWindow("Lighting", bottomDockId);
        ImGui::DockBuilderDockWindow("Sound Control", bottomDockId);
        ImGui::DockBuilderDockWindow("Scene View", centerDockId);
        ImGui::DockBuilderDockWindow("Game View", centerDockId);
        ImGui::DockBuilderFinish(dockSpaceId);
    }

    ImGui::DockSpace(
        dockSpaceId,
        ImVec2(0.0f, 0.0f),
        ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
}

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
                        ImGui::Checkbox("Gizmos", &showSceneGizmos_);
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
                            "Move (G)",
                            TransformGizmoOperation::Translate);
                        drawModeButton(
                            "Rotate (R)",
                            TransformGizmoOperation::Rotate);
                        drawModeButton(
                            "Scale (S)",
                            TransformGizmoOperation::Scale);
                        ImGui::SameLine();
                        const char* spaceLabel =
                            transformGizmoSpace_ ==
                                TransformGizmoSpace::Global
                            ? "Global"
                            : "Local";
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
        "Scene View",
        !isPlaying,
        true,
        "Scene View is available in Edit Mode.");
    drawViewportWindow(
        "Game View",
        isPlaying,
        false,
        "Press Play to display the Game Camera.");
}

void Editor::DrawSceneGizmos(
    Scene& scene,
    float viewportLeft,
    float viewportTop,
    float viewportWidth,
    float viewportHeight) {
    if (graphics_ == nullptr || viewportWidth < 1.0f ||
        viewportHeight < 1.0f) {
        return;
    }

    DebugCamera* sceneCamera = graphics_->GetEditorCamera();
    if (sceneCamera == nullptr) {
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 viewportMin = { viewportLeft, viewportTop };
    const ImVec2 viewportSize = { viewportWidth, viewportHeight };
    const ImVec2 viewportMax = {
        viewportLeft + viewportWidth,
        viewportTop + viewportHeight
    };
    const Matrix4x4& viewProjection =
        sceneCamera->GetViewProjectionMatrix();

    drawList->PushClipRect(viewportMin, viewportMax, true);

    // Draw the selected object last so its Gizmo stays readable when icons overlap.
    for (int selectedPass = 0; selectedPass < 2; ++selectedPass) {
        for (const std::unique_ptr<GameObject>& gameObjectPointer :
            scene.GetGameObjects()) {
            GameObject* gameObject = gameObjectPointer.get();
            if (gameObject == nullptr || gameObject->IsPendingDestroy() ||
                !gameObject->IsActiveInHierarchy()) {
                continue;
            }

            const bool selected =
                gameObject->GetId() == selectedGameObjectId_;
            if (selected != (selectedPass == 1)) {
                continue;
            }

            const Vector3 worldPosition =
                gameObject->GetTransform().GetWorldPosition();

            if (LightComponent* light =
                gameObject->GetComponent<LightComponent>()) {
                const bool lightEnabled =
                    light->IsEnabled() && light->IsLightEnabled();
                const float alpha = lightEnabled ? 1.0f : 0.38f;
                const ImU32 lightColor =
                    MakeGizmoColor(light->GetColor(), alpha);
                ImVec2 lightScreen{};

                if (ProjectWorldPoint(
                        worldPosition,
                        viewProjection,
                        viewportMin,
                        viewportSize,
                        lightScreen)) {
                    const float iconRadius = selected ? 7.0f : 6.0f;
                    drawList->AddCircleFilled(
                        lightScreen,
                        iconRadius,
                        IM_COL32(20, 20, 22, 235),
                        20);
                    drawList->AddCircle(
                        lightScreen,
                        iconRadius,
                        lightColor,
                        20,
                        selected ? 3.0f : 2.0f);

                    for (int rayIndex = 0; rayIndex < 8; ++rayIndex) {
                        const float angle =
                            static_cast<float>(rayIndex) * kPi / 4.0f;
                        const ImVec2 rayStart = {
                            lightScreen.x + std::cos(angle) * 9.0f,
                            lightScreen.y + std::sin(angle) * 9.0f
                        };
                        const ImVec2 rayEnd = {
                            lightScreen.x + std::cos(angle) * 14.0f,
                            lightScreen.y + std::sin(angle) * 14.0f
                        };
                        DrawOutlinedLine(
                            drawList, rayStart, rayEnd, lightColor, 1.5f);
                    }

                    if (selected) {
                        drawList->AddCircle(
                            lightScreen,
                            17.0f,
                            IM_COL32(255, 255, 255, 230),
                            28,
                            1.5f);
                    }

                    std::string label = gameObject->GetName();
                    label += light->GetLightType() ==
                            LightComponent::LightType::Directional
                        ? " [Directional Light]"
                        : " [Point Light]";
                    if (!lightEnabled) {
                        label += " (disabled)";
                    }
                    DrawGizmoLabel(
                        drawList, lightScreen, label, lightColor);
                }

                if (light->GetLightType() ==
                    LightComponent::LightType::Directional) {
                    const Vector3 direction = light->GetDirection();
                    const float directionLength = (std::clamp)(
                        GetDistance(
                            worldPosition, sceneCamera->GetPosition()) * 0.12f,
                        0.75f,
                        12.0f);
                    DrawProjectedLine(
                        drawList,
                        worldPosition,
                        worldPosition + direction * directionLength,
                        viewProjection,
                        viewportMin,
                        viewportSize,
                        lightColor,
                        selected ? 3.0f : 2.0f,
                        true);
                } else if (selected && light->GetRadius() > 0.0f) {
                    Vector3 viewForward =
                        sceneCamera->GetTarget() - sceneCamera->GetPosition();
                    viewForward.Normalize();
                    Vector3 viewRight = Cross(
                        { 0.0f, 1.0f, 0.0f }, viewForward);
                    viewRight.Normalize();
                    if (viewRight.Length() <= kProjectionEpsilon) {
                        viewRight = { 1.0f, 0.0f, 0.0f };
                    }

                    ImVec2 centerScreen{};
                    ImVec2 radiusScreen{};
                    if (ProjectWorldPoint(
                            worldPosition,
                            viewProjection,
                            viewportMin,
                            viewportSize,
                            centerScreen) &&
                        ProjectWorldPoint(
                            worldPosition + viewRight * light->GetRadius(),
                            viewProjection,
                            viewportMin,
                            viewportSize,
                            radiusScreen)) {
                        const float deltaX = radiusScreen.x - centerScreen.x;
                        const float deltaY = radiusScreen.y - centerScreen.y;
                        const float radiusPixels = (std::min)(
                            std::sqrt(deltaX * deltaX + deltaY * deltaY),
                            400.0f);
                        if (radiusPixels >= 3.0f) {
                            drawList->AddCircle(
                                centerScreen,
                                radiusPixels,
                                MakeGizmoColor(light->GetColor(), 0.5f),
                                64,
                                1.5f);
                        }
                    }
                }
            }

            if (CameraComponent* camera =
                gameObject->GetComponent<CameraComponent>()) {
                const bool cameraEnabled = camera->IsEnabled();
                const ImU32 cameraColor = cameraEnabled
                    ? IM_COL32(80, 220, 245, 255)
                    : IM_COL32(110, 135, 145, 145);
                const Matrix4x4 worldMatrix =
                    gameObject->GetTransform().GetWorldMatrix();
                Vector3 right = GetWorldDirection(
                    worldMatrix, { 1.0f, 0.0f, 0.0f });
                Vector3 up = GetWorldDirection(
                    worldMatrix, { 0.0f, 1.0f, 0.0f });
                Vector3 forward = GetWorldDirection(
                    worldMatrix, { 0.0f, 0.0f, 1.0f });
                if (right.Length() <= kProjectionEpsilon) {
                    right = { 1.0f, 0.0f, 0.0f };
                }
                if (up.Length() <= kProjectionEpsilon) {
                    up = { 0.0f, 1.0f, 0.0f };
                }
                if (forward.Length() <= kProjectionEpsilon) {
                    forward = { 0.0f, 0.0f, 1.0f };
                }

                const float frustumDepth = (std::clamp)(
                    camera->GetDistance() * 0.35f, 0.75f, 8.0f);
                float halfHeight = camera->IsOrthographic()
                    ? camera->GetDistance() *
                        std::tan(camera->GetFovY() * 0.5f)
                    : frustumDepth * std::tan(camera->GetFovY() * 0.5f);
                halfHeight = (std::clamp)(halfHeight, 0.2f, 8.0f);
                const float aspectRatio = viewportWidth / viewportHeight;
                const float halfWidth = (std::clamp)(
                    halfHeight * aspectRatio, 0.2f, 12.0f);
                const Vector3 frustumCenter =
                    worldPosition + forward * frustumDepth;
                const Vector3 corners[] = {
                    frustumCenter - right * halfWidth + up * halfHeight,
                    frustumCenter + right * halfWidth + up * halfHeight,
                    frustumCenter + right * halfWidth - up * halfHeight,
                    frustumCenter - right * halfWidth - up * halfHeight
                };
                const float lineThickness = selected ? 3.0f : 1.8f;
                for (const Vector3& corner : corners) {
                    DrawProjectedLine(
                        drawList,
                        worldPosition,
                        corner,
                        viewProjection,
                        viewportMin,
                        viewportSize,
                        cameraColor,
                        lineThickness);
                }
                for (int cornerIndex = 0; cornerIndex < 4; ++cornerIndex) {
                    DrawProjectedLine(
                        drawList,
                        corners[cornerIndex],
                        corners[(cornerIndex + 1) % 4],
                        viewProjection,
                        viewportMin,
                        viewportSize,
                        cameraColor,
                        lineThickness);
                }
                DrawProjectedLine(
                    drawList,
                    worldPosition,
                    frustumCenter,
                    viewProjection,
                    viewportMin,
                    viewportSize,
                    cameraColor,
                    lineThickness,
                    true);

                ImVec2 cameraScreen{};
                if (ProjectWorldPoint(
                        worldPosition,
                        viewProjection,
                        viewportMin,
                        viewportSize,
                        cameraScreen)) {
                    drawList->AddRectFilled(
                        ImVec2(cameraScreen.x - 8.0f, cameraScreen.y - 6.0f),
                        ImVec2(cameraScreen.x + 6.0f, cameraScreen.y + 6.0f),
                        IM_COL32(18, 20, 25, 240),
                        2.0f);
                    drawList->AddRect(
                        ImVec2(cameraScreen.x - 8.0f, cameraScreen.y - 6.0f),
                        ImVec2(cameraScreen.x + 6.0f, cameraScreen.y + 6.0f),
                        cameraColor,
                        2.0f,
                        0,
                        selected ? 3.0f : 2.0f);
                    drawList->AddTriangleFilled(
                        ImVec2(cameraScreen.x + 6.0f, cameraScreen.y - 5.0f),
                        ImVec2(cameraScreen.x + 13.0f, cameraScreen.y - 9.0f),
                        ImVec2(cameraScreen.x + 13.0f, cameraScreen.y + 1.0f),
                        cameraColor);
                    if (selected) {
                        drawList->AddCircle(
                            cameraScreen,
                            18.0f,
                            IM_COL32(255, 255, 255, 235),
                            28,
                            1.5f);
                    }

                    std::string label = gameObject->GetName() + " [Camera]";
                    if (!cameraEnabled) {
                        label += " (disabled)";
                    }
                    DrawGizmoLabel(
                        drawList, cameraScreen, label, cameraColor);
                }
            }
        }
    }

    if (GameObject* selected =
        scene.FindGameObject(selectedGameObjectId_)) {
        if (!selected->IsPendingDestroy() &&
            selected->IsActiveInHierarchy()) {
            DrawTransformGizmo(
                scene,
                *selected,
                *sceneCamera,
                viewProjection,
                viewportLeft,
                viewportTop,
                viewportWidth,
                viewportHeight);
        }
    } else if (transformGizmoActive_) {
        transformGizmoActive_ = false;
        transformGizmoBeforeSnapshot_.clear();
        transformGizmoGameObjectId_ = 0;
    }

    DrawSceneOrientationCompass(
        drawList,
        sceneCamera->GetViewMatrix(),
        viewportMin,
        viewportSize);
    drawList->PopClipRect();
}

Vector3 Editor::GetTransformGizmoAxis(
    const GameObject& gameObject,
    TransformGizmoAxis axis) const {
    Vector3 direction{};
    if (axis == TransformGizmoAxis::X) {
        direction = { 1.0f, 0.0f, 0.0f };
    } else if (axis == TransformGizmoAxis::Y) {
        direction = { 0.0f, 1.0f, 0.0f };
    } else if (axis == TransformGizmoAxis::Z) {
        direction = { 0.0f, 0.0f, 1.0f };
    } else {
        return direction;
    }

    if (transformGizmoSpace_ == TransformGizmoSpace::Local) {
        const Matrix4x4 worldMatrix =
            gameObject.GetTransform().GetWorldMatrix();
        const int row =
            axis == TransformGizmoAxis::X
            ? 0
            : (axis == TransformGizmoAxis::Y ? 1 : 2);
        direction = {
            worldMatrix.m[row][0],
            worldMatrix.m[row][1],
            worldMatrix.m[row][2]
        };
        direction.Normalize();
    }
    return direction;
}

void Editor::BeginTransformGizmo(
    Scene& scene,
    GameObject& gameObject,
    TransformGizmoOperation operation,
    TransformGizmoAxis axis,
    bool keyboardMode,
    float mouseX,
    float mouseY,
    float centerX,
    float centerY) {
    if (transformGizmoActive_) {
        return;
    }
    std::string snapshot;
    if (!CaptureSceneSnapshot(scene, snapshot)) {
        return;
    }

    transformGizmoActive_ = true;
    transformGizmoKeyboardMode_ = keyboardMode;
    transformGizmoOperation_ = operation;
    transformGizmoAxis_ = axis;
    transformGizmoGameObjectId_ = gameObject.GetId();
    transformGizmoInitialTransform_ =
        gameObject.GetTransform().GetLocalTransform();
    transformGizmoInitialWorldMatrix_ =
        gameObject.GetTransform().GetWorldMatrix();
    transformGizmoInitialWorldPosition_ =
        gameObject.GetTransform().GetWorldPosition();
    transformGizmoStartMouseX_ = mouseX;
    transformGizmoStartMouseY_ = mouseY;
    transformGizmoCenterX_ = centerX;
    transformGizmoCenterY_ = centerY;
    transformGizmoBeforeSnapshot_ = std::move(snapshot);
    lastMessage_ =
        keyboardMode
        ? "Transform started. X/Y/Z constrains the axis."
        : "Gizmo drag started.";
}

void Editor::EndTransformGizmo(
    Scene& scene,
    GameObject& gameObject,
    bool cancel) {
    if (!transformGizmoActive_ ||
        transformGizmoGameObjectId_ != gameObject.GetId()) {
        return;
    }

    if (cancel) {
        gameObject.GetTransform().GetLocalTransform() =
            transformGizmoInitialTransform_;
        lastMessage_ = "Transform cancelled.";
    } else {
        const char* label =
            transformGizmoOperation_ ==
                TransformGizmoOperation::Translate
            ? "Move with Gizmo"
            : (transformGizmoOperation_ ==
                    TransformGizmoOperation::Rotate
                ? "Rotate with Gizmo"
                : "Scale with Gizmo");
        CommitHistory(
            scene,
            label,
            std::move(transformGizmoBeforeSnapshot_),
            selectedGameObjectId_);
        lastMessage_ = std::string(label) + ".";
    }

    if (CameraComponent* camera =
        gameObject.GetComponent<CameraComponent>()) {
        camera->CaptureOwnerTransform();
    }
    transformGizmoActive_ = false;
    transformGizmoKeyboardMode_ = false;
    transformGizmoAxis_ = TransformGizmoAxis::None;
    transformGizmoGameObjectId_ = 0;
    transformGizmoBeforeSnapshot_.clear();
}

void Editor::UpdateTransformGizmo(
    Scene& scene,
    GameObject& gameObject,
    DebugCamera& sceneCamera,
    const Matrix4x4& viewProjection,
    float viewportLeft,
    float viewportTop,
    float viewportWidth,
    float viewportHeight,
    float worldLength) {
    if (!transformGizmoActive_ ||
        transformGizmoGameObjectId_ != gameObject.GetId()) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        EndTransformGizmo(scene, gameObject, true);
        return;
    }

    if (!io.KeyCtrl && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_X, false)) {
            transformGizmoAxis_ = TransformGizmoAxis::X;
        } else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
            transformGizmoAxis_ = TransformGizmoAxis::Y;
        } else if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            transformGizmoAxis_ = TransformGizmoAxis::Z;
        }
    }

    const ImVec2 mouse = ImGui::GetMousePos();
    const float mouseDeltaX = mouse.x - transformGizmoStartMouseX_;
    const float mouseDeltaY = mouse.y - transformGizmoStartMouseY_;
    const bool snap = io.KeyCtrl;
    TransformComponent& transform = gameObject.GetTransform();

    if (transformGizmoOperation_ ==
        TransformGizmoOperation::Translate) {
        Vector3 displacement{};
        if (transformGizmoAxis_ == TransformGizmoAxis::None) {
            Vector3 forward = NormalizeVector(
                sceneCamera.GetTarget() - sceneCamera.GetPosition());
            Vector3 right = NormalizeVector(Cross(
                { 0.0f, 1.0f, 0.0f }, forward));
            if (right.Length() <= kProjectionEpsilon) {
                right = { 1.0f, 0.0f, 0.0f };
            }
            Vector3 up = NormalizeVector(Cross(forward, right));
            const float worldPerPixel =
                worldLength / 82.0f;
            displacement =
                right * (mouseDeltaX * worldPerPixel) +
                up * (-mouseDeltaY * worldPerPixel);
            if (snap) {
                displacement.x = SnapValue(displacement.x, 0.5f);
                displacement.y = SnapValue(displacement.y, 0.5f);
                displacement.z = SnapValue(displacement.z, 0.5f);
            }
        } else {
            const Vector3 axis = GetTransformGizmoAxis(
                gameObject, transformGizmoAxis_);
            const ImVec2 viewportMin = {
                viewportLeft, viewportTop
            };
            const ImVec2 viewportSize = {
                viewportWidth, viewportHeight
            };
            ImVec2 startScreen{};
            ImVec2 endScreen{};
            float amount = 0.0f;
            if (ProjectWorldPoint(
                    transformGizmoInitialWorldPosition_,
                    viewProjection,
                    viewportMin,
                    viewportSize,
                    startScreen) &&
                ProjectWorldPoint(
                    transformGizmoInitialWorldPosition_ +
                        axis * worldLength,
                    viewProjection,
                    viewportMin,
                    viewportSize,
                    endScreen)) {
                const float screenX = endScreen.x - startScreen.x;
                const float screenY = endScreen.y - startScreen.y;
                const float screenLength = std::sqrt(
                    screenX * screenX + screenY * screenY);
                if (screenLength > 2.0f) {
                    amount =
                        (mouseDeltaX * screenX +
                         mouseDeltaY * screenY) /
                        screenLength *
                        worldLength / screenLength;
                }
            }
            if (snap) {
                amount = SnapValue(amount, 0.5f);
            }
            displacement = axis * amount;
        }
        transform.SetWorldPosition(
            transformGizmoInitialWorldPosition_ + displacement);
    } else if (
        transformGizmoOperation_ ==
        TransformGizmoOperation::Scale) {
        const float startDistance = std::sqrt(
            (transformGizmoStartMouseX_ - transformGizmoCenterX_) *
                (transformGizmoStartMouseX_ - transformGizmoCenterX_) +
            (transformGizmoStartMouseY_ - transformGizmoCenterY_) *
                (transformGizmoStartMouseY_ - transformGizmoCenterY_));
        const float currentDistance = std::sqrt(
            (mouse.x - transformGizmoCenterX_) *
                (mouse.x - transformGizmoCenterX_) +
            (mouse.y - transformGizmoCenterY_) *
                (mouse.y - transformGizmoCenterY_));
        float factor = startDistance > 12.0f
            ? currentDistance / startDistance
            : 1.0f + (mouseDeltaX - mouseDeltaY) * 0.01f;
        if (snap) {
            factor = SnapValue(factor, 0.1f);
        }
        factor = (std::max)(factor, 0.001f);
        Vector3 scale = transformGizmoInitialTransform_.scale;
        if (transformGizmoAxis_ == TransformGizmoAxis::None ||
            transformGizmoAxis_ == TransformGizmoAxis::X) {
            scale.x = (std::max)(
                transformGizmoInitialTransform_.scale.x * factor,
                0.001f);
        }
        if (transformGizmoAxis_ == TransformGizmoAxis::None ||
            transformGizmoAxis_ == TransformGizmoAxis::Y) {
            scale.y = (std::max)(
                transformGizmoInitialTransform_.scale.y * factor,
                0.001f);
        }
        if (transformGizmoAxis_ == TransformGizmoAxis::None ||
            transformGizmoAxis_ == TransformGizmoAxis::Z) {
            scale.z = (std::max)(
                transformGizmoInitialTransform_.scale.z * factor,
                0.001f);
        }
        transform.SetLocalScale(scale);
    } else {
        const float startAngle = std::atan2(
            transformGizmoStartMouseY_ - transformGizmoCenterY_,
            transformGizmoStartMouseX_ - transformGizmoCenterX_);
        const float currentAngle = std::atan2(
            mouse.y - transformGizmoCenterY_,
            mouse.x - transformGizmoCenterX_);
        float angle = std::remainder(
            currentAngle - startAngle,
            2.0f * kPi);
        const float startRadius = std::sqrt(
            (transformGizmoStartMouseX_ - transformGizmoCenterX_) *
                (transformGizmoStartMouseX_ - transformGizmoCenterX_) +
            (transformGizmoStartMouseY_ - transformGizmoCenterY_) *
                (transformGizmoStartMouseY_ - transformGizmoCenterY_));
        if (startRadius < 12.0f) {
            angle = (mouseDeltaX - mouseDeltaY) * 0.01f;
        }
        if (snap) {
            angle = SnapValue(angle, kPi / 36.0f);
        }

        if (transformGizmoSpace_ == TransformGizmoSpace::Local &&
            transformGizmoAxis_ != TransformGizmoAxis::None) {
            Vector3 rotation = transformGizmoInitialTransform_.rotate;
            if (transformGizmoAxis_ == TransformGizmoAxis::X) {
                rotation.x += angle;
            } else if (
                transformGizmoAxis_ == TransformGizmoAxis::Y) {
                rotation.y += angle;
            } else {
                rotation.z += angle;
            }
            transform.SetLocalRotation(rotation);
        } else {
            Vector3 rotationAxis =
                transformGizmoAxis_ == TransformGizmoAxis::None
                ? NormalizeVector(
                    sceneCamera.GetTarget() -
                    sceneCamera.GetPosition())
                : GetTransformGizmoAxis(
                    gameObject, transformGizmoAxis_);
            Matrix4x4 linearMatrix =
                transformGizmoInitialWorldMatrix_;
            linearMatrix.m[3][0] = 0.0f;
            linearMatrix.m[3][1] = 0.0f;
            linearMatrix.m[3][2] = 0.0f;
            Matrix4x4 rotatedMatrix = Multiply(
                linearMatrix,
                MakeAxisAngleMatrix(rotationAxis, angle));
            rotatedMatrix.m[3][0] =
                transformGizmoInitialWorldPosition_.x;
            rotatedMatrix.m[3][1] =
                transformGizmoInitialWorldPosition_.y;
            rotatedMatrix.m[3][2] =
                transformGizmoInitialWorldPosition_.z;
            transform.SetWorldMatrix(rotatedMatrix);
        }
    }

    if (CameraComponent* camera =
        gameObject.GetComponent<CameraComponent>()) {
        camera->CaptureOwnerTransform();
    }

    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    const bool confirmKeyboard =
        transformGizmoKeyboardMode_ &&
        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
         ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
         ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    const bool confirmDrag =
        !transformGizmoKeyboardMode_ &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    if (confirmKeyboard || confirmDrag) {
        EndTransformGizmo(scene, gameObject, false);
    }
}

void Editor::DrawTransformGizmo(
    Scene& scene,
    GameObject& gameObject,
    DebugCamera& sceneCamera,
    const Matrix4x4& viewProjection,
    float viewportLeft,
    float viewportTop,
    float viewportWidth,
    float viewportHeight) {
    if (transformGizmoActive_ &&
        transformGizmoGameObjectId_ != gameObject.GetId()) {
        if (GameObject* previous =
            scene.FindGameObject(transformGizmoGameObjectId_)) {
            EndTransformGizmo(scene, *previous, true);
        } else {
            transformGizmoActive_ = false;
            transformGizmoBeforeSnapshot_.clear();
            transformGizmoGameObjectId_ = 0;
        }
    }

    const ImVec2 viewportMin = { viewportLeft, viewportTop };
    const ImVec2 viewportSize = { viewportWidth, viewportHeight };
    const Vector3 worldPosition =
        gameObject.GetTransform().GetWorldPosition();
    ImVec2 center{};
    if (!ProjectWorldPoint(
            worldPosition,
            viewProjection,
            viewportMin,
            viewportSize,
            center)) {
        if (transformGizmoActive_) {
            EndTransformGizmo(scene, gameObject, true);
        }
        return;
    }

    const float cameraDistance = (std::max)(
        GetDistance(worldPosition, sceneCamera.GetPosition()),
        0.1f);
    const float visibleWorldHeight = sceneCamera.IsOrthographic()
        ? 2.0f * sceneCamera.GetDistance() *
            std::tan(sceneCamera.GetFovY() * 0.5f)
        : 2.0f * cameraDistance *
            std::tan(sceneCamera.GetFovY() * 0.5f);
    const float worldLength = (std::max)(
        visibleWorldHeight * 82.0f /
            (std::max)(viewportHeight, 1.0f),
        0.05f);

    const TransformGizmoAxis axes[] = {
        TransformGizmoAxis::X,
        TransformGizmoAxis::Y,
        TransformGizmoAxis::Z
    };
    const ImU32 axisColors[] = {
        IM_COL32(235, 75, 75, 255),
        IM_COL32(90, 210, 105, 255),
        IM_COL32(75, 135, 245, 255)
    };
    const char* axisLabels[] = { "X", "Y", "Z" };
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool mouseInside =
        mouse.x >= viewportLeft &&
        mouse.x <= viewportLeft + viewportWidth &&
        mouse.y >= viewportTop &&
        mouse.y <= viewportTop + viewportHeight;

    const ImGuiIO& io = ImGui::GetIO();
    const bool hotkeysAllowed =
        mouseInside &&
        ImGui::IsWindowFocused(
            ImGuiFocusedFlags_RootAndChildWindows) &&
        !io.WantTextInput &&
        !io.KeyCtrl &&
        !transformGizmoActive_;
    if (hotkeysAllowed) {
        TransformGizmoOperation operation =
            transformGizmoOperation_;
        bool startKeyboard = false;
        if (ImGui::IsKeyPressed(ImGuiKey_G, false)) {
            operation = TransformGizmoOperation::Translate;
            startKeyboard = true;
        } else if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
            operation = TransformGizmoOperation::Rotate;
            startKeyboard = true;
        } else if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            operation = TransformGizmoOperation::Scale;
            startKeyboard = true;
        }
        if (startKeyboard) {
            BeginTransformGizmo(
                scene,
                gameObject,
                operation,
                TransformGizmoAxis::None,
                true,
                mouse.x,
                mouse.y,
                center.x,
                center.y);
        }
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    TransformGizmoAxis hoveredAxis = TransformGizmoAxis::None;
    float closestHit = 9.0f;
    std::array<ImVec2, 3> axisEnds{};
    std::array<bool, 3> axisVisible{};

    if (transformGizmoOperation_ !=
        TransformGizmoOperation::Rotate) {
        for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
            const Vector3 direction = GetTransformGizmoAxis(
                gameObject, axes[axisIndex]);
            axisVisible[axisIndex] = ProjectWorldPoint(
                worldPosition + direction * worldLength,
                viewProjection,
                viewportMin,
                viewportSize,
                axisEnds[axisIndex]);
            if (!axisVisible[axisIndex]) {
                continue;
            }
            const float hitDistance = DistanceToLineSegment(
                mouse, center, axisEnds[axisIndex]);
            if (mouseInside && hitDistance < closestHit) {
                closestHit = hitDistance;
                hoveredAxis = axes[axisIndex];
            }
        }

        const float centerDistance = std::sqrt(
            (mouse.x - center.x) * (mouse.x - center.x) +
            (mouse.y - center.y) * (mouse.y - center.y));
        const bool centerHovered =
            mouseInside && centerDistance <= 10.0f;
        if (centerHovered) {
            hoveredAxis = TransformGizmoAxis::None;
            transformGizmoHovered_ = true;
        } else if (closestHit < 9.0f) {
            transformGizmoHovered_ = true;
        }

        for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
            if (!axisVisible[axisIndex]) {
                continue;
            }
            const bool highlighted =
                (transformGizmoActive_ &&
                 transformGizmoAxis_ == axes[axisIndex]) ||
                (!transformGizmoActive_ &&
                 hoveredAxis == axes[axisIndex] &&
                 !centerHovered);
            const ImU32 color = highlighted
                ? IM_COL32(255, 210, 80, 255)
                : axisColors[axisIndex];
            DrawOutlinedLine(
                drawList,
                center,
                axisEnds[axisIndex],
                color,
                highlighted ? 4.0f : 3.0f);
            if (transformGizmoOperation_ ==
                TransformGizmoOperation::Translate) {
                DrawArrowHead(
                    drawList,
                    center,
                    axisEnds[axisIndex],
                    color,
                    highlighted ? 4.0f : 3.0f);
            } else {
                drawList->AddRectFilled(
                    ImVec2(
                        axisEnds[axisIndex].x - 5.0f,
                        axisEnds[axisIndex].y - 5.0f),
                    ImVec2(
                        axisEnds[axisIndex].x + 5.0f,
                        axisEnds[axisIndex].y + 5.0f),
                    color,
                    1.5f);
            }
            drawList->AddText(
                ImVec2(
                    axisEnds[axisIndex].x + 6.0f,
                    axisEnds[axisIndex].y - 8.0f),
                color,
                axisLabels[axisIndex]);
        }

        const ImU32 centerColor =
            (transformGizmoActive_ &&
             transformGizmoAxis_ == TransformGizmoAxis::None) ||
            centerHovered
            ? IM_COL32(255, 210, 80, 255)
            : IM_COL32(230, 235, 242, 255);
        if (transformGizmoOperation_ ==
            TransformGizmoOperation::Scale) {
            drawList->AddRectFilled(
                ImVec2(center.x - 6.0f, center.y - 6.0f),
                ImVec2(center.x + 6.0f, center.y + 6.0f),
                centerColor,
                2.0f);
        } else {
            drawList->AddCircleFilled(
                center, 6.0f, centerColor, 20);
        }
    } else {
        for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
            const Vector3 axis = GetTransformGizmoAxis(
                gameObject, axes[axisIndex]);
            const Vector3 reference =
                std::abs(Vector3::Dot(axis, { 0.0f, 1.0f, 0.0f })) <
                    0.9f
                ? Vector3{ 0.0f, 1.0f, 0.0f }
                : Vector3{ 1.0f, 0.0f, 0.0f };
            const Vector3 firstBasis =
                NormalizeVector(Cross(reference, axis));
            const Vector3 secondBasis =
                NormalizeVector(Cross(axis, firstBasis));
            constexpr int kRingSegments = 64;
            std::array<ImVec2, kRingSegments + 1> points{};
            bool ringVisible = true;
            float ringHit = 1000000.0f;
            for (int segment = 0;
                segment <= kRingSegments;
                ++segment) {
                const float angle =
                    static_cast<float>(segment) /
                    static_cast<float>(kRingSegments) *
                    2.0f * kPi;
                const Vector3 point =
                    worldPosition +
                    (firstBasis * std::cos(angle) +
                     secondBasis * std::sin(angle)) *
                        (worldLength * 0.82f);
                if (!ProjectWorldPoint(
                        point,
                        viewProjection,
                        viewportMin,
                        viewportSize,
                        points[segment])) {
                    ringVisible = false;
                    break;
                }
                if (segment > 0) {
                    ringHit = (std::min)(
                        ringHit,
                        DistanceToLineSegment(
                            mouse,
                            points[segment - 1],
                            points[segment]));
                }
            }
            if (!ringVisible) {
                continue;
            }
            if (mouseInside && ringHit < closestHit) {
                closestHit = ringHit;
                hoveredAxis = axes[axisIndex];
            }
            const bool highlighted =
                (transformGizmoActive_ &&
                 transformGizmoAxis_ == axes[axisIndex]) ||
                (!transformGizmoActive_ &&
                 hoveredAxis == axes[axisIndex]);
            const ImU32 color = highlighted
                ? IM_COL32(255, 210, 80, 255)
                : axisColors[axisIndex];
            for (int segment = 1;
                segment <= kRingSegments;
                ++segment) {
                DrawOutlinedLine(
                    drawList,
                    points[segment - 1],
                    points[segment],
                    color,
                    highlighted ? 3.5f : 2.0f);
            }
        }

        const float mouseRadius = std::sqrt(
            (mouse.x - center.x) * (mouse.x - center.x) +
            (mouse.y - center.y) * (mouse.y - center.y));
        constexpr float kViewRingRadius = 68.0f;
        const bool viewRingHovered =
            mouseInside &&
            closestHit >= 9.0f &&
            std::abs(mouseRadius - kViewRingRadius) <= 6.0f;
        if (viewRingHovered) {
            hoveredAxis = TransformGizmoAxis::None;
        }
        transformGizmoHovered_ =
            closestHit < 9.0f || viewRingHovered;
        const bool viewHighlighted =
            (transformGizmoActive_ &&
             transformGizmoAxis_ == TransformGizmoAxis::None) ||
            viewRingHovered;
        drawList->AddCircle(
            center,
            kViewRingRadius,
            viewHighlighted
                ? IM_COL32(255, 210, 80, 255)
                : IM_COL32(220, 225, 235, 180),
            72,
            viewHighlighted ? 3.0f : 1.5f);
        drawList->AddCircleFilled(
            center,
            4.0f,
            IM_COL32(235, 238, 245, 255),
            16);
    }

    if (!transformGizmoActive_ &&
        transformGizmoHovered_ &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        BeginTransformGizmo(
            scene,
            gameObject,
            transformGizmoOperation_,
            hoveredAxis,
            false,
            mouse.x,
            mouse.y,
            center.x,
            center.y);
    }

    if (transformGizmoActive_) {
        UpdateTransformGizmo(
            scene,
            gameObject,
            sceneCamera,
            viewProjection,
            viewportLeft,
            viewportTop,
            viewportWidth,
            viewportHeight,
            worldLength);

        const char* operation =
            transformGizmoOperation_ ==
                TransformGizmoOperation::Translate
            ? "G Move"
            : (transformGizmoOperation_ ==
                    TransformGizmoOperation::Rotate
                ? "R Rotate"
                : "S Scale");
        const char* axis =
            transformGizmoAxis_ == TransformGizmoAxis::X
            ? " X"
            : (transformGizmoAxis_ == TransformGizmoAxis::Y
                ? " Y"
                : (transformGizmoAxis_ == TransformGizmoAxis::Z
                    ? " Z"
                    : " Free"));
        const std::string help =
            std::string(operation) + axis +
            "  |  LMB/Enter Confirm  RMB/Esc Cancel  Ctrl Snap";
        const ImVec2 textPosition = {
            viewportLeft + 12.0f,
            viewportTop + viewportHeight - 30.0f
        };
        const ImVec2 textSize = ImGui::CalcTextSize(help.c_str());
        drawList->AddRectFilled(
            ImVec2(textPosition.x - 6.0f, textPosition.y - 4.0f),
            ImVec2(
                textPosition.x + textSize.x + 6.0f,
                textPosition.y + textSize.y + 4.0f),
            IM_COL32(18, 20, 26, 220),
            4.0f);
        drawList->AddText(
            textPosition,
            IM_COL32(245, 245, 248, 255),
            help.c_str());
    }
}

void Editor::DrawPlayModeToolbar(
    bool isPlaying,
    const std::string& playModeMessage) {
    ImGui::SetNextWindowSize(ImVec2(360.0f, 120.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin(
        "Play Mode",
        nullptr,
        ImGuiWindowFlags_NoCollapse);

    if (!isPlaying) {
        if (ImGui::Button("Play")) {
            playModeRequest_ = PlayModeRequest::Start;
        }
        ImGui::SameLine();
        ImGui::TextColored(
            ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "EDIT MODE");
        ImGui::TextDisabled("Components and nodes are not updated.");
    } else {
        if (ImGui::Button("Stop")) {
            playModeRequest_ = PlayModeRequest::Stop;
        }
        ImGui::SameLine();
        ImGui::TextColored(
            ImVec4(0.35f, 1.0f, 0.45f, 1.0f), "PLAY MODE");
        ImGui::TextDisabled("Runtime changes are discarded when stopped.");
    }

    if (!playModeMessage.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", playModeMessage.c_str());
    }
    ImGui::End();
}

void Editor::HandleUndoRedoShortcuts(Scene& scene) {
    const ImGuiIO& io = ImGui::GetIO();
    // 文字入力やTransformドラッグ中のCtrl+ZをEditor全体のUndoにしない。
    if (!io.KeyCtrl ||
        ImGui::IsAnyItemActive() ||
        transformGizmoActive_) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        Undo(scene);
    } else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
        Redo(scene);
    }
}

bool Editor::CaptureSceneSnapshot(
    const Scene& scene,
    std::string& snapshot) {
    std::string message;
    if (sceneSerializer_.SerializeToString(scene, snapshot, message)) {
        return true;
    }
    lastMessage_ = "History capture failed: " + message;
    return false;
}

void Editor::CommitHistory(
    Scene& scene,
    const std::string& label,
    std::string beforeSnapshot,
    uint64_t selectionBefore) {
    if (beforeSnapshot.empty()) {
        return;
    }

    std::string afterSnapshot;
    if (!CaptureSceneSnapshot(scene, afterSnapshot)) {
        return;
    }
    // 同じ親へのドロップなど、Sceneに変化が無い操作は履歴を増やさない。
    if (beforeSnapshot == afterSnapshot) {
        return;
    }

    // Undo後に新しい操作をした場合、その位置より先にあったRedo履歴を破棄する。
    if (historyCursor_ < history_.size()) {
        history_.erase(
            history_.begin() + static_cast<std::ptrdiff_t>(historyCursor_),
            history_.end());
    }
    history_.push_back({
        label,
        std::move(beforeSnapshot),
        std::move(afterSnapshot),
        selectionBefore,
        selectedGameObjectId_
    });

    // 大きなSceneでもメモリを使い続けないよう、直近50操作だけを保持する。
    constexpr size_t kMaximumHistoryCount = 50;
    if (history_.size() > kMaximumHistoryCount) {
        history_.erase(history_.begin());
    }
    historyCursor_ = history_.size();
}

bool Editor::Undo(Scene& scene) {
    if (transformGizmoActive_) {
        if (GameObject* gameObject =
            scene.FindGameObject(transformGizmoGameObjectId_)) {
            EndTransformGizmo(scene, *gameObject, true);
        }
        return false;
    }
    if (!CanUndo()) {
        lastMessage_ = "Nothing to undo.";
        return false;
    }

    const HistoryEntry& entry = history_[historyCursor_ - 1];
    std::string message;
    if (!sceneSerializer_.DeserializeFromString(
        scene, entry.beforeSnapshot, message)) {
        lastMessage_ = "Undo failed: " + message;
        return false;
    }

    --historyCursor_;
    selectedGameObjectId_ =
        scene.FindGameObject(entry.selectionBefore) != nullptr
        ? entry.selectionBefore
        : 0;
    hierarchyAction_ = {};
    renamingGameObjectId_ = 0;
    openHierarchyRenamePopup_ = false;
    focusHierarchyRename_ = false;
    transformBeforeSnapshot_.clear();
    transformEditingGameObjectId_ = 0;
    transformSelectionBefore_ = 0;
    lastMessage_ = "Undo: " + entry.label;
    return true;
}

bool Editor::Redo(Scene& scene) {
    if (transformGizmoActive_) {
        if (GameObject* gameObject =
            scene.FindGameObject(transformGizmoGameObjectId_)) {
            EndTransformGizmo(scene, *gameObject, true);
        }
        return false;
    }
    if (!CanRedo()) {
        lastMessage_ = "Nothing to redo.";
        return false;
    }

    const HistoryEntry& entry = history_[historyCursor_];
    std::string message;
    if (!sceneSerializer_.DeserializeFromString(
        scene, entry.afterSnapshot, message)) {
        lastMessage_ = "Redo failed: " + message;
        return false;
    }

    ++historyCursor_;
    selectedGameObjectId_ =
        scene.FindGameObject(entry.selectionAfter) != nullptr
        ? entry.selectionAfter
        : 0;
    hierarchyAction_ = {};
    renamingGameObjectId_ = 0;
    openHierarchyRenamePopup_ = false;
    focusHierarchyRename_ = false;
    transformBeforeSnapshot_.clear();
    transformEditingGameObjectId_ = 0;
    transformSelectionBefore_ = 0;
    lastMessage_ = "Redo: " + entry.label;
    return true;
}

void Editor::ClearHistory() {
    history_.clear();
    historyCursor_ = 0;
    hierarchyAction_ = {};
    renamingGameObjectId_ = 0;
    openHierarchyRenamePopup_ = false;
    focusHierarchyRename_ = false;
    transformBeforeSnapshot_.clear();
    transformEditingGameObjectId_ = 0;
    transformSelectionBefore_ = 0;
}

void Editor::DrawHierarchy(Scene& scene) {
    ImGui::Begin("Hierarchy");
    ImGui::Text("Scene: %s", scene.GetName().c_str());
    DrawSceneFileControls(scene);
    ImGui::Separator();

    ImGui::BeginDisabled(!CanUndo());
    if (ImGui::Button("Undo")) {
        Undo(scene);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!CanRedo());
    if (ImGui::Button("Redo")) {
        Redo(scene);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+Z / Ctrl+Y");
    ImGui::Separator();

    // よく使う操作は上部ボタンから、同じ操作は右クリックメニューからも実行できる。
    if (ImGui::Button("+ Create")) {
        hierarchyAction_ = { HierarchyActionType::CreateRoot, 0, 0 };
    }
    ImGui::SameLine();
    const bool hasSelection =
        scene.FindGameObject(selectedGameObjectId_) != nullptr;
    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::Button("Duplicate")) {
        hierarchyAction_ = {
            HierarchyActionType::Duplicate, selectedGameObjectId_, 0
        };
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        hierarchyAction_ = {
            HierarchyActionType::Delete, selectedGameObjectId_, 0
        };
    }
    ImGui::EndDisabled();

    // Unityに近いF2・Ctrl+D・Deleteのショートカットも用意する。
    // InputText編集中にGameObjectを消さないよう、ItemがActiveな間は受け付けない。
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::IsAnyItemActive() && hasSelection) {
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
            if (GameObject* selected =
                scene.FindGameObject(selectedGameObjectId_)) {
                BeginHierarchyRename(*selected);
            }
        } else if (ImGui::GetIO().KeyCtrl &&
            ImGui::IsKeyPressed(ImGuiKey_D, false)) {
            hierarchyAction_ = {
                HierarchyActionType::Duplicate,
                selectedGameObjectId_,
                0
            };
        } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            hierarchyAction_ = {
                HierarchyActionType::Delete,
                selectedGameObjectId_,
                0
            };
        }
    }
    ImGui::Separator();

    for (const std::unique_ptr<GameObject>& gameObject :
        scene.GetGameObjects()) {
        if (!gameObject->IsPendingDestroy() &&
            gameObject->GetParent() == nullptr) {
            DrawHierarchyNode(scene, *gameObject);
        }
    }
    DrawHierarchyEmptyArea(scene);
    DrawHierarchyRenamePopup(scene);
    ImGui::End();

    // SceneのvectorとGameObjectのchildrenを走査し終えてから、安全に操作を反映する。
    ExecuteHierarchyAction(scene);
}

void Editor::DrawSceneFileControls(Scene& scene) {
    // Play中のScene保存は実行時変更をファイルへ残してしまうため無効にする。
    ImGui::BeginDisabled(isPlaying_);
    ImGui::InputText(
        "Scene File", scenePath_.data(), scenePath_.size());

    const bool controlPressed = ImGui::GetIO().KeyCtrl;
    const bool directInputControlPressed = inputManager_ != nullptr &&
        inputManager_->IsKeyPressed(VK_CONTROL);
    const bool saveButtonPressed = ImGui::Button("Save Scene");
    const bool saveRequested = !isPlaying_ && (saveButtonPressed ||
        (controlPressed && ImGui::IsKeyPressed(ImGuiKey_S, false)) ||
        (directInputControlPressed &&
            inputManager_->IsKeyTriggered('S')));
    if (saveRequested) {
        sceneSerializer_.Save(scene, scenePath_.data(), sceneMessage_);
    }
    ImGui::SameLine();
    const bool loadButtonPressed = ImGui::Button("Load Scene");
    const bool loadRequested = !isPlaying_ && (loadButtonPressed ||
        (controlPressed && ImGui::IsKeyPressed(ImGuiKey_O, false)) ||
        (directInputControlPressed &&
            inputManager_->IsKeyTriggered('O')));
    if (loadRequested) {
        if (sceneSerializer_.Load(
            scene, scenePath_.data(), sceneMessage_)) {
            // 別Sceneの履歴を現在Sceneへ適用しないよう、読み込み成功時に履歴を初期化する。
            ClearHistory();
            // 読み込んだSceneに同じIDがなければ、古い選択を解除する。
            if (scene.FindGameObject(selectedGameObjectId_) == nullptr) {
                selectedGameObjectId_ = 0;
            }
            lastMessage_.clear();
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+S / Ctrl+O");
    ImGui::EndDisabled();

    if (isPlaying_) {
        ImGui::TextDisabled("Scene Save/Load is disabled during Play Mode.");
    }

    if (!sceneMessage_.empty()) {
        ImGui::TextWrapped("%s", sceneMessage_.c_str());
    }
}

void Editor::DrawHierarchyNode(Scene& scene, GameObject& gameObject) {
    if (gameObject.IsPendingDestroy()) {
        return;
    }

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_SpanAvailWidth;
    if (selectedGameObjectId_ == gameObject.GetId()) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (gameObject.GetChildren().empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf |
            ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    // 名前入力は別Popupへ分離し、この行はTreeNodeだけにする。
    // これにより選択・Popup・D&Dの全てが同じTreeNode Itemを安全に参照できる。
    ImGui::PushID(&gameObject);
    const bool opened = ImGui::TreeNodeEx(
        "HierarchyNode",
        flags,
        "%s",
        gameObject.GetName().c_str());
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        selectedGameObjectId_ = gameObject.GetId();
    }
    if (ImGui::IsItemHovered() &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        selectedGameObjectId_ = gameObject.GetId();
        BeginHierarchyRename(gameObject);
    }

    // 右クリックしたGameObjectを操作対象として選択する。
    if (ImGui::BeginPopupContextItem("HierarchyItemContext")) {
        selectedGameObjectId_ = gameObject.GetId();
        if (ImGui::MenuItem("Create Empty Child")) {
            hierarchyAction_ = {
                HierarchyActionType::CreateChild, gameObject.GetId(), 0
            };
        }
        if (ImGui::MenuItem("Rename", "F2")) {
            BeginHierarchyRename(gameObject);
        }
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
            hierarchyAction_ = {
                HierarchyActionType::Duplicate, gameObject.GetId(), 0
            };
        }
        if (ImGui::MenuItem(
            "Unparent", nullptr, false,
            gameObject.GetParent() != nullptr)) {
            hierarchyAction_ = {
                HierarchyActionType::Unparent, gameObject.GetId(), 0
            };
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Delete")) {
            hierarchyAction_ = {
                HierarchyActionType::Delete, gameObject.GetId(), 0
            };
        }
        ImGui::EndPopup();
    }

    // ポインタではなくIDをPayloadへ入れる。Scene側で削除済みならFindが失敗するため安全。
    if (ImGui::BeginDragDropSource()) {
        const GameObject::Id draggedId = gameObject.GetId();
        ImGui::SetDragDropPayload(
            "CG2_GAMEOBJECT_ID", &draggedId, sizeof(draggedId));
        ImGui::Text("Move %s", gameObject.GetName().c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload =
            ImGui::AcceptDragDropPayload("CG2_GAMEOBJECT_ID")) {
            if (payload->IsDelivery() &&
                payload->DataSize == sizeof(GameObject::Id)) {
                GameObject::Id draggedId = 0;
                std::memcpy(
                    &draggedId, payload->Data, sizeof(draggedId));
                hierarchyAction_ = {
                    HierarchyActionType::Reparent,
                    draggedId,
                    gameObject.GetId()
                };
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (opened && !gameObject.GetChildren().empty()) {
        for (GameObject* child : gameObject.GetChildren()) {
            if (child != nullptr && !child->IsPendingDestroy()) {
                DrawHierarchyNode(scene, *child);
            }
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void Editor::DrawHierarchyEmptyArea(Scene& /*scene*/) {
    ImGui::TextDisabled(
        "Drag here to move a GameObject to the scene root.");

    // 残りの空白を1つの大きなD&Dターゲットにする。
    // 高さが残っていない場合も最低40pxを確保し、必ずドロップできるようにする。
    ImVec2 emptySize = ImGui::GetContentRegionAvail();
    emptySize.x = (std::max)(emptySize.x, 1.0f);
    emptySize.y = (std::max)(emptySize.y, 40.0f);
    ImGui::InvisibleButton("HierarchyRootDropArea", emptySize);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        selectedGameObjectId_ = 0;
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload =
            ImGui::AcceptDragDropPayload("CG2_GAMEOBJECT_ID")) {
            if (payload->IsDelivery() &&
                payload->DataSize == sizeof(GameObject::Id)) {
                GameObject::Id draggedId = 0;
                std::memcpy(
                    &draggedId, payload->Data, sizeof(draggedId));
                hierarchyAction_ = {
                    HierarchyActionType::Unparent, draggedId, 0
                };
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Hierarchyの空白を右クリックしたときのScene用メニュー。
    if (ImGui::BeginPopupContextItem("HierarchyEmptyContext")) {
        if (ImGui::MenuItem("Create Empty")) {
            hierarchyAction_ = { HierarchyActionType::CreateRoot, 0, 0 };
        }
        ImGui::EndPopup();
    }
}

void Editor::DrawHierarchyRenamePopup(Scene& scene) {
    if (openHierarchyRenamePopup_) {
        ImGui::OpenPopup("Rename GameObject");
        openHierarchyRenamePopup_ = false;
    }

    if (!ImGui::BeginPopupModal(
        "Rename GameObject", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    GameObject* target = scene.FindGameObject(renamingGameObjectId_);
    if (target == nullptr) {
        renamingGameObjectId_ = 0;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::Text("GameObject: %s", target->GetName().c_str());
    if (focusHierarchyRename_) {
        ImGui::SetKeyboardFocusHere();
        focusHierarchyRename_ = false;
    }
    const bool enterPressed = ImGui::InputText(
        "New Name",
        hierarchyRenameBuffer_.data(),
        hierarchyRenameBuffer_.size(),
        ImGuiInputTextFlags_AutoSelectAll |
        ImGuiInputTextFlags_EnterReturnsTrue);

    const bool confirm = enterPressed || ImGui::Button("Rename");
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel") ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (confirm) {
        target->SetName(
            hierarchyRenameBuffer_[0] != '\0'
            ? hierarchyRenameBuffer_.data()
            : "GameObject");
        renamingGameObjectId_ = 0;
        lastMessage_ = "GameObject renamed.";
        ImGui::CloseCurrentPopup();
    } else if (cancel) {
        renamingGameObjectId_ = 0;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void Editor::ExecuteHierarchyAction(Scene& scene) {
    // 先に予約を空へ戻す。処理中に失敗しても翌フレームへ同じ操作を持ち越さない。
    const HierarchyAction action = hierarchyAction_;
    hierarchyAction_ = {};

    if (action.type == HierarchyActionType::None) {
        return;
    }

    // 操作前のSceneを1回だけ記録し、成功した分岐だけafterと組にして履歴へ追加する。
    std::string beforeSnapshot;
    const uint64_t selectionBefore = selectedGameObjectId_;
    if (!CaptureSceneSnapshot(scene, beforeSnapshot)) {
        return;
    }

    switch (action.type) {
    case HierarchyActionType::None:
        return;

    case HierarchyActionType::CreateRoot: {
        GameObject& created = scene.CreateGameObject("GameObject");
        selectedGameObjectId_ = created.GetId();
        lastMessage_ = "GameObject created.";
        CommitHistory(
            scene, "Create GameObject",
            std::move(beforeSnapshot), selectionBefore);
        return;
    }

    case HierarchyActionType::CreateChild: {
        GameObject* parent = scene.FindGameObject(action.sourceId);
        if (parent == nullptr) {
            lastMessage_ = "Create failed: parent was not found.";
            return;
        }

        GameObject& created = scene.CreateGameObject("GameObject");
        if (!created.SetParent(parent)) {
            scene.DestroyGameObject(created);
            lastMessage_ = "Create failed: parent could not be assigned.";
            return;
        }
        selectedGameObjectId_ = created.GetId();
        lastMessage_ = "Child GameObject created.";
        CommitHistory(
            scene, "Create Child GameObject",
            std::move(beforeSnapshot), selectionBefore);
        return;
    }

    case HierarchyActionType::Duplicate: {
        const GameObject* source = scene.FindGameObject(action.sourceId);
        if (source == nullptr) {
            lastMessage_ = "Duplicate failed: GameObject was not found.";
            return;
        }

        bool skippedComponent = false;
        GameObject* duplicate = DuplicateGameObjectHierarchy(
            scene,
            *source,
            source->GetParent(),
            true,
            skippedComponent);
        if (duplicate == nullptr) {
            lastMessage_ = "Duplicate failed.";
            return;
        }

        selectedGameObjectId_ = duplicate->GetId();
        lastMessage_ = skippedComponent
            ? "Duplicated. Camera or an unsupported component was skipped."
            : "GameObject duplicated.";
        CommitHistory(
            scene, "Duplicate GameObject",
            std::move(beforeSnapshot), selectionBefore);
        return;
    }

    case HierarchyActionType::Delete: {
        GameObject* target = scene.FindGameObject(action.sourceId);
        if (target == nullptr) {
            lastMessage_ = "Delete failed: GameObject was not found.";
            return;
        }

        // 親を次の選択対象にしてから破棄予約する。子はSceneの既存仕様どおり再帰削除される。
        selectedGameObjectId_ = target->GetParent() != nullptr
            ? target->GetParent()->GetId()
            : 0;
        if (renamingGameObjectId_ == target->GetId()) {
            renamingGameObjectId_ = 0;
        }
        // ModelのGPUリソースを解放する前に、使用中のフレームを完了させる。
        if (graphics_ != nullptr) {
            graphics_->FlushGpu();
        }
        scene.DestroyGameObject(*target);
        lastMessage_ = "GameObject and its children scheduled for deletion.";
        CommitHistory(
            scene, "Delete GameObject",
            std::move(beforeSnapshot), selectionBefore);
        return;
    }

    case HierarchyActionType::Reparent: {
        GameObject* source = scene.FindGameObject(action.sourceId);
        GameObject* newParent = scene.FindGameObject(action.targetId);
        if (source == nullptr || newParent == nullptr) {
            lastMessage_ = "Reparent failed: GameObject was not found.";
            return;
        }

        // trueにより、親変更前のWorld Transformをできるだけ維持する。
        if (!source->SetParent(newParent, true)) {
            lastMessage_ =
                "Reparent rejected: a hierarchy cycle is not allowed.";
            return;
        }
        selectedGameObjectId_ = source->GetId();
        lastMessage_ = "Parent changed.";
        CommitHistory(
            scene, "Change Parent",
            std::move(beforeSnapshot), selectionBefore);
        return;
    }

    case HierarchyActionType::Unparent: {
        GameObject* source = scene.FindGameObject(action.sourceId);
        if (source == nullptr) {
            lastMessage_ = "Unparent failed: GameObject was not found.";
            return;
        }
        if (!source->SetParent(nullptr, true)) {
            lastMessage_ = "Unparent failed.";
            return;
        }
        selectedGameObjectId_ = source->GetId();
        lastMessage_ = "GameObject moved to the scene root.";
        CommitHistory(
            scene, "Unparent GameObject",
            std::move(beforeSnapshot), selectionBefore);
        return;
    }
    }
}

void Editor::BeginHierarchyRename(const GameObject& gameObject) {
    renamingGameObjectId_ = gameObject.GetId();
    focusHierarchyRename_ = true;
    openHierarchyRenamePopup_ = true;
    hierarchyRenameBuffer_.fill('\0');

    const std::string& name = gameObject.GetName();
    const size_t copyLength = (std::min)(
        name.size(), hierarchyRenameBuffer_.size() - 1);
    std::copy_n(
        name.data(), copyLength, hierarchyRenameBuffer_.data());
}

GameObject* Editor::DuplicateGameObjectHierarchy(
    Scene& scene,
    const GameObject& source,
    GameObject* duplicateParent,
    bool isDuplicateRoot,
    bool& skippedComponent) {
    if (source.IsPendingDestroy()) {
        return nullptr;
    }

    // 複製の最上位だけ "Copy" 名にし、配下の名前と階層構造は元のまま保つ。
    const std::string duplicateName = isDuplicateRoot
        ? MakeDuplicateName(scene, source.GetName())
        : source.GetName();
    GameObject& duplicate = scene.CreateGameObject(duplicateName);
    duplicate.SetActive(source.IsActive());
    if (duplicateParent != nullptr) {
        duplicate.SetParent(duplicateParent);
    }
    duplicate.GetTransform().GetLocalTransform() =
        source.GetTransform().GetLocalTransform();

    CopyComponentsForDuplicate(source, duplicate, skippedComponent);

    // 子も再帰複製するため、選択したまとまりをそのまま複製できる。
    for (GameObject* child : source.GetChildren()) {
        if (child == nullptr || child->IsPendingDestroy()) {
            continue;
        }
        DuplicateGameObjectHierarchy(
            scene,
            *child,
            &duplicate,
            false,
            skippedComponent);
    }
    return &duplicate;
}

void Editor::CopyComponentsForDuplicate(
    const GameObject& source,
    GameObject& destination,
    bool& skippedComponent) {
    // Component共通の有効状態とRenderer共通の描画順を最後に揃える小さな補助処理。
    const auto copyCommonState = [](
        const Component& sourceComponent,
        Component& destinationComponent) {
        if (const auto* sourceRenderer =
            dynamic_cast<const RendererComponent*>(&sourceComponent)) {
            if (auto* destinationRenderer =
                dynamic_cast<RendererComponent*>(&destinationComponent)) {
                destinationRenderer->SetRenderOrder(
                    sourceRenderer->GetRenderOrder());
            }
        }
        destinationComponent.SetEnabled(sourceComponent.IsEnabled());
    };

    for (const std::unique_ptr<Component>& component :
        source.GetComponents()) {
        if (dynamic_cast<const TransformComponent*>(component.get()) != nullptr) {
            continue;
        }

        if (const auto* sourceRenderer =
            dynamic_cast<const ModelRendererComponent*>(component.get())) {
            if (sourceRenderer->GetSharedModel() == nullptr) {
                skippedComponent = true;
                continue;
            }

            // GUIDが同じModelはAssetManagerのGPUリソースを共有する。
            auto* duplicate =
                destination.AddComponent<ModelRendererComponent>(
                    sourceRenderer->GetSharedModel(),
                    sourceRenderer->GetFallbackTextureHandle(),
                    sourceRenderer->GetModelGuid(),
                    sourceRenderer->GetFallbackTextureGuid());
            duplicate->SetColor(sourceRenderer->GetColor());
            duplicate->SetUVTransform(sourceRenderer->GetUVTransform());
            duplicate->SetLightingEnabled(
                sourceRenderer->IsLightingEnabled());

            // Model本体は共有しても、Inspectorで変更したMaterial設定はGameObject固有。
            // 複製後に片方だけ編集できるよう、TextureとUVの上書き値をSlotごとにコピーする。
            for (uint32_t materialIndex = 0;
                materialIndex < sourceRenderer->GetSharedModel()->GetMaterialCount();
                ++materialIndex) {
                const int textureHandle =
                    sourceRenderer->GetMaterialTextureHandle(materialIndex);
                if (textureHandle >= 0) {
                    duplicate->SetMaterialTextureAsset(
                        materialIndex,
                        textureHandle,
                        sourceRenderer->GetMaterialTextureGuid(materialIndex));
                }
                const int normalTextureHandle =
                    sourceRenderer->GetMaterialNormalTextureHandle(
                        materialIndex);
                if (normalTextureHandle >= 0) {
                    duplicate->SetMaterialNormalTextureAsset(
                        materialIndex,
                        normalTextureHandle,
                        sourceRenderer->GetMaterialNormalTextureGuid(
                            materialIndex));
                }
                if (sourceRenderer->IsMaterialPbrOverridden(materialIndex)) {
                    duplicate->SetMaterialPbrOverridden(materialIndex, true);
                    duplicate->SetMaterialMetallic(
                        materialIndex,
                        sourceRenderer->GetMaterialMetallic(materialIndex));
                    duplicate->SetMaterialRoughness(
                        materialIndex,
                        sourceRenderer->GetMaterialRoughness(materialIndex));
                }
                if (sourceRenderer->IsMaterialUVTransformOverridden(
                    materialIndex)) {
                    duplicate->SetMaterialUVTransformOverridden(
                        materialIndex, true);
                    duplicate->SetMaterialUVTransform(
                        materialIndex,
                        sourceRenderer->GetMaterialUVTransform(materialIndex));
                }
                const std::shared_ptr<Material> sourceShaderMaterial =
                    sourceRenderer->GetShaderMaterial(materialIndex);
                if (sourceShaderMaterial != nullptr && graphics_ != nullptr) {
                    std::shared_ptr<Material> materialCopy =
                        graphics_->CreateMaterial(
                            sourceShaderMaterial->GetShaderName());
                    if (materialCopy != nullptr) {
                        for (const ShaderParameterDefinition& definition :
                            sourceShaderMaterial->
                                GetParameterDefinitions()) {
                            const ShaderParameterValue* value =
                                sourceShaderMaterial->GetParameter(
                                    definition.name);
                            if (value == nullptr) {
                                continue;
                            }
                            switch (definition.type) {
                            case ShaderParameterType::Float:
                                materialCopy->SetFloat(
                                    definition.name, value->floats[0]);
                                break;
                            case ShaderParameterType::Float2:
                                materialCopy->SetFloat2(
                                    definition.name,
                                    value->floats[0],
                                    value->floats[1]);
                                break;
                            case ShaderParameterType::Float3:
                                materialCopy->SetFloat3(
                                    definition.name,
                                    value->floats[0],
                                    value->floats[1],
                                    value->floats[2]);
                                break;
                            case ShaderParameterType::Float4:
                                materialCopy->SetFloat4(
                                    definition.name,
                                    value->floats[0],
                                    value->floats[1],
                                    value->floats[2],
                                    value->floats[3]);
                                break;
                            case ShaderParameterType::Int:
                                materialCopy->SetInt(
                                    definition.name, value->integer);
                                break;
                            case ShaderParameterType::Bool:
                                materialCopy->SetBool(
                                    definition.name,
                                    value->integer != 0);
                                break;
                            }
                        }
                        duplicate->SetShaderMaterial(
                            materialIndex, std::move(materialCopy));
                    }
                }
            }
            copyCommonState(*sourceRenderer, *duplicate);
            continue;
        }

        if (const auto* sourceRenderer =
            dynamic_cast<const SpriteRendererComponent*>(component.get())) {
            auto* duplicate =
                destination.AddComponent<SpriteRendererComponent>(
                    sourceRenderer->GetSprite(),
                    sourceRenderer->GetTextureHandle(),
                    sourceRenderer->GetTextureGuid());
            duplicate->SetColor(sourceRenderer->GetColor());
            duplicate->SetUVTransform(sourceRenderer->GetUVTransform());
            copyCommonState(*sourceRenderer, *duplicate);
            continue;
        }

        if (const auto* sourceRenderer =
            dynamic_cast<const PrimitiveRendererComponent*>(component.get())) {
            if (graphics_ == nullptr) {
                skippedComponent = true;
                continue;
            }
            auto* duplicate =
                destination.AddComponent<PrimitiveRendererComponent>(
                    graphics_->GetPrimitiveDrawer(),
                    sourceRenderer->GetPrimitiveType(),
                    sourceRenderer->GetTextureHandle(),
                    sourceRenderer->GetTextureGuid());
            duplicate->SetTriangleVertices(
                sourceRenderer->GetTriangleVertices());
            duplicate->SetColor(sourceRenderer->GetColor());
            duplicate->SetUVTransform(sourceRenderer->GetUVTransform());
            copyCommonState(*sourceRenderer, *duplicate);
            continue;
        }

        if (const auto* sourceLight =
            dynamic_cast<const LightComponent*>(component.get())) {
            if (graphics_ == nullptr) {
                skippedComponent = true;
                continue;
            }
            auto* duplicate = destination.AddComponent<LightComponent>(
                graphics_->GetLightingManager(),
                sourceLight->GetLightType());
            duplicate->SetLightEnabled(sourceLight->IsLightEnabled());
            duplicate->SetColor(sourceLight->GetColor());
            duplicate->SetIntensity(sourceLight->GetIntensity());
            duplicate->SetRadius(sourceLight->GetRadius());
            duplicate->SetDecay(sourceLight->GetDecay());
            copyCommonState(*sourceLight, *duplicate);
            duplicate->ApplyLight();
            continue;
        }

        if (const auto* sourcePrefab =
            dynamic_cast<const PrefabInstanceComponent*>(component.get())) {
            auto* duplicate =
                destination.AddComponent<PrefabInstanceComponent>(
                    sourcePrefab->GetPrefabGuid(),
                    sourcePrefab->IsAutoUpdateEnabled());
            copyCommonState(*sourcePrefab, *duplicate);
            continue;
        }

        // DebugCameraはGraphicsが1個だけ共有し、SceneSerializerもCameraを最大1個に制限する。
        // Camera付きGameObject自体は複製するが、CameraComponentは重複させない。
        if (dynamic_cast<const CameraComponent*>(component.get()) != nullptr) {
            skippedComponent = true;
            continue;
        }

        // 将来追加されたComponentにClone処理がまだ無い場合も、破損した複製は作らない。
        skippedComponent = true;
    }
}

std::string Editor::MakeDuplicateName(
    const Scene& scene,
    const std::string& sourceName) const {
    const std::string baseName = sourceName + " Copy";
    if (scene.FindGameObject(baseName) == nullptr) {
        return baseName;
    }

    for (uint32_t suffix = 2; ; ++suffix) {
        const std::string candidate =
            baseName + " (" + std::to_string(suffix) + ")";
        if (scene.FindGameObject(candidate) == nullptr) {
            return candidate;
        }
    }
}

void Editor::DrawInspector(Scene& scene) {
    ImGui::Begin("Inspector");
    GameObject* selected = scene.FindGameObject(selectedGameObjectId_);
    if (selected == nullptr) {
        ImGui::TextUnformatted("Select a GameObject in Hierarchy.");
        ImGui::End();
        return;
    }

    std::array<char, 128> nameBuffer{};
    const std::string& currentName = selected->GetName();
    const size_t copyLength = (std::min)(
        currentName.size(), nameBuffer.size() - 1);
    std::copy_n(currentName.data(), copyLength, nameBuffer.data());
    if (ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size())) {
        selected->SetName(nameBuffer.data());
    }
    ImGui::Text("ID: %llu", static_cast<unsigned long long>(selected->GetId()));
    ImGui::Text(
        "Parent: %s",
        selected->GetParent() != nullptr
        ? selected->GetParent()->GetName().c_str()
        : "None");
    if (assetManager_ != nullptr && ImGui::Button("Refresh Assets")) {
        assetManager_->RefreshAssets();
        lastMessage_ = "Asset database refreshed. Registered: " +
            std::to_string(assetManager_->GetAssetCount());
    }

    if (DrawPrefabInspector(scene, *selected)) {
        // Apply/RevertはGameObjectを同じIDの新しい実体へ差し替える。
        // このフレームでは古いselectedポインタを以降使用しない。
        ImGui::End();
        return;
    }

    DrawTransformInspector(scene, *selected);

    Component* removeTarget = nullptr;
    for (const std::unique_ptr<Component>& component :
        selected->GetComponents()) {
        if (component.get() == &selected->GetTransform() ||
            dynamic_cast<PrefabInstanceComponent*>(component.get()) != nullptr) {
            continue;
        }

        ImGui::PushID(component.get());
        const bool opened = ImGui::CollapsingHeader(
            GetComponentName(*component),
            ImGuiTreeNodeFlags_DefaultOpen);
        if (opened) {
            bool enabled = component->IsEnabled();
            if (ImGui::Checkbox("Enabled", &enabled)) {
                component->SetEnabled(enabled);
            }
            DrawComponentInspector(*component);
            if (ImGui::Button("Remove Component")) {
                removeTarget = component.get();
            }
        }
        ImGui::PopID();
    }

    // vectorを走査し終わってから削除し、Iteratorの無効化を防ぐ。
    if (removeTarget != nullptr) {
        std::string beforeSnapshot;
        const uint64_t selectionBefore = selectedGameObjectId_;
        const std::string componentName = GetComponentName(*removeTarget);
        if (CaptureSceneSnapshot(scene, beforeSnapshot)) {
            // ModelなどのGPUリソースをComponentが所有している可能性がある。
            if (graphics_ != nullptr) {
                graphics_->FlushGpu();
            }
            if (selected->RemoveComponent(removeTarget)) {
                lastMessage_ = "Component removed.";
                CommitHistory(
                    scene,
                    "Remove " + componentName,
                    std::move(beforeSnapshot),
                    selectionBefore);
            }
        }
    }

    DrawAddComponent(*selected, scene);
    if (!lastMessage_.empty()) {
        ImGui::TextWrapped("%s", lastMessage_.c_str());
    }
    ImGui::End();
}

bool Editor::DrawPrefabInspector(
    Scene& scene,
    GameObject& gameObject) {
    ImGui::SeparatorText("Prefab");
    PrefabInstanceComponent* prefab =
        gameObject.GetComponent<PrefabInstanceComponent>();

    if (prefab != nullptr) {
        const std::string prefabPath = assetManager_ != nullptr
            ? assetManager_->GetAssetPath(prefab->GetPrefabGuid())
            : std::string{};
        ImGui::TextWrapped("GUID: %s", prefab->GetPrefabGuid().c_str());
        ImGui::TextWrapped("File: %s", prefabPath.c_str());
        bool autoUpdate = prefab->IsAutoUpdateEnabled();
        if (ImGui::Checkbox("Auto Update", &autoUpdate)) {
            prefab->SetAutoUpdateEnabled(autoUpdate);
        }

        ImGui::BeginDisabled(isPlaying_);
        const bool applyToPrefab = ImGui::Button("Apply To Prefab");
        ImGui::EndDisabled();
        if (applyToPrefab) {
            std::string beforeSnapshot;
            const uint64_t selectionBefore = selectedGameObjectId_;
            if (prefabManager_ != nullptr &&
                CaptureSceneSnapshot(scene, beforeSnapshot) &&
                prefabManager_->ApplyPrefab(scene, gameObject, lastMessage_)) {
                CommitHistory(
                    scene,
                    "Apply Prefab",
                    std::move(beforeSnapshot),
                    selectionBefore);
                return true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert From Prefab")) {
            std::string beforeSnapshot;
            const uint64_t selectionBefore = selectedGameObjectId_;
            if (prefabManager_ != nullptr &&
                CaptureSceneSnapshot(scene, beforeSnapshot) &&
                prefabManager_->RefreshInstance(
                    scene, gameObject, lastMessage_) != nullptr) {
                CommitHistory(
                    scene,
                    "Revert Prefab",
                    std::move(beforeSnapshot),
                    selectionBefore);
                return true;
            }
        }
    } else {
        ImGui::TextDisabled("Not a Prefab instance.");
    }

    ImGui::BeginDisabled(isPlaying_);
    const bool saveAsPrefab = ImGui::Button("Save As Prefab...");
    ImGui::EndDisabled();
    if (saveAsPrefab) {
        const std::string filePath = OpenPrefabSaveDialog();
        if (!filePath.empty() && prefabManager_ != nullptr) {
            std::string beforeSnapshot;
            const uint64_t selectionBefore = selectedGameObjectId_;
            if (CaptureSceneSnapshot(scene, beforeSnapshot)) {
                const std::string guid = prefabManager_->SavePrefab(
                    gameObject, filePath, lastMessage_);
                if (!guid.empty()) {
                    CommitHistory(
                        scene,
                        "Create Prefab",
                        std::move(beforeSnapshot),
                        selectionBefore);
                }
            }
        }
    }

    ImGui::SameLine();
    const bool instantiateAsChild =
        ImGui::Button("Instantiate As Child...");
    ImGui::SameLine();
    const bool instantiateAsRoot =
        ImGui::Button("Instantiate As Root...");
    if (instantiateAsChild || instantiateAsRoot) {
        const std::string filePath = OpenAssetFileDialog(AssetType::Prefab);
        if (!filePath.empty() && assetManager_ != nullptr &&
            prefabManager_ != nullptr) {
            std::string importError;
            const std::string guid = assetManager_->ImportPrefab(
                filePath, &importError);
            if (guid.empty()) {
                lastMessage_ = importError;
            } else {
                std::string beforeSnapshot;
                const uint64_t selectionBefore = selectedGameObjectId_;
                if (CaptureSceneSnapshot(scene, beforeSnapshot)) {
                    GameObject* instance = prefabManager_->Instantiate(
                        scene,
                        guid,
                        instantiateAsChild ? &gameObject : nullptr,
                        lastMessage_);
                    if (instance != nullptr) {
                        selectedGameObjectId_ = instance->GetId();
                        CommitHistory(
                            scene,
                            "Instantiate Prefab",
                            std::move(beforeSnapshot),
                            selectionBefore);
                    }
                }
            }
        }
    }
    if (isPlaying_) {
        ImGui::TextDisabled(
            "Apply/Save Prefab is disabled during Play Mode.");
    }
    return false;
}

void Editor::DrawTransformInspector(
    Scene& scene,
    GameObject& gameObject) {
    if (!ImGui::CollapsingHeader(
        "Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    TransformData& transform =
        gameObject.GetTransform().GetLocalTransform();
    const bool positionChanged = ImGui::DragFloat3(
        "Local Position", &transform.translate.x, 0.05f);
    if (positionChanged) {
        if (CameraComponent* camera =
            gameObject.GetComponent<CameraComponent>()) {
            camera->CaptureOwnerTransform();
        }
    }
    HandleTransformHistoryItem(scene, gameObject);
    const bool rotationChanged = ImGui::DragFloat3(
        "Local Rotation", &transform.rotate.x,
        0.01f, -3.141592f, 3.141592f);
    if (rotationChanged) {
        if (CameraComponent* camera =
            gameObject.GetComponent<CameraComponent>()) {
            camera->CaptureOwnerTransform();
        }
    }
    HandleTransformHistoryItem(scene, gameObject);
    ImGui::DragFloat3(
        "Local Scale", &transform.scale.x, 0.01f, 0.001f, 1000.0f);
    HandleTransformHistoryItem(scene, gameObject);

    const Vector3 worldPosition =
        gameObject.GetTransform().GetWorldPosition();
    ImGui::Text(
        "World Position: %.3f, %.3f, %.3f",
        worldPosition.x, worldPosition.y, worldPosition.z);
}

void Editor::HandleTransformHistoryItem(
    Scene& scene,
    GameObject& gameObject) {
    if (ImGui::IsItemActivated()) {
        std::string snapshot;
        if (CaptureSceneSnapshot(scene, snapshot)) {
            transformBeforeSnapshot_ = std::move(snapshot);
            transformEditingGameObjectId_ = gameObject.GetId();
            transformSelectionBefore_ = selectedGameObjectId_;
        }
    }

    if (transformEditingGameObjectId_ != gameObject.GetId()) {
        return;
    }

    if (ImGui::IsItemDeactivatedAfterEdit()) {
        lastMessage_ = "Transform changed.";
        CommitHistory(
            scene,
            "Change Transform",
            std::move(transformBeforeSnapshot_),
            transformSelectionBefore_);
        transformBeforeSnapshot_.clear();
        transformEditingGameObjectId_ = 0;
    } else if (ImGui::IsItemDeactivated()) {
        // クリックしただけで値が変わらなかった場合は履歴を作らない。
        transformBeforeSnapshot_.clear();
        transformEditingGameObjectId_ = 0;
    }
}

void Editor::DrawComponentInspector(Component& component) {
    if (auto* renderer = dynamic_cast<RendererComponent*>(&component)) {
        int renderOrder = renderer->GetRenderOrder();
        if (ImGui::InputInt("Render Order", &renderOrder)) {
            renderer->SetRenderOrder(renderOrder);
        }
    }

    if (auto* modelRenderer =
        dynamic_cast<ModelRendererComponent*>(&component)) {
        DrawColor(modelRenderer->GetColor());
        DrawUVTransform(modelRenderer->GetUVTransform());
        bool lighting = modelRenderer->IsLightingEnabled();
        if (ImGui::Checkbox("Lighting", &lighting)) {
            modelRenderer->SetLightingEnabled(lighting);
        }
        if (Model* model = modelRenderer->GetModel()) {
            const std::string modelPath = assetManager_ != nullptr
                ? assetManager_->GetAssetPath(modelRenderer->GetModelGuid())
                : std::string{};
            ImGui::TextWrapped(
                "Model GUID: %s", modelRenderer->GetModelGuid().c_str());
            ImGui::TextWrapped("Model: %s", modelPath.c_str());
            ImGui::Text(
                "Vertices: %u  Indices: %u",
                model->GetVertexCount(), model->GetIndexCount());
        }
        if (ImGui::Button("Select Model...")) {
            const std::string path = OpenAssetFileDialog(AssetType::Model);
            if (!path.empty() && assetManager_ != nullptr) {
                std::string error;
                const AssetGuid guid = assetManager_->ImportModel(path, &error);
                const bool skySphere = modelRenderer->GetModel() != nullptr &&
                    modelRenderer->GetModel()->IsSkySphere();
                std::shared_ptr<Model> model = guid.empty()
                    ? nullptr
                    : assetManager_->LoadModel(guid, skySphere, &error);
                if (model != nullptr) {
                    modelRenderer->SetModel(std::move(model), guid);
                    lastMessage_ = "Model Asset changed.";
                } else {
                    lastMessage_ = error;
                }
            }
        }

        const std::string fallbackPath = assetManager_ != nullptr
            ? assetManager_->GetAssetPath(
                modelRenderer->GetFallbackTextureGuid())
            : std::string{};
        ImGui::TextWrapped(
            "Fallback Texture GUID: %s",
            modelRenderer->GetFallbackTextureGuid().c_str());
        ImGui::TextWrapped("Fallback Texture: %s", fallbackPath.c_str());
        if (ImGui::Button("Select Fallback Texture...")) {
            const std::string path = OpenAssetFileDialog(AssetType::Texture);
            if (!path.empty() && assetManager_ != nullptr) {
                std::string error;
                const AssetGuid guid = assetManager_->ImportTexture(path, &error);
                const int handle = guid.empty()
                    ? -1 : assetManager_->LoadTexture(guid, &error);
                if (handle >= 0) {
                    modelRenderer->SetFallbackTextureAsset(handle, guid);
                    lastMessage_ = "Fallback Texture Asset changed.";
                } else {
                    lastMessage_ = error;
                }
            }
        }

        if (Model* model = modelRenderer->GetModel()) {
            ImGui::SeparatorText("Material Textures");
            ImGui::TextWrapped(
                "Each OBJ usemtl slot can override its MTL map_Kd texture.");
            for (uint32_t materialIndex = 0;
                materialIndex < model->GetMaterialCount();
                ++materialIndex) {
                if (!model->IsMaterialUsed(materialIndex)) {
                    continue;
                }

                const ModelMaterial* material = model->GetMaterial(materialIndex);
                if (material == nullptr) {
                    continue;
                }

                ImGui::PushID(static_cast<int>(materialIndex));
                const std::string& overrideGuid =
                    modelRenderer->GetMaterialTextureGuid(materialIndex);
                const std::string overridePath =
                    assetManager_ != nullptr && !overrideGuid.empty()
                    ? assetManager_->GetAssetPath(overrideGuid)
                    : std::string{};

                ImGui::Text("Slot %u: %s", materialIndex, material->name.c_str());

                std::shared_ptr<Material> shaderMaterial =
                    modelRenderer->GetShaderMaterial(materialIndex);
                const char* currentShader = shaderMaterial != nullptr
                    ? shaderMaterial->GetShaderName().c_str()
                    : "(Default Engine Material)";
                if (ImGui::BeginCombo("Shader", currentShader)) {
                    if (ImGui::Selectable(
                            "(Default Engine Material)",
                            shaderMaterial == nullptr)) {
                        modelRenderer->ClearShaderMaterial(materialIndex);
                        shaderMaterial.reset();
                        lastMessage_ = "Default Material restored.";
                    }
                    if (graphics_ != nullptr &&
                        graphics_->GetShaderManager() != nullptr) {
                        for (const std::string& shaderName :
                            graphics_->GetShaderManager()->GetShaderNames()) {
                            const bool selected =
                                shaderMaterial != nullptr &&
                                shaderMaterial->GetShaderName() == shaderName;
                            if (ImGui::Selectable(
                                    shaderName.c_str(), selected)) {
                                shaderMaterial =
                                    graphics_->CreateMaterial(shaderName);
                                modelRenderer->SetShaderMaterial(
                                    materialIndex, shaderMaterial);
                                lastMessage_ =
                                    "Material Shader changed to " + shaderName;
                            }
                        }
                    }
                    ImGui::EndCombo();
                }

                if (shaderMaterial != nullptr) {
                    ImGui::Indent();
                    for (const ShaderParameterDefinition& parameter :
                        shaderMaterial->GetParameterDefinitions()) {
                        const ShaderParameterValue* currentValue =
                            shaderMaterial->GetParameter(parameter.name);
                        if (currentValue == nullptr) {
                            continue;
                        }
                        std::array<float, 4> values = currentValue->floats;
                        if (parameter.type == ShaderParameterType::Float &&
                            ImGui::DragFloat(
                                parameter.name.c_str(),
                                &values[0],
                                0.01f)) {
                            shaderMaterial->SetFloat(
                                parameter.name, values[0]);
                        } else if (
                            parameter.type == ShaderParameterType::Float2 &&
                            ImGui::DragFloat2(
                                parameter.name.c_str(),
                                values.data(),
                                0.01f)) {
                            shaderMaterial->SetFloat2(
                                parameter.name, values[0], values[1]);
                        } else if (
                            parameter.type == ShaderParameterType::Float3 &&
                            ImGui::DragFloat3(
                                parameter.name.c_str(),
                                values.data(),
                                0.01f)) {
                            shaderMaterial->SetFloat3(
                                parameter.name,
                                values[0],
                                values[1],
                                values[2]);
                        } else if (
                            parameter.type == ShaderParameterType::Float4 &&
                            ImGui::ColorEdit4(
                                parameter.name.c_str(),
                                values.data())) {
                            shaderMaterial->SetFloat4(
                                parameter.name,
                                values[0],
                                values[1],
                                values[2],
                                values[3]);
                        } else if (
                            parameter.type == ShaderParameterType::Int) {
                            int value = currentValue->integer;
                            if (ImGui::InputInt(
                                    parameter.name.c_str(), &value)) {
                                shaderMaterial->SetInt(
                                    parameter.name, value);
                            }
                        } else if (
                            parameter.type == ShaderParameterType::Bool) {
                            bool value = currentValue->integer != 0;
                            if (ImGui::Checkbox(
                                    parameter.name.c_str(), &value)) {
                                shaderMaterial->SetBool(
                                    parameter.name, value);
                            }
                        }
                    }
                    ImGui::Unindent();
                }
                ImGui::TextWrapped(
                    "MTL Texture: %s",
                    material->texturePath.empty()
                    ? "(none - fallback is used)"
                    : material->texturePath.c_str());
                ImGui::TextWrapped(
                    "Override: %s",
                    overridePath.empty()
                    ? "(Use MTL Texture)"
                    : overridePath.c_str());

                if (ImGui::Button("Select Texture...")) {
                    const std::string path =
                        OpenAssetFileDialog(AssetType::Texture);
                    if (!path.empty() && assetManager_ != nullptr) {
                        std::string error;
                        const AssetGuid guid =
                            assetManager_->ImportTexture(path, &error);
                        const int handle = guid.empty()
                            ? -1 : assetManager_->LoadTexture(guid, &error);
                        if (handle >= 0) {
                            modelRenderer->SetMaterialTextureAsset(
                                materialIndex, handle, guid);
                            lastMessage_ = "Material Texture Asset changed.";
                        } else {
                            lastMessage_ = error;
                        }
                    }
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(overrideGuid.empty());
                if (ImGui::Button("Use MTL Texture")) {
                    modelRenderer->ClearMaterialTextureAsset(materialIndex);
                    lastMessage_ = "Material Texture override cleared.";
                }
                ImGui::EndDisabled();

                const std::string& normalOverrideGuid =
                    modelRenderer->GetMaterialNormalTextureGuid(materialIndex);
                const std::string normalOverridePath =
                    assetManager_ != nullptr && !normalOverrideGuid.empty()
                    ? assetManager_->GetAssetPath(normalOverrideGuid)
                    : std::string{};
                ImGui::TextWrapped(
                    "MTL Normal Map: %s",
                    material->normalTexturePath.empty()
                    ? "(none)"
                    : material->normalTexturePath.c_str());
                ImGui::TextWrapped(
                    "Normal Override: %s",
                    normalOverridePath.empty()
                    ? "(Use MTL Normal Map)"
                    : normalOverridePath.c_str());
                if (ImGui::Button("Select Normal Map...")) {
                    const std::string path =
                        OpenAssetFileDialog(AssetType::Texture);
                    if (!path.empty() && assetManager_ != nullptr) {
                        std::string error;
                        const AssetGuid guid =
                            assetManager_->ImportTexture(path, &error);
                        const int handle = guid.empty()
                            ? -1
                            : assetManager_->LoadLinearTexture(guid, &error);
                        if (handle >= 0) {
                            modelRenderer->SetMaterialNormalTextureAsset(
                                materialIndex, handle, guid);
                            lastMessage_ = "Material Normal Map changed.";
                        } else {
                            lastMessage_ = error;
                        }
                    }
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(normalOverrideGuid.empty());
                if (ImGui::Button("Use MTL Normal Map")) {
                    modelRenderer->ClearMaterialNormalTextureAsset(
                        materialIndex);
                    lastMessage_ = "Normal Map override cleared.";
                }
                ImGui::EndDisabled();

                ImGui::Text(
                    "MTL PBR: Metallic %.3f / Roughness %.3f",
                    material->metallic,
                    material->roughness);
                bool overridePbr =
                    modelRenderer->IsMaterialPbrOverridden(materialIndex);
                if (ImGui::Checkbox(
                        "Override Metallic / Roughness", &overridePbr)) {
                    modelRenderer->SetMaterialPbrOverridden(
                        materialIndex, overridePbr);
                    lastMessage_ = overridePbr
                        ? "Material PBR override enabled."
                        : "Material PBR values now use MTL defaults.";
                }
                if (overridePbr) {
                    float metallic =
                        modelRenderer->GetMaterialMetallic(materialIndex);
                    if (ImGui::SliderFloat(
                            "Metallic", &metallic, 0.0f, 1.0f)) {
                        modelRenderer->SetMaterialMetallic(
                            materialIndex, metallic);
                    }
                    float roughness =
                        modelRenderer->GetMaterialRoughness(materialIndex);
                    if (ImGui::SliderFloat(
                            "Roughness", &roughness, 0.04f, 1.0f)) {
                        modelRenderer->SetMaterialRoughness(
                            materialIndex, roughness);
                    }
                }

                // OFFでは上部のModel Renderer共通UVを使い、
                // ONにしたSlotだけ独立したScale／Rotation／Positionを表示する。
                bool overrideUV =
                    modelRenderer->IsMaterialUVTransformOverridden(
                        materialIndex);
                if (ImGui::Checkbox("Override UV Transform", &overrideUV)) {
                    modelRenderer->SetMaterialUVTransformOverridden(
                        materialIndex, overrideUV);
                    lastMessage_ = overrideUV
                        ? "Material UV Transform override enabled."
                        : "Material UV Transform now uses Model Renderer values.";
                }
                if (overrideUV) {
                    DrawUVTransform(
                        modelRenderer->GetMaterialUVTransform(materialIndex));
                } else {
                    ImGui::TextDisabled(
                        "Uses the Model Renderer UV Transform above.");
                }
                ImGui::Separator();
                ImGui::PopID();
            }
        }
        return;
    }

    if (auto* spriteRenderer =
        dynamic_cast<SpriteRendererComponent*>(&component)) {
        DrawColor(spriteRenderer->GetColor());
        DrawUVTransform(spriteRenderer->GetUVTransform());
        const std::string texturePath = assetManager_ != nullptr
            ? assetManager_->GetAssetPath(spriteRenderer->GetTextureGuid())
            : std::string{};
        ImGui::TextWrapped(
            "Texture GUID: %s", spriteRenderer->GetTextureGuid().c_str());
        ImGui::TextWrapped("Texture: %s", texturePath.c_str());
        if (ImGui::Button("Select Texture...")) {
            const std::string path = OpenAssetFileDialog(AssetType::Texture);
            if (!path.empty() && assetManager_ != nullptr) {
                std::string error;
                const AssetGuid guid = assetManager_->ImportTexture(path, &error);
                const int handle = guid.empty()
                    ? -1 : assetManager_->LoadTexture(guid, &error);
                if (handle >= 0) {
                    spriteRenderer->SetTextureAsset(handle, guid);
                    lastMessage_ = "Texture Asset changed.";
                } else {
                    lastMessage_ = error;
                }
            }
        }
        return;
    }

    if (auto* primitiveRenderer =
        dynamic_cast<PrimitiveRendererComponent*>(&component)) {
        int primitiveType = primitiveRenderer->GetPrimitiveType() ==
            PrimitiveRendererComponent::PrimitiveType::Triangle ? 0 : 1;
        const char* types[] = { "Triangle", "Sphere" };
        if (ImGui::Combo("Primitive", &primitiveType, types, 2)) {
            primitiveRenderer->SetPrimitiveType(
                primitiveType == 0
                ? PrimitiveRendererComponent::PrimitiveType::Triangle
                : PrimitiveRendererComponent::PrimitiveType::Sphere);
        }
        DrawColor(primitiveRenderer->GetColor());
        DrawUVTransform(primitiveRenderer->GetUVTransform());
        const std::string texturePath = assetManager_ != nullptr
            ? assetManager_->GetAssetPath(primitiveRenderer->GetTextureGuid())
            : std::string{};
        ImGui::TextWrapped(
            "Texture GUID: %s", primitiveRenderer->GetTextureGuid().c_str());
        ImGui::TextWrapped("Texture: %s", texturePath.c_str());
        if (ImGui::Button("Select Texture...")) {
            const std::string path = OpenAssetFileDialog(AssetType::Texture);
            if (!path.empty() && assetManager_ != nullptr) {
                std::string error;
                const AssetGuid guid = assetManager_->ImportTexture(path, &error);
                const int handle = guid.empty()
                    ? -1 : assetManager_->LoadTexture(guid, &error);
                if (handle >= 0) {
                    primitiveRenderer->SetTextureAsset(handle, guid);
                    lastMessage_ = "Texture Asset changed.";
                } else {
                    lastMessage_ = error;
                }
            }
        }
        return;
    }

    if (auto* prefab =
        dynamic_cast<PrefabInstanceComponent*>(&component)) {
        ImGui::TextWrapped(
            "Prefab GUID: %s", prefab->GetPrefabGuid().c_str());
        bool autoUpdate = prefab->IsAutoUpdateEnabled();
        if (ImGui::Checkbox("Auto Update From File", &autoUpdate)) {
            prefab->SetAutoUpdateEnabled(autoUpdate);
        }
        return;
    }

    if (auto* light = dynamic_cast<LightComponent*>(&component)) {
        int lightType = light->GetLightType() ==
            LightComponent::LightType::Directional ? 0 : 1;
        const char* types[] = { "Directional", "Point" };
        if (ImGui::Combo("Light Type", &lightType, types, 2)) {
            light->SetLightType(
                lightType == 0
                ? LightComponent::LightType::Directional
                : LightComponent::LightType::Point);
        }
        bool lightEnabled = light->IsLightEnabled();
        if (ImGui::Checkbox("Light Enabled", &lightEnabled)) {
            light->SetLightEnabled(lightEnabled);
        }
        ImGui::ColorEdit4("Light Color", &light->GetColor().r);
        float intensity = light->GetIntensity();
        if (ImGui::DragFloat(
            "Intensity", &intensity, 0.05f, 0.0f, 1000.0f)) {
            light->SetIntensity(intensity);
        }
        if (light->GetLightType() == LightComponent::LightType::Point) {
            float radius = light->GetRadius();
            if (ImGui::DragFloat(
                "Radius", &radius, 0.05f, 0.001f, 1000.0f)) {
                light->SetRadius(radius);
            }
            float decay = light->GetDecay();
            if (ImGui::DragFloat(
                "Decay", &decay, 0.05f, 0.001f, 16.0f)) {
                light->SetDecay(decay);
            }
        }
        ImGui::Text("Slot: %d", light->GetLightHandle());
        light->ApplyLight();
        return;
    }

    if (auto* camera = dynamic_cast<CameraComponent*>(&component)) {
        bool inputEnabled = camera->IsInputEnabled();
        if (ImGui::Checkbox("Input Enabled", &inputEnabled)) {
            camera->SetInputEnabled(inputEnabled);
        }

        bool projectionChanged = false;
        bool orthographic = camera->IsOrthographic();
        if (ImGui::Checkbox("Orthographic", &orthographic)) {
            projectionChanged = true;
        }

        constexpr float kRadiansToDegrees = 57.29577951308232f;
        constexpr float kDegreesToRadians = 0.017453292519943295f;
        float fovDegrees = camera->GetFovY() * kRadiansToDegrees;
        if (ImGui::DragFloat("Field of View", &fovDegrees, 0.5f, 1.0f, 179.0f)) {
            projectionChanged = true;
        }
        float nearClip = camera->GetNearClip();
        if (ImGui::DragFloat("Near Clip", &nearClip, 0.01f, 0.001f, 1000.0f)) {
            projectionChanged = true;
        }
        float farClip = camera->GetFarClip();
        if (ImGui::DragFloat("Far Clip", &farClip, 1.0f, 0.002f, 100000.0f)) {
            projectionChanged = true;
        }
        if (projectionChanged) {
            camera->SetProjectionSettings(
                orthographic,
                fovDegrees * kDegreesToRadians,
                nearClip,
                farClip);
        }

        ImGui::BeginDisabled(isPlaying_);
        const bool alignToView = ImGui::Button("Align Game Camera to Scene View");
        ImGui::EndDisabled();
        if (alignToView) {
            camera->CaptureCurrentView();
            lastMessage_ = "Game Camera aligned to Scene View.";
        }
    }
}

void Editor::DrawAddComponent(GameObject& gameObject, Scene& scene) {
    ImGui::SeparatorText("Add Component");
    const char* componentTypes[] = {
        "Model Renderer",
        "Sprite Renderer",
        "Triangle Renderer",
        "Sphere Renderer",
        "Directional Light",
        "Point Light",
        "Camera"
    };
    ImGui::Combo(
        "Component Type", &addComponentType_, componentTypes,
        static_cast<int>(std::size(componentTypes)));

    if (addComponentType_ == 0) {
        const std::string modelPath = assetManager_ != nullptr
            ? assetManager_->GetAssetPath(addModelGuid_)
            : std::string{};
        ImGui::TextWrapped("Model: %s", modelPath.c_str());
        ImGui::TextWrapped("GUID: %s", addModelGuid_.c_str());
        if (ImGui::Button("Select OBJ...")) {
            const std::string path = OpenAssetFileDialog(AssetType::Model);
            if (!path.empty() && assetManager_ != nullptr) {
                std::string error;
                const AssetGuid guid = assetManager_->ImportModel(path, &error);
                if (!guid.empty()) {
                    addModelGuid_ = guid;
                    lastMessage_ = "Model Asset selected.";
                } else {
                    lastMessage_ = error;
                }
            }
        }
    }

    if (!ImGui::Button("Add Component")) {
        return;
    }

    lastMessage_.clear();
    if (graphics_ == nullptr || assetManager_ == nullptr) {
        lastMessage_ = "Graphics or AssetManager is not available.";
        return;
    }

    std::string beforeSnapshot;
    const uint64_t selectionBefore = selectedGameObjectId_;
    if (!CaptureSceneSnapshot(scene, beforeSnapshot)) {
        return;
    }

    switch (addComponentType_) {
    case 0: {
        std::string error;
        std::shared_ptr<Model> model = assetManager_->LoadModel(
            addModelGuid_, false, &error);
        if (model == nullptr) {
            lastMessage_ = error;
            return;
        }
        gameObject.AddComponent<ModelRendererComponent>(
            std::move(model),
            defaultTextureHandle_,
            addModelGuid_,
            defaultTextureGuid_);
        break;
    }
    case 1:
        gameObject.AddComponent<SpriteRendererComponent>(
            graphics_->GetSprite(),
            defaultTextureHandle_,
            defaultTextureGuid_);
        break;
    case 2:
        gameObject.AddComponent<PrimitiveRendererComponent>(
            graphics_->GetPrimitiveDrawer(),
            PrimitiveRendererComponent::PrimitiveType::Triangle,
            defaultTextureHandle_,
            defaultTextureGuid_);
        break;
    case 3:
        gameObject.AddComponent<PrimitiveRendererComponent>(
            graphics_->GetPrimitiveDrawer(),
            PrimitiveRendererComponent::PrimitiveType::Sphere,
            defaultTextureHandle_,
            defaultTextureGuid_);
        break;
    case 4:
        gameObject.AddComponent<LightComponent>(
            graphics_->GetLightingManager(),
            LightComponent::LightType::Directional);
        break;
    case 5:
        gameObject.AddComponent<LightComponent>(
            graphics_->GetLightingManager(),
            LightComponent::LightType::Point);
        break;
    case 6: {
        bool cameraExists = false;
        for (const std::unique_ptr<GameObject>& object :
            scene.GetGameObjects()) {
            if (object->GetComponent<CameraComponent>() != nullptr) {
                cameraExists = true;
                break;
            }
        }
        if (cameraExists) {
            lastMessage_ = "Only one CameraComponent is currently supported.";
            return;
        }
        gameObject.AddComponent<CameraComponent>(
            graphics_->GetDebugCamera(),
            inputManager_,
            graphics_->GetLightingManager());
        break;
    }
    default:
        return;
    }

    lastMessage_ = "Component added.";
    CommitHistory(
        scene,
        std::string("Add ") + componentTypes[addComponentType_],
        std::move(beforeSnapshot),
        selectionBefore);
}

#endif
