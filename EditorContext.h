#pragma once

#include <string>

class AudioManager;
class Scene;

// Editorの各Panelが1フレーム中だけ参照する実行状態。
// 永続状態はEditorや各Managerが所有し、Contextは所有しない。
struct EditorContext {
    Scene& scene;
    bool isPlaying = false;
    const std::string& playModeMessage;
    AudioManager* audioManager = nullptr;
    int bgmHandle = -1;
    float* bgmVolume = nullptr;
};
