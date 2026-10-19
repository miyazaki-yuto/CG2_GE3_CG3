#pragma once

#include "GameObject.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

class AssetManager;
class Graphics;
class InputManager;
class Scene;
class SceneSerializer;

// GameObject階層を再利用可能な.prefabとして保存・生成・更新する。
class PrefabManager {
public:
    PrefabManager();
    ~PrefabManager();

    PrefabManager(const PrefabManager&) = delete;
    PrefabManager& operator=(const PrefabManager&) = delete;

    void Initialize(
        AssetManager* assetManager,
        Graphics* graphics,
        InputManager* inputManager,
        int defaultTextureHandle = -1,
        std::string defaultTextureGuid = {});

    // rootとその全子孫だけを.prefabへ保存し、Prefab GUIDを返す。
    std::string SavePrefab(
        GameObject& root,
        const std::string& filePath,
        std::string& resultMessage);

    // 同じPrefab GUIDから何個でも独立したGameObject階層を生成できる。
    GameObject* Instantiate(
        Scene& scene,
        const std::string& prefabGuid,
        GameObject* parent,
        std::string& resultMessage);

    // 選択中インスタンスの内容を元Prefabへ書き戻し、全インスタンスへ反映する。
    bool ApplyPrefab(
        Scene& scene,
        GameObject& instanceRoot,
        std::string& resultMessage);

    // 元Prefabの現在内容からインスタンスを作り直す。
    GameObject* RefreshInstance(
        Scene& scene,
        GameObject& instanceRoot,
        std::string& resultMessage);
    size_t RefreshInstances(
        Scene& scene,
        const std::string& prefabGuid,
        std::string& resultMessage);

    // .prefabの更新時刻を監視し、外部編集された場合も自動反映する。
    void Update(Scene& scene);

    const std::string& GetLastError() const { return lastError_; }

private:
    GameObject* InstantiateInternal(
        Scene& scene,
        const std::string& prefabGuid,
        GameObject* parent,
        GameObject::Id forcedRootId,
        std::string& resultMessage);
    bool LoadPrefabHierarchy(
        const std::string& prefabGuid,
        Scene& prefabScene,
        GameObject::Id& sourceRootId,
        std::string& prefabPath,
        std::string& resultMessage);
    void ConfigureInstanceRoot(
        GameObject& instanceRoot,
        const std::string& prefabGuid);
    std::string ResolvePrefabPath(const std::string& prefabGuid);
    bool GetPrefabWriteTime(
        const std::string& prefabGuid,
        std::filesystem::file_time_type& writeTime);

    AssetManager* assetManager_ = nullptr;
    std::unique_ptr<SceneSerializer> sceneSerializer_;
    std::unordered_map<std::string, std::filesystem::file_time_type>
        prefabWriteTimes_;
    std::chrono::steady_clock::time_point lastUpdateCheck_{};
    std::string lastError_;
};
