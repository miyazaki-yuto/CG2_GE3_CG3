#pragma once

#include "SceneSerializer.h"

#include <memory>
#include <string>

class AssetManager;
class Graphics;
class InputManager;
class Scene;

// Editor用Sceneとゲーム実行用Sceneを分離して管理する。
// Play開始時にJSONを介してSceneを複製し、停止時には複製を破棄する。
class PlayModeManager {
public:
    enum class Mode {
        Edit,
        Play
    };

    PlayModeManager() = default;
    ~PlayModeManager();

    PlayModeManager(const PlayModeManager&) = delete;
    PlayModeManager& operator=(const PlayModeManager&) = delete;

    void Initialize(
        AssetManager* assetManager,
        Graphics* graphics,
        InputManager* inputManager,
        int defaultTextureHandle,
        std::string defaultTextureGuid);

    bool StartPlay(Scene& editScene, std::string& resultMessage);
    bool StopPlay(Scene& editScene, std::string& resultMessage);

    bool IsPlaying() const { return mode_ == Mode::Play; }
    Mode GetMode() const { return mode_; }

    // Edit中は編集用Scene、Play中は複製した実行用Sceneを返す。
    Scene& GetActiveScene(Scene& editScene) const;

private:
    SceneSerializer sceneSerializer_;
    Graphics* graphics_ = nullptr;
    std::unique_ptr<Scene> playScene_;
    std::string editSceneSnapshot_;
    Mode mode_ = Mode::Edit;
};
