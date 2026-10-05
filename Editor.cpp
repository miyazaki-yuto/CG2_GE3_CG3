#include "Editor.h"

#include "GameObject.h"
#include "Graphics.h"
#include "Scene.h"

#include <algorithm>
#include <iterator>
#include <utility>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

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
    history_.Initialize(&sceneSerializer_);
    shaderGraphEditor_.Initialize(graphics_);

    addModelGuid_ = std::move(defaultModelGuid);

    constexpr char kDefaultScenePath[] = "Resources/Scenes/MainScene.json";
    std::copy(
        std::begin(kDefaultScenePath),
        std::end(kDefaultScenePath),
        scenePath_.begin());
}

void Editor::Draw(EditorContext& context) {
#ifdef USE_IMGUI
    Scene& scene = context.scene;
    isPlaying_ = context.isPlaying;
    DrawDockSpace();
    DrawViewportWindows(scene, context.isPlaying);
    DrawPlayModeToolbar(context.isPlaying, context.playModeMessage);
    HandleUndoRedoShortcuts(scene);
    DrawHierarchy(scene);
    DrawInspector(scene);
    performancePanel_.Draw();
    lightingPanel_.Draw(
        graphics_, graphics_ != nullptr
        ? graphics_->GetLightingManager()
        : nullptr);
    soundPanel_.Draw(
        context.audioManager,
        context.bgmHandle,
        context.bgmVolume,
        context.isPlaying);
    shaderGraphEditor_.Draw();
#else
    (void)context;
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
    return history_.Capture(scene, snapshot, lastMessage_);
}

void Editor::CommitHistory(
    Scene& scene,
    const std::string& label,
    std::string beforeSnapshot,
    uint64_t selectionBefore) {
    history_.Commit(
        scene,
        label,
        std::move(beforeSnapshot),
        selectionBefore,
        selectedGameObjectId_,
        lastMessage_);
}

bool Editor::Undo(Scene& scene) {
    if (transformGizmoActive_) {
        if (GameObject* gameObject =
            scene.FindGameObject(transformGizmoGameObjectId_)) {
            EndTransformGizmo(scene, *gameObject, true);
        }
        return false;
    }
    if (!history_.Undo(scene, selectedGameObjectId_, lastMessage_)) {
        return false;
    }
    hierarchyAction_ = {};
    renamingGameObjectId_ = 0;
    openHierarchyRenamePopup_ = false;
    focusHierarchyRename_ = false;
    transformBeforeSnapshot_.clear();
    transformEditingGameObjectId_ = 0;
    transformSelectionBefore_ = 0;
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
    if (!history_.Redo(scene, selectedGameObjectId_, lastMessage_)) {
        return false;
    }
    hierarchyAction_ = {};
    renamingGameObjectId_ = 0;
    openHierarchyRenamePopup_ = false;
    focusHierarchyRename_ = false;
    transformBeforeSnapshot_.clear();
    transformEditingGameObjectId_ = 0;
    transformSelectionBefore_ = 0;
    return true;
}

void Editor::ClearHistory() {
    history_.Clear();
    hierarchyAction_ = {};
    renamingGameObjectId_ = 0;
    openHierarchyRenamePopup_ = false;
    focusHierarchyRename_ = false;
    transformBeforeSnapshot_.clear();
    transformEditingGameObjectId_ = 0;
    transformSelectionBefore_ = 0;
}

#endif
