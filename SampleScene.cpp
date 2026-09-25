#include "SampleScene.h"

#include "AssetManager.h"
#include "Graphics.h"
#include "InputManager.h"
#include "Scene.h"
#include "SceneSerializer.h"

namespace SampleScene {
namespace {

constexpr char kStartupScenePath[] =
    "Resources/Scenes/MainScene.json";

} // namespace

bool LoadStartup(
    Scene& scene,
    AssetManager& assetManager,
    Graphics& graphics,
    InputManager* inputManager,
    int defaultTextureHandle,
    const std::string& defaultTextureGuid,
    std::string& message) {
    SceneSerializer loader;
    loader.Initialize(
        &assetManager,
        &graphics,
        inputManager,
        defaultTextureHandle,
        defaultTextureGuid);
    return loader.Load(scene, kStartupScenePath, message);
}

void ResolveGameObjectIds(
    const Scene& scene,
    GameObjectIds& ids) {
    const auto findId = [&scene](const char* name) {
        if (const GameObject* object = scene.FindGameObject(name)) {
            return object->GetId();
        }
        return GameObject::Id{ 0 };
    };

    ids.triangles[0] = findId("Triangle 1");
    ids.triangles[1] = findId("Triangle 2");
    ids.model = findId("Cube Model");
    ids.skySphere = findId("Sky Sphere");
}

} // namespace SampleScene
