#include "PrefabManager.h"

#include "AssetManager.h"
#include "Graphics.h"
#include "InputManager.h"
#include "PrefabInstanceComponent.h"
#include "Scene.h"
#include "SceneSerializer.h"
#include "TransformComponent.h"

#include <fstream>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

std::filesystem::path MakePathFromUtf8(const std::string& text) {
    const auto* first = reinterpret_cast<const char8_t*>(text.data());
    const std::u8string utf8(first, first + text.size());
    return std::filesystem::path(utf8);
}

std::string MakeUtf8FromPath(const std::filesystem::path& path) {
    const std::u8string utf8 = path.generic_u8string();
    return std::string(
        reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

std::string ReadTextFile(const std::string& filePath) {
    std::ifstream input(MakePathFromUtf8(filePath), std::ios::binary);
    if (!input.is_open()) {
        return {};
    }
    return std::string(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
}

} // namespace

PrefabManager::PrefabManager()
    : sceneSerializer_(std::make_unique<SceneSerializer>()) {
}

PrefabManager::~PrefabManager() = default;

void PrefabManager::Initialize(
    AssetManager* assetManager,
    Graphics* graphics,
    InputManager* inputManager,
    int defaultTextureHandle,
    std::string defaultTextureGuid) {
    assetManager_ = assetManager;
    sceneSerializer_->Initialize(
        assetManager,
        graphics,
        inputManager,
        defaultTextureHandle,
        std::move(defaultTextureGuid));
    prefabWriteTimes_.clear();
    lastUpdateCheck_ = std::chrono::steady_clock::now();
    lastError_.clear();

    // Prefabの標準保存先を先に用意しておく。
    // これにより、初めて「Save As Prefab」を選んだ場合でも
    // ファイル選択ダイアログをResources/Prefabsから開始できる。
    std::error_code directoryError;
    std::filesystem::create_directories("Resources/Prefabs", directoryError);
}

std::string PrefabManager::SavePrefab(
    GameObject& root,
    const std::string& filePath,
    std::string& resultMessage) {
    lastError_.clear();
    if (assetManager_ == nullptr || root.GetScene() == nullptr) {
        resultMessage = "PrefabManager is not initialized.";
        lastError_ = resultMessage;
        return {};
    }

    std::filesystem::path outputPath = MakePathFromUtf8(filePath);
    if (outputPath.extension().empty()) {
        outputPath += L".prefab";
    }
    if (outputPath.extension() != L".prefab") {
        resultMessage = "Prefab file extension must be .prefab.";
        lastError_ = resultMessage;
        return {};
    }

    std::string json;
    if (!sceneSerializer_->SerializeHierarchyToString(
        *root.GetScene(), root, json, resultMessage)) {
        lastError_ = resultMessage;
        return {};
    }

    std::error_code errorCode;
    if (outputPath.has_parent_path()) {
        std::filesystem::create_directories(
            outputPath.parent_path(), errorCode);
    }
    if (errorCode) {
        resultMessage = "Prefab directory could not be created.";
        lastError_ = resultMessage;
        return {};
    }

    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        resultMessage = "Prefab file could not be opened for writing.";
        lastError_ = resultMessage;
        return {};
    }
    output.write(json.data(), static_cast<std::streamsize>(json.size()));
    output.close();
    if (!output.good()) {
        resultMessage = "Prefab file could not be written.";
        lastError_ = resultMessage;
        return {};
    }

    // 既存Prefabを上書きした場合は、移動検出用の.meta内容ハッシュも更新する。
    assetManager_->RefreshAssets();
    std::string importError;
    const std::string normalizedPath = MakeUtf8FromPath(outputPath);
    const std::string prefabGuid = assetManager_->ImportPrefab(
        normalizedPath, &importError);
    if (prefabGuid.empty()) {
        resultMessage = importError;
        lastError_ = resultMessage;
        return {};
    }

    PrefabInstanceComponent* instance =
        root.GetComponent<PrefabInstanceComponent>();
    if (instance == nullptr) {
        instance = root.AddComponent<PrefabInstanceComponent>(prefabGuid, true);
    } else {
        instance->SetPrefabGuid(prefabGuid);
    }
    instance->MarkRuntimeSynchronized();

    std::filesystem::file_time_type writeTime{};
    if (GetPrefabWriteTime(prefabGuid, writeTime)) {
        prefabWriteTimes_[prefabGuid] = writeTime;
    }
    resultMessage = "Prefab saved: " + normalizedPath;
    return prefabGuid;
}

GameObject* PrefabManager::Instantiate(
    Scene& scene,
    const std::string& prefabGuid,
    GameObject* parent,
    std::string& resultMessage) {
    return InstantiateInternal(scene, prefabGuid, parent, 0, resultMessage);
}

GameObject* PrefabManager::InstantiateInternal(
    Scene& scene,
    const std::string& prefabGuid,
    GameObject* parent,
    GameObject::Id forcedRootId,
    std::string& resultMessage) {
    lastError_.clear();
    Scene prefabScene("Prefab");
    GameObject::Id sourceRootId = 0;
    std::string prefabPath;
    if (!LoadPrefabHierarchy(
        prefabGuid,
        prefabScene,
        sourceRootId,
        prefabPath,
        resultMessage)) {
        return nullptr;
    }

    GameObject* instanceRoot = scene.AppendHierarchy(
        std::move(prefabScene), sourceRootId, parent, forcedRootId);
    if (instanceRoot == nullptr) {
        resultMessage = "Prefab hierarchy could not be added to the Scene.";
        lastError_ = resultMessage;
        return nullptr;
    }

    ConfigureInstanceRoot(*instanceRoot, prefabGuid);
    resultMessage = "Prefab instantiated: " + prefabPath;
    return instanceRoot;
}

bool PrefabManager::LoadPrefabHierarchy(
    const std::string& prefabGuid,
    Scene& prefabScene,
    GameObject::Id& sourceRootId,
    std::string& prefabPath,
    std::string& resultMessage) {
    prefabPath = ResolvePrefabPath(prefabGuid);
    if (prefabPath.empty()) {
        resultMessage = "Prefab GUID could not be resolved: " + prefabGuid;
        lastError_ = resultMessage;
        return false;
    }

    const std::string json = ReadTextFile(prefabPath);
    if (json.empty()) {
        resultMessage = "Prefab file could not be read: " + prefabPath;
        lastError_ = resultMessage;
        return false;
    }

    if (!sceneSerializer_->DeserializeFromString(
        prefabScene, json, resultMessage, false)) {
        lastError_ = resultMessage;
        return false;
    }

    GameObject* sourceRoot = nullptr;
    for (const std::unique_ptr<GameObject>& gameObject :
        prefabScene.GetGameObjects()) {
        if (gameObject->GetParent() == nullptr) {
            if (sourceRoot != nullptr) {
                resultMessage = "Prefab must contain exactly one root GameObject.";
                lastError_ = resultMessage;
                return false;
            }
            sourceRoot = gameObject.get();
        }
    }
    if (sourceRoot == nullptr) {
        resultMessage = "Prefab does not contain a root GameObject.";
        lastError_ = resultMessage;
        return false;
    }

    sourceRootId = sourceRoot->GetId();
    return true;
}

void PrefabManager::ConfigureInstanceRoot(
    GameObject& instanceRoot,
    const std::string& prefabGuid) {
    PrefabInstanceComponent* instance =
        instanceRoot.GetComponent<PrefabInstanceComponent>();
    if (instance == nullptr) {
        instance = instanceRoot.AddComponent<PrefabInstanceComponent>(
            prefabGuid, true);
    } else {
        instance->SetPrefabGuid(prefabGuid);
    }
    instance->MarkRuntimeSynchronized();

    std::filesystem::file_time_type writeTime{};
    if (GetPrefabWriteTime(prefabGuid, writeTime)) {
        prefabWriteTimes_[prefabGuid] = writeTime;
    }
}

bool PrefabManager::ApplyPrefab(
    Scene& scene,
    GameObject& instanceRoot,
    std::string& resultMessage) {
    PrefabInstanceComponent* instance =
        instanceRoot.GetComponent<PrefabInstanceComponent>();
    if (instance == nullptr || instance->GetPrefabGuid().empty()) {
        resultMessage = "Selected GameObject is not a Prefab instance root.";
        lastError_ = resultMessage;
        return false;
    }

    const std::string prefabGuid = instance->GetPrefabGuid();
    const std::string prefabPath = ResolvePrefabPath(prefabGuid);
    if (prefabPath.empty()) {
        resultMessage = "Prefab GUID could not be resolved: " + prefabGuid;
        lastError_ = resultMessage;
        return false;
    }

    const std::string savedGuid = SavePrefab(
        instanceRoot, prefabPath, resultMessage);
    if (savedGuid.empty()) {
        return false;
    }
    RefreshInstances(scene, savedGuid, resultMessage);
    return true;
}

GameObject* PrefabManager::RefreshInstance(
    Scene& scene,
    GameObject& instanceRoot,
    std::string& resultMessage) {
    PrefabInstanceComponent* instance =
        instanceRoot.GetComponent<PrefabInstanceComponent>();
    if (instance == nullptr || instance->GetPrefabGuid().empty()) {
        resultMessage = "Selected GameObject is not a Prefab instance root.";
        lastError_ = resultMessage;
        return nullptr;
    }

    const std::string prefabGuid = instance->GetPrefabGuid();
    const bool autoUpdate = instance->IsAutoUpdateEnabled();
    const GameObject::Id rootId = instanceRoot.GetId();
    const GameObject::Id parentId = instanceRoot.GetParent() != nullptr
        ? instanceRoot.GetParent()->GetId() : 0;
    const TransformData localTransform =
        instanceRoot.GetTransform().GetLocalTransform();
    const bool active = instanceRoot.IsActive();
    const std::string instanceName = instanceRoot.GetName();

    // 先にPrefabを完全に読み込んで検証する。壊れたファイルなら現在の実体を残す。
    Scene prefabScene("Prefab");
    GameObject::Id sourceRootId = 0;
    std::string prefabPath;
    if (!LoadPrefabHierarchy(
        prefabGuid,
        prefabScene,
        sourceRootId,
        prefabPath,
        resultMessage)) {
        return nullptr;
    }

    scene.DestroyGameObjectImmediate(instanceRoot);
    GameObject* parent = parentId != 0 ? scene.FindGameObject(parentId) : nullptr;
    GameObject* replacement = scene.AppendHierarchy(
        std::move(prefabScene), sourceRootId, parent, rootId);
    if (replacement != nullptr) {
        ConfigureInstanceRoot(*replacement, prefabGuid);
        replacement->GetComponent<PrefabInstanceComponent>()
            ->SetAutoUpdateEnabled(autoUpdate);
        // 配置はインスタンス固有値として維持し、内部Componentと子階層だけを更新する。
        replacement->GetTransform().GetLocalTransform() = localTransform;
        replacement->SetActive(active);
        replacement->SetName(instanceName);
        resultMessage = "Prefab instance refreshed: " + prefabPath;
    } else {
        resultMessage = "Prefab hierarchy could not be added to the Scene.";
        lastError_ = resultMessage;
    }
    return replacement;
}

size_t PrefabManager::RefreshInstances(
    Scene& scene,
    const std::string& prefabGuid,
    std::string& resultMessage) {
    std::vector<GameObject::Id> instanceIds;
    for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
        const PrefabInstanceComponent* instance =
            gameObject->GetComponent<PrefabInstanceComponent>();
        if (instance != nullptr && instance->IsAutoUpdateEnabled() &&
            instance->GetPrefabGuid() == prefabGuid) {
            instanceIds.push_back(gameObject->GetId());
        }
    }

    size_t refreshedCount = 0;
    for (const GameObject::Id id : instanceIds) {
        GameObject* current = scene.FindGameObject(id);
        if (current != nullptr &&
            RefreshInstance(scene, *current, resultMessage) != nullptr) {
            ++refreshedCount;
        }
    }
    resultMessage = "Prefab instances refreshed: " +
        std::to_string(refreshedCount);
    return refreshedCount;
}

void PrefabManager::Update(Scene& scene) {
    const auto now = std::chrono::steady_clock::now();
    if (now - lastUpdateCheck_ < std::chrono::milliseconds(500)) {
        return;
    }
    lastUpdateCheck_ = now;

    std::unordered_set<std::string> prefabGuids;
    std::unordered_set<std::string> needsInitialSynchronization;
    for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
        const PrefabInstanceComponent* instance =
            gameObject->GetComponent<PrefabInstanceComponent>();
        if (instance != nullptr && instance->IsAutoUpdateEnabled() &&
            !instance->GetPrefabGuid().empty()) {
            prefabGuids.insert(instance->GetPrefabGuid());
            if (!instance->IsRuntimeSynchronized()) {
                needsInitialSynchronization.insert(
                    instance->GetPrefabGuid());
            }
        }
    }

    for (const std::string& guid : prefabGuids) {
        std::filesystem::file_time_type currentWriteTime{};
        if (!GetPrefabWriteTime(guid, currentWriteTime)) {
            continue;
        }
        if (needsInitialSynchronization.contains(guid)) {
            prefabWriteTimes_[guid] = currentWriteTime;
            std::string message;
            RefreshInstances(scene, guid, message);
            continue;
        }
        const auto previous = prefabWriteTimes_.find(guid);
        if (previous == prefabWriteTimes_.end()) {
            prefabWriteTimes_[guid] = currentWriteTime;
            continue;
        }
        if (previous->second == currentWriteTime) {
            continue;
        }

        previous->second = currentWriteTime;
        std::string message;
        RefreshInstances(scene, guid, message);
    }
}

std::string PrefabManager::ResolvePrefabPath(
    const std::string& prefabGuid) {
    if (assetManager_ == nullptr || prefabGuid.empty()) {
        return {};
    }
    std::string path = assetManager_->GetAssetPath(prefabGuid);
    std::error_code errorCode;
    if (!path.empty() && std::filesystem::is_regular_file(
        MakePathFromUtf8(path), errorCode)) {
        return path;
    }

    // Prefabと.metaが移動された場合はAssetデータベースを再走査する。
    assetManager_->RefreshAssets();
    path = assetManager_->GetAssetPath(prefabGuid);
    return path;
}

bool PrefabManager::GetPrefabWriteTime(
    const std::string& prefabGuid,
    std::filesystem::file_time_type& writeTime) {
    const std::string path = ResolvePrefabPath(prefabGuid);
    if (path.empty()) {
        return false;
    }
    std::error_code errorCode;
    writeTime = std::filesystem::last_write_time(
        MakePathFromUtf8(path), errorCode);
    return !errorCode;
}
