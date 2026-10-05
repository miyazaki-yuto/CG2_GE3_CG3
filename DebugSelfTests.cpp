#include "DebugSelfTests.h"

#ifdef _DEBUG

#include "AssetManager.h"
#include "GameObject.h"
#include "LightingManager.h"
#include "Model.h"
#include "PlayModeManager.h"
#include "PrefabInstanceComponent.h"
#include "PrefabManager.h"
#include "Scene.h"

#include <Windows.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

namespace DebugSelfTests {

void RunSkeletalAnimation() {
    char enabled[2]{};
    if (GetEnvironmentVariableA(
        "CG2_SKELETAL_SELF_TEST",
        enabled,
        static_cast<DWORD>(std::size(enabled))) == 0) {
        return;
    }

    const std::filesystem::path modelPath =
        "externals/assimp/test/models/glTF2/simple_skin/simple_skin.gltf";
    assert(std::filesystem::exists(modelPath));
    Model model;
    std::vector<TextureVertexData> vertices;
    std::vector<uint32_t> indices;
    std::string texturePath;
    assert(model.LoadModelFile(
        modelPath.generic_string(), vertices, indices, texturePath));
    assert(!vertices.empty() && !indices.empty());
    assert(model.GetBoneCount() > 0);
    assert(model.GetAnimationCount() > 0);
    const ModelAnimationClip* clip = model.GetAnimation(0);
    assert(clip != nullptr && clip->durationSeconds > 0.0f);
    ModelPose pose;
    assert(model.EvaluateAnimation(0, clip->durationSeconds * 0.5f, pose));
    assert(pose.nodeModelTransforms.size() == model.GetNodeCount());
    OutputDebugStringA("Skeletal animation self-test succeeded.\n");
}

void RunPrefab(
    Scene& scene,
    PrefabManager& prefabManager,
    AssetManager& assetManager) {
    char enabled[2]{};
    if (GetEnvironmentVariableA(
        "CG2_PREFAB_SELF_TEST",
        enabled,
        static_cast<DWORD>(std::size(enabled))) == 0) {
        return;
    }

    const std::filesystem::path prefabPath =
        "Resources/Prefabs/__PrefabSelfTest.prefab";
    std::filesystem::path metaPath = prefabPath;
    metaPath += ".meta";
    std::error_code errorCode;
    std::filesystem::remove(prefabPath, errorCode);
    errorCode.clear();
    std::filesystem::remove(metaPath, errorCode);
    assetManager.RefreshAssets();

    GameObject& source = scene.CreateGameObject("Prefab Self Test");
    GameObject& firstChild = scene.CreateGameObject("First Child");
    assert(firstChild.SetParent(&source));
    std::string message;
    const std::string prefabGuid = prefabManager.SavePrefab(
        source, prefabPath.generic_string(), message);
    assert(!prefabGuid.empty());

    GameObject* firstInstance = prefabManager.Instantiate(
        scene, prefabGuid, nullptr, message);
    GameObject* secondInstance = prefabManager.Instantiate(
        scene, prefabGuid, nullptr, message);
    assert(firstInstance != nullptr && secondInstance != nullptr);
    firstInstance->GetTransform().GetLocalTransform().translate.x = 10.0f;
    secondInstance->GetTransform().GetLocalTransform().translate.x = 20.0f;

    const GameObject::Id instanceIds[] = {
        source.GetId(), firstInstance->GetId(), secondInstance->GetId()
    };
    GameObject& addedChild = scene.CreateGameObject("Added Child");
    assert(addedChild.SetParent(&source));
    firstChild.SetName("Updated Child");
    assert(prefabManager.ApplyPrefab(scene, source, message));

    const float expectedPositions[] = { 0.0f, 10.0f, 20.0f };
    GameObject::Id previousFirstChildIds[std::size(instanceIds)]{};
    for (size_t index = 0; index < std::size(instanceIds); ++index) {
        GameObject* instance = scene.FindGameObject(instanceIds[index]);
        assert(instance != nullptr);
        assert(instance->GetChildren().size() == 2);
        assert(instance->GetChildren()[0]->GetName() == "Updated Child");
        previousFirstChildIds[index] = instance->GetChildren()[0]->GetId();
        assert(instance->GetTransform().GetLocalTransform().translate.x ==
            expectedPositions[index]);
        const PrefabInstanceComponent* marker =
            instance->GetComponent<PrefabInstanceComponent>();
        assert(marker != nullptr && marker->GetPrefabGuid() == prefabGuid);
    }

    // Editor外で.prefabが書き換わった場合も、更新時刻の監視で全実体へ反映する。
    {
        std::ofstream externalEdit(prefabPath, std::ios::app);
        externalEdit << '\n';
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    prefabManager.Update(scene);
    for (size_t index = 0; index < std::size(instanceIds); ++index) {
        GameObject* instance = scene.FindGameObject(instanceIds[index]);
        assert(instance != nullptr && instance->GetChildren().size() == 2);
        assert(instance->GetChildren()[0]->GetId() !=
            previousFirstChildIds[index]);
    }

    for (const GameObject::Id id : instanceIds) {
        if (GameObject* instance = scene.FindGameObject(id)) {
            scene.DestroyGameObjectImmediate(*instance);
        }
    }
    std::filesystem::remove(prefabPath, errorCode);
    errorCode.clear();
    std::filesystem::remove(metaPath, errorCode);
    assetManager.RefreshAssets();
    OutputDebugStringA("Prefab self-test succeeded.\n");
}

void RunPlayMode(
    Scene& editScene,
    PlayModeManager& playModeManager,
    LightingManager& lightingManager) {
    char enabled[2]{};
    if (GetEnvironmentVariableA(
        "CG2_PLAY_MODE_SELF_TEST",
        enabled,
        static_cast<DWORD>(std::size(enabled))) == 0) {
        return;
    }

    assert(!editScene.GetGameObjects().empty());
    const size_t editObjectCount = editScene.GetGameObjects().size();
    const GameObject& originalObject = *editScene.GetGameObjects().front();
    const GameObject::Id originalId = originalObject.GetId();
    const std::string originalName = originalObject.GetName();
    const Vector3 originalPosition =
        originalObject.GetTransform().GetLocalTransform().translate;
    const uint32_t directionalLightCount =
        lightingManager.GetDirectionalLightCount();
    const uint32_t pointLightCount = lightingManager.GetPointLightCount();
    const LightingMode originalLightingMode =
        lightingManager.GetLightingMode();
    lightingManager.SetLightingMode(LightingMode::Lambert);

    std::string message;
    assert(playModeManager.StartPlay(editScene, message));
    assert(playModeManager.IsPlaying());
    Scene& playScene = playModeManager.GetActiveScene(editScene);
    assert(&playScene != &editScene);
    assert(playScene.IsRuntimeUpdateEnabled());
    // Edit側を停止してからPlay側を登録するため、ライト数は二重にならない。
    assert(lightingManager.GetDirectionalLightCount() ==
        directionalLightCount);
    assert(lightingManager.GetPointLightCount() == pointLightCount);
    assert(lightingManager.GetLightingMode() == LightingMode::Lambert);

    // 実行用Sceneだけを書き換え、編集用Sceneへ変更が漏れないことを確認する。
    GameObject* runtimeObject = playScene.FindGameObject(originalId);
    assert(runtimeObject != nullptr);
    runtimeObject->SetName("Runtime Modified Object");
    runtimeObject->GetTransform().GetLocalTransform().translate.x += 100.0f;
    playScene.CreateGameObject("Runtime Only Object");
    lightingManager.SetLightingMode(LightingMode::HalfLambert);
    playScene.Update(1.0f / 60.0f);
    assert(editScene.FindGameObject("Runtime Only Object") == nullptr);

    assert(playModeManager.StopPlay(editScene, message));
    assert(!playModeManager.IsPlaying());
    assert(!editScene.IsRuntimeUpdateEnabled());
    assert(lightingManager.GetDirectionalLightCount() ==
        directionalLightCount);
    assert(lightingManager.GetPointLightCount() == pointLightCount);
    assert(lightingManager.GetLightingMode() == LightingMode::Lambert);
    assert(editScene.GetGameObjects().size() == editObjectCount);
    const GameObject* restoredObject = editScene.FindGameObject(originalId);
    assert(restoredObject != nullptr);
    assert(restoredObject->GetName() == originalName);
    const Vector3 restoredPosition =
        restoredObject->GetTransform().GetLocalTransform().translate;
    assert(restoredPosition.x == originalPosition.x);
    assert(restoredPosition.y == originalPosition.y);
    assert(restoredPosition.z == originalPosition.z);
    assert(editScene.FindGameObject("Runtime Only Object") == nullptr);
    lightingManager.SetLightingMode(originalLightingMode);
    OutputDebugStringA("Play Mode self-test succeeded.\n");
}

} // namespace DebugSelfTests

#endif
