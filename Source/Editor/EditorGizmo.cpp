#include "Editor.h"

#include "CameraComponent.h"
#include "DebugCamera.h"
#include "GameObject.h"
#include "Graphics.h"
#include "InputManager.h"
#include "LightComponent.h"
#include "Scene.h"
#include "TransformComponent.h"

#include <algorithm>
#include <cmath>
#include <string>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"

namespace {

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


} // namespace


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
                    if (light->GetLightType() ==
                        LightComponent::LightType::Directional) {
                        label += " [Directional Light]";
                    } else if (light->GetLightType() ==
                        LightComponent::LightType::Spot) {
                        label += " [Spot Light]";
                    } else {
                        label += " [Point Light]";
                    }
                    if (!lightEnabled) {
                        label += " (disabled)";
                    }
                    DrawGizmoLabel(
                        drawList, lightScreen, label, lightColor);
                }

                if (light->GetLightType() !=
                    LightComponent::LightType::Point) {
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
        ? "トランスフォーム操作を開始しました。X/Y/Zで軸を固定できます。"
        : "ギズモのドラッグを開始しました。";
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
        lastMessage_ = "トランスフォーム操作をキャンセルしました。";
    } else {
        const char* label =
            transformGizmoOperation_ ==
                TransformGizmoOperation::Translate
            ? "ギズモで移動"
            : (transformGizmoOperation_ ==
                    TransformGizmoOperation::Rotate
                ? "ギズモで回転"
                : "ギズモで拡縮");
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
            ? "G 移動"
            : (transformGizmoOperation_ ==
                    TransformGizmoOperation::Rotate
                ? "R 回転"
                : "S 拡縮");
        const char* axis =
            transformGizmoAxis_ == TransformGizmoAxis::X
            ? " X"
            : (transformGizmoAxis_ == TransformGizmoAxis::Y
                ? " Y"
                : (transformGizmoAxis_ == TransformGizmoAxis::Z
                    ? " Z"
                    : " 自由"));
        const std::string help =
            std::string(operation) + axis +
            "  |  左クリック/Enter 決定  右クリック/Esc キャンセル  Ctrl スナップ";
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

#endif
