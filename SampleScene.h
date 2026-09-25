#pragma once

#include "GameObject.h"

#include <string>

class AssetManager;
class Graphics;
class InputManager;
class Scene;

namespace SampleScene {

struct GameObjectIds {
    GameObject::Id triangles[2]{};
    GameObject::Id model = 0;
    GameObject::Id skySphere = 0;
};

bool LoadStartup(
    Scene& scene,
    AssetManager& assetManager,
    Graphics& graphics,
    InputManager* inputManager,
    int defaultTextureHandle,
    const std::string& defaultTextureGuid,
    std::string& message);

void ResolveGameObjectIds(
    const Scene& scene,
    GameObjectIds& ids);

} // namespace SampleScene
