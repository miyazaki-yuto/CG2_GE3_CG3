#include "PlayModeManager.h"

#include "Graphics.h"
#include "Scene.h"

#include <utility>

PlayModeManager::~PlayModeManager() = default;

void PlayModeManager::Initialize(
    AssetManager* assetManager,
    Graphics* graphics,
    InputManager* inputManager,
    int defaultTextureHandle,
    std::string defaultTextureGuid) {
    graphics_ = graphics;
    sceneSerializer_.Initialize(
        assetManager,
        graphics,
        inputManager,
        defaultTextureHandle,
        std::move(defaultTextureGuid));
}

bool PlayModeManager::StartPlay(
    Scene& editScene,
    std::string& resultMessage) {
    if (IsPlaying()) {
        resultMessage = "Play Mode is already running.";
        return false;
    }

    // ファイルへ保存せず、現在編集中の状態をメモリ上のJSONへ退避する。
    // このJSONから実行用Sceneを作るため、GameObject IDや親子関係も維持される。
    std::string snapshot;
    if (!sceneSerializer_.SerializeToString(
        editScene, snapshot, resultMessage)) {
        return false;
    }

    // Edit SceneのLightなどが外部Managerへ登録されたまま複製すると二重登録になる。
    // enabledフラグは変更せず、実行時コールバックだけを一時停止する。
    editScene.SuspendRuntimeCallbacks();

    auto newPlayScene = std::make_unique<Scene>(editScene.GetName());
    if (!sceneSerializer_.DeserializeFromString(
        *newPlayScene, snapshot, resultMessage)) {
        editScene.ResumeRuntimeCallbacks();
        return false;
    }

    newPlayScene->SetRuntimeUpdateEnabled(true);
    editSceneSnapshot_ = std::move(snapshot);
    playScene_ = std::move(newPlayScene);
    mode_ = Mode::Play;
    resultMessage = "Play Mode started. Scene was cloned in memory.";
    return true;
}

bool PlayModeManager::StopPlay(
    Scene& editScene,
    std::string& resultMessage) {
    if (!IsPlaying() || playScene_ == nullptr) {
        resultMessage = "Play Mode is not running.";
        return false;
    }

    // 描画中のComponentをGPUが参照し終えてから、実行用Sceneを破棄する
    if (graphics_ != nullptr) {
        graphics_->FlushGpu();
    }
    playScene_->SetRuntimeUpdateEnabled(false);
    playScene_.reset();
    mode_ = Mode::Edit;

    // 開始前のスナップショットを復元する
    // これによりPlay中のTransform変更、Component追加、GameObject生成などは残らない
    if (!sceneSerializer_.DeserializeFromString(
        editScene, editSceneSnapshot_, resultMessage)) {
        // スナップショットはStartPlay時に検証済みだが、万一の失敗時も
        // Edit SceneのLightなどを再登録し、編集を継続できるようにする
        editScene.ResumeRuntimeCallbacks();
        editScene.SetRuntimeUpdateEnabled(false);
        editSceneSnapshot_.clear();
        return false;
    }

    editScene.SetRuntimeUpdateEnabled(false);
    editSceneSnapshot_.clear();
    resultMessage = "Play Mode stopped. Runtime changes were discarded.";
    return true;
}

Scene& PlayModeManager::GetActiveScene(Scene& editScene) const {
    if (IsPlaying() && playScene_ != nullptr) {
        return *playScene_;
    }
    return editScene;
}
