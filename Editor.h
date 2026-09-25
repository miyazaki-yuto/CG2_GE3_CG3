#pragma once

#include "CommonTypes.h"
#include "SceneSerializer.h"
#include "ShaderGraphEditor.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class AssetManager;
class Component;
class DebugCamera;
class GameObject;
class Graphics;
class InputManager;
class PrefabManager;
class Scene;

// Unity風のHierarchyとInspectorを表示する、デバッグ用Editor。
// ReleaseではDrawが何もしないため、ゲーム本体へEditor依存を持ち込まない。
class Editor {
public:
    enum class PlayModeRequest {
        None,
        Start,
        Stop
    };

    Editor() = default;
    ~Editor() = default;

    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    void Initialize(
        AssetManager* assetManager,
        PrefabManager* prefabManager,
        Graphics* graphics,
        InputManager* inputManager,
        int defaultTextureHandle,
        std::string defaultTextureGuid,
        std::string defaultModelGuid);
    void Draw(
        Scene& scene,
        bool isPlaying,
        const std::string& playModeMessage);

    // Toolbarで押されたPlay／Stopを、Sceneを使い終えたフレーム末尾で受け取る。
    PlayModeRequest ConsumePlayModeRequest();
    // Edit SceneとPlay Sceneを切り替えた時に、別Scene用のUndo履歴を破棄する。
    void ResetSceneContext(const Scene& scene);

    uint64_t GetSelectedGameObjectId() const {
        return selectedGameObjectId_;
    }
    bool IsViewportVisible() const { return viewportVisible_; }
    bool IsViewportHovered() const { return viewportHovered_; }
    uint32_t GetViewportWidth() const { return viewportWidth_; }
    uint32_t GetViewportHeight() const { return viewportHeight_; }

private:
    enum class TransformGizmoOperation {
        Translate,
        Rotate,
        Scale
    };

    enum class TransformGizmoAxis {
        None,
        X,
        Y,
        Z
    };

    enum class TransformGizmoSpace {
        Global,
        Local
    };

    // Hierarchyの描画中にSceneの配列や親子配列を書き換えると、
    // 走査中のiteratorが無効になる。操作内容だけを予約し、描画後に1回だけ実行する。
    enum class HierarchyActionType {
        None,
        CreateRoot,
        CreateChild,
        Duplicate,
        Delete,
        Reparent,
        Unparent
    };

    struct HierarchyAction {
        HierarchyActionType type = HierarchyActionType::None;
        uint64_t sourceId = 0;
        uint64_t targetId = 0;
    };

    // 1回のEditor操作の前後を保持するUndo/Redo履歴。
    // JSONなのでGameObject ID・親子関係・Component設定もまとめて復元できる。
    struct HistoryEntry {
        std::string label;
        std::string beforeSnapshot;
        std::string afterSnapshot;
        uint64_t selectionBefore = 0;
        uint64_t selectionAfter = 0;
    };

    void DrawHierarchy(Scene& scene);
    void DrawDockSpace();
    void DrawViewportWindows(Scene& scene, bool isPlaying);
    void DrawSceneGizmos(
        Scene& scene,
        float viewportLeft,
        float viewportTop,
        float viewportWidth,
        float viewportHeight);
    void DrawTransformGizmo(
        Scene& scene,
        GameObject& gameObject,
        DebugCamera& sceneCamera,
        const Matrix4x4& viewProjection,
        float viewportLeft,
        float viewportTop,
        float viewportWidth,
        float viewportHeight);
    void BeginTransformGizmo(
        Scene& scene,
        GameObject& gameObject,
        TransformGizmoOperation operation,
        TransformGizmoAxis axis,
        bool keyboardMode,
        float mouseX,
        float mouseY,
        float centerX,
        float centerY);
    void UpdateTransformGizmo(
        Scene& scene,
        GameObject& gameObject,
        DebugCamera& sceneCamera,
        const Matrix4x4& viewProjection,
        float viewportLeft,
        float viewportTop,
        float viewportWidth,
        float viewportHeight,
        float worldLength);
    void EndTransformGizmo(
        Scene& scene,
        GameObject& gameObject,
        bool cancel);
    Vector3 GetTransformGizmoAxis(
        const GameObject& gameObject,
        TransformGizmoAxis axis) const;
    void DrawPlayModeToolbar(
        bool isPlaying,
        const std::string& playModeMessage);
    void DrawSceneFileControls(Scene& scene);
    void DrawHierarchyNode(Scene& scene, GameObject& gameObject);
    void DrawHierarchyEmptyArea(Scene& scene);
    void DrawHierarchyRenamePopup(Scene& scene);
    void ExecuteHierarchyAction(Scene& scene);
    void BeginHierarchyRename(const GameObject& gameObject);
    GameObject* DuplicateGameObjectHierarchy(
        Scene& scene,
        const GameObject& source,
        GameObject* duplicateParent,
        bool isDuplicateRoot,
        bool& skippedComponent);
    void CopyComponentsForDuplicate(
        const GameObject& source,
        GameObject& destination,
        bool& skippedComponent);
    std::string MakeDuplicateName(
        const Scene& scene,
        const std::string& sourceName) const;
    void HandleUndoRedoShortcuts(Scene& scene);
    bool CaptureSceneSnapshot(
        const Scene& scene,
        std::string& snapshot);
    void CommitHistory(
        Scene& scene,
        const std::string& label,
        std::string beforeSnapshot,
        uint64_t selectionBefore);
    bool Undo(Scene& scene);
    bool Redo(Scene& scene);
    void ClearHistory();
    bool CanUndo() const { return historyCursor_ > 0; }
    bool CanRedo() const { return historyCursor_ < history_.size(); }
    void HandleTransformHistoryItem(Scene& scene, GameObject& gameObject);
    void DrawInspector(Scene& scene);
    bool DrawPrefabInspector(Scene& scene, GameObject& gameObject);
    void DrawTransformInspector(Scene& scene, GameObject& gameObject);
    void DrawComponentInspector(Component& component);
    void DrawAddComponent(GameObject& gameObject, Scene& scene);

    AssetManager* assetManager_ = nullptr;
    PrefabManager* prefabManager_ = nullptr;
    Graphics* graphics_ = nullptr;
    InputManager* inputManager_ = nullptr;
    int defaultTextureHandle_ = -1;
    std::string defaultTextureGuid_;
    uint64_t selectedGameObjectId_ = 0;
    uint64_t renamingGameObjectId_ = 0;
    bool focusHierarchyRename_ = false;
    bool openHierarchyRenamePopup_ = false;
    HierarchyAction hierarchyAction_{};
    std::array<char, 128> hierarchyRenameBuffer_{};
    std::vector<HistoryEntry> history_;
    size_t historyCursor_ = 0;
    std::string transformBeforeSnapshot_;
    uint64_t transformEditingGameObjectId_ = 0;
    uint64_t transformSelectionBefore_ = 0;
    int addComponentType_ = 0;
    std::string addModelGuid_;
    std::array<char, 260> scenePath_{};
    SceneSerializer sceneSerializer_;
    ShaderGraphEditor shaderGraphEditor_;
    std::string lastMessage_;
    std::string sceneMessage_;
    bool isPlaying_ = false;
    bool resetDockLayoutRequested_ = false;
    bool viewportVisible_ = false;
    bool viewportHovered_ = false;
    bool hasViewportMode_ = false;
    bool lastViewportWasPlay_ = false;
    bool showSceneGizmos_ = true;
    bool transformGizmoHovered_ = false;
    bool transformGizmoActive_ = false;
    bool transformGizmoKeyboardMode_ = false;
    TransformGizmoOperation transformGizmoOperation_ =
        TransformGizmoOperation::Translate;
    TransformGizmoAxis transformGizmoAxis_ = TransformGizmoAxis::None;
    TransformGizmoSpace transformGizmoSpace_ =
        TransformGizmoSpace::Global;
    uint64_t transformGizmoGameObjectId_ = 0;
    TransformData transformGizmoInitialTransform_{};
    Matrix4x4 transformGizmoInitialWorldMatrix_ = MakeIdentity4x4();
    Vector3 transformGizmoInitialWorldPosition_{};
    float transformGizmoStartMouseX_ = 0.0f;
    float transformGizmoStartMouseY_ = 0.0f;
    float transformGizmoCenterX_ = 0.0f;
    float transformGizmoCenterY_ = 0.0f;
    std::string transformGizmoBeforeSnapshot_;
    uint32_t viewportWidth_ = 1;
    uint32_t viewportHeight_ = 1;
    PlayModeRequest playModeRequest_ = PlayModeRequest::None;
};
