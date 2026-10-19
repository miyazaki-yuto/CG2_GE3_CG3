#pragma once

#include <string>

class AssetManager;
class GameObject;
class Graphics;
class InputManager;
class Scene;

// SceneとGameObject群をJSONファイルへ保存・復元するクラス。
// GPUリソースの生成はGraphicsへ任せ、Serializerは構造と設定値だけを扱う。
class SceneSerializer {
public:
    void Initialize(
        AssetManager* assetManager,
        Graphics* graphics,
        InputManager* inputManager,
        int defaultTextureHandle,
        std::string defaultTextureGuid);

    bool Save(
        const Scene& scene,
        const std::string& filePath,
        std::string& resultMessage) const;
    bool Load(
        Scene& scene,
        const std::string& filePath,
        std::string& resultMessage) const;

    // Undo/Redo用。ファイルを経由せず、Scene全体をメモリ上のJSONへ変換・復元する。
    bool SerializeToString(
        const Scene& scene,
        std::string& json,
        std::string& resultMessage) const;
    bool SerializeHierarchyToString(
        const Scene& scene,
        const GameObject& hierarchyRoot,
        std::string& json,
        std::string& resultMessage) const;
    bool DeserializeFromString(
        Scene& scene,
        const std::string& json,
        std::string& resultMessage,
        bool applySceneSettings = true) const;

private:
    bool SerializeToStringInternal(
        const Scene& scene,
        const GameObject* hierarchyRoot,
        std::string& json,
        std::string& resultMessage) const;

    AssetManager* assetManager_ = nullptr;
    Graphics* graphics_ = nullptr;
    InputManager* inputManager_ = nullptr;
    int defaultTextureHandle_ = -1;
    std::string defaultTextureGuid_;
};
