#pragma once

#ifdef _DEBUG

class AssetManager;
class LightingManager;
class PlayModeManager;
class PrefabManager;
class Scene;

namespace DebugSelfTests {

void RunPrefab(
    Scene& scene,
    PrefabManager& prefabManager,
    AssetManager& assetManager);

void RunPlayMode(
    Scene& editScene,
    PlayModeManager& playModeManager,
    LightingManager& lightingManager);

} // namespace DebugSelfTests

#endif
