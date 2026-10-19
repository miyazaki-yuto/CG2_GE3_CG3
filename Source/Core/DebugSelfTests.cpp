#include "DebugSelfTests.h"

#ifdef _DEBUG

#include "AssetManager.h"
#include "ColliderComponent.h"
#include "GameObject.h"
#include "LightingManager.h"
#include "Model.h"
#include "PlayModeManager.h"
#include "PrefabInstanceComponent.h"
#include "PrefabManager.h"
#include "Scene.h"
#include "SceneSerializer.h"
#include "RigidbodyComponent.h"
#include "VisualScriptComponent.h"

#include <Windows.h>

#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

namespace DebugSelfTests {

namespace {

class CollisionProbe final : public Component {
public:
    void OnCollisionEnter(ColliderComponent& /*other*/) override {
        ++enterCount;
    }
    void OnCollisionStay(ColliderComponent& /*other*/) override {
        ++stayCount;
    }
    void OnCollisionExit(ColliderComponent& /*other*/) override {
        ++exitCount;
    }

    int enterCount = 0;
    int stayCount = 0;
    int exitCount = 0;
};

} // namespace

void RunPhysics() {
    // 接地、めり込み解消、Enter/Stay/Exitを同じScene更新で確認する。
    Scene scene("Physics Self Test");
    scene.SetRuntimeUpdateEnabled(true);

    GameObject& floor = scene.CreateGameObject("Floor");
    ColliderComponent* floorCollider =
        floor.AddComponent<ColliderComponent>(
            nullptr, -1, ColliderComponent::ColliderType::Aabb);
    floorCollider->SetHalfExtents({ 5.0f, 0.5f, 5.0f });
    RigidbodyComponent* floorBody =
        floor.AddComponent<RigidbodyComponent>();
    floorBody->SetBodyType(RigidbodyComponent::BodyType::Static);

    GameObject& ball = scene.CreateGameObject("Ball");
    ball.GetTransform().SetWorldPosition({ 0.0f, 0.75f, 0.0f });
    ColliderComponent* ballCollider =
        ball.AddComponent<ColliderComponent>(
            nullptr, -1, ColliderComponent::ColliderType::Sphere);
    ballCollider->SetRadius(0.5f);
    RigidbodyComponent* ballBody =
        ball.AddComponent<RigidbodyComponent>();
    ballBody->SetUseGravity(false);
    ballBody->SetVelocity({ 0.0f, -1.0f, 0.0f });
    CollisionProbe* probe = ball.AddComponent<CollisionProbe>();

    scene.Update(1.0f / 60.0f);
    assert(probe->enterCount == 1);
    assert(ballBody->IsGrounded());
    assert(ball.GetTransform().GetWorldPosition().y > 0.99f);
    assert(std::abs(ballBody->GetVelocity().y) < 0.0001f);

    scene.Update(1.0f / 60.0f);
    assert(probe->stayCount == 1);
    ball.GetTransform().SetWorldPosition({ 0.0f, 3.0f, 0.0f });
    scene.Update(1.0f / 60.0f);
    assert(probe->exitCount == 1);

    // Triggerは通知だけで、位置と速度を補正しない。
    Scene triggerScene("Trigger Self Test");
    triggerScene.SetRuntimeUpdateEnabled(true);
    GameObject& trigger = triggerScene.CreateGameObject("Trigger");
    ColliderComponent* triggerCollider =
        trigger.AddComponent<ColliderComponent>(
            nullptr, -1, ColliderComponent::ColliderType::Aabb);
    triggerCollider->SetHalfExtents({ 5.0f, 0.5f, 5.0f });
    triggerCollider->SetTrigger(true);
    GameObject& triggerBall = triggerScene.CreateGameObject("Trigger Ball");
    triggerBall.GetTransform().SetWorldPosition({ 0.0f, 0.75f, 0.0f });
    triggerBall.AddComponent<ColliderComponent>(
        nullptr, -1, ColliderComponent::ColliderType::Sphere);
    RigidbodyComponent* triggerBody =
        triggerBall.AddComponent<RigidbodyComponent>();
    triggerBody->SetUseGravity(false);
    triggerBody->SetVelocity({ 0.0f, -1.0f, 0.0f });
    CollisionProbe* triggerProbe =
        triggerBall.AddComponent<CollisionProbe>();
    triggerScene.Update(1.0f / 60.0f);
    assert(triggerProbe->enterCount == 1);
    assert(std::abs(
        triggerBall.GetTransform().GetWorldPosition().y -
        (0.75f - 1.0f / 60.0f)) < 0.0001f);
    assert(std::abs(triggerBody->GetVelocity().y + 1.0f) < 0.0001f);

    // 両方の反発係数が1なら、床へ向かう速度が上向きへ反転する。
    Scene bounceScene("Bounce Self Test");
    bounceScene.SetRuntimeUpdateEnabled(true);
    GameObject& bounceFloor = bounceScene.CreateGameObject("Bounce Floor");
    ColliderComponent* bounceFloorCollider =
        bounceFloor.AddComponent<ColliderComponent>(
            nullptr, -1, ColliderComponent::ColliderType::Aabb);
    bounceFloorCollider->SetHalfExtents({ 5.0f, 0.5f, 5.0f });
    RigidbodyComponent* bounceFloorBody =
        bounceFloor.AddComponent<RigidbodyComponent>();
    bounceFloorBody->SetBodyType(RigidbodyComponent::BodyType::Static);
    bounceFloorBody->SetRestitution(1.0f);
    GameObject& bounceBall = bounceScene.CreateGameObject("Bounce Ball");
    bounceBall.GetTransform().SetWorldPosition({ 0.0f, 0.75f, 0.0f });
    bounceBall.AddComponent<ColliderComponent>(
        nullptr, -1, ColliderComponent::ColliderType::Sphere);
    RigidbodyComponent* bounceBody =
        bounceBall.AddComponent<RigidbodyComponent>();
    bounceBody->SetUseGravity(false);
    bounceBody->SetRestitution(1.0f);
    bounceBody->SetVelocity({ 0.0f, -2.0f, 0.0f });
    bounceScene.Update(1.0f / 60.0f);
    assert(bounceBody->GetVelocity().y > 1.99f);

    // Kinematicは重力や力を受けず、指定速度だけで移動する。
    Scene kinematicScene("Kinematic Self Test");
    kinematicScene.SetRuntimeUpdateEnabled(true);
    GameObject& platform = kinematicScene.CreateGameObject("Platform");
    RigidbodyComponent* kinematicBody =
        platform.AddComponent<RigidbodyComponent>();
    kinematicBody->SetBodyType(RigidbodyComponent::BodyType::Kinematic);
    kinematicBody->SetVelocity({ 2.0f, 0.0f, 0.0f });
    kinematicScene.Update(0.5f);
    assert(std::abs(
        platform.GetTransform().GetWorldPosition().x - 1.0f) < 0.0001f);

    // AABBと回転OBBのSAT判定も確認する。
    Scene obbScene("OBB Self Test");
    GameObject& aabbObject = obbScene.CreateGameObject("AABB");
    ColliderComponent* aabb = aabbObject.AddComponent<ColliderComponent>(
        nullptr, -1, ColliderComponent::ColliderType::Aabb);
    aabb->SetHalfExtents({ 1.0f, 1.0f, 1.0f });
    GameObject& obbObject = obbScene.CreateGameObject("OBB");
    obbObject.GetTransform().SetWorldPosition({ 1.5f, 0.0f, 0.0f });
    obbObject.GetTransform().SetLocalRotation({ 0.0f, 0.78539816f, 0.0f });
    ColliderComponent* obb = obbObject.AddComponent<ColliderComponent>(
        nullptr, -1, ColliderComponent::ColliderType::Obb);
    obb->SetHalfExtents({ 1.0f, 1.0f, 1.0f });
    ColliderComponent::Contact obbContact{};
    assert(ColliderComponent::ComputeContact(*aabb, *obb, obbContact));
    assert(obbContact.penetration > 0.0f);
    obbObject.GetTransform().SetWorldPosition({ 10.0f, 0.0f, 0.0f });
    assert(!ColliderComponent::ComputeContact(*aabb, *obb, obbContact));

    // 離れたCollider群ではSweep and PruneがN^2の狭域判定を発生させない。
    Scene broadPhaseScene("Broad Phase Self Test");
    broadPhaseScene.SetRuntimeUpdateEnabled(true);
    constexpr size_t kColliderCount = 64;
    for (size_t index = 0; index < kColliderCount; ++index) {
        GameObject& object = broadPhaseScene.CreateGameObject("Separated");
        object.GetTransform().SetWorldPosition({
            static_cast<float>(index) * 10.0f, 0.0f, 0.0f
        });
        object.AddComponent<ColliderComponent>(
            nullptr, -1, ColliderComponent::ColliderType::Sphere);
    }
    broadPhaseScene.Update(1.0f / 60.0f);
    assert(broadPhaseScene.GetLastColliderCount() == kColliderCount);
    assert(broadPhaseScene.GetLastNarrowPhaseTestCount() == 0);

    // RigidbodyとColliderの設定がメモリ上のScene保存・復元を往復できる。
    SceneSerializer serializer;
    serializer.Initialize(nullptr, nullptr, nullptr, -1, {});
    std::string serializedScene;
    std::string serializerMessage;
    assert(serializer.SerializeToString(
        scene, serializedScene, serializerMessage));
    assert(serializedScene.find("\"type\": \"Rigidbody\"") !=
        std::string::npos);
    Scene restoredScene("Restored Physics Self Test");
    assert(serializer.DeserializeFromString(
        restoredScene, serializedScene, serializerMessage));
    const GameObject* restoredBall = restoredScene.FindGameObject("Ball");
    assert(restoredBall != nullptr);
    assert(restoredBall->GetComponent<ColliderComponent>() != nullptr);
    const RigidbodyComponent* restoredBody =
        restoredBall->GetComponent<RigidbodyComponent>();
    assert(restoredBody != nullptr);
    assert(restoredBody->GetMass() == ballBody->GetMass());
    assert(restoredBody->UsesGravity() == ballBody->UsesGravity());

    OutputDebugStringA("Physics self-test succeeded.\n");
}

void RunVisualScript() {
    Scene scene("Visual Script Self Test");
    scene.SetRuntimeUpdateEnabled(true);
    GameObject& object = scene.CreateGameObject("Script Object");
    GameObject& target = scene.CreateGameObject("Target Object");
    auto* component = object.AddComponent<VisualScriptComponent>(nullptr);
    VisualScriptGraph& graph = component->GetGraph();
    graph.Clear();
    VisualScriptVariable& speed =
        graph.AddVariable(VisualScriptValueType::Float);
    speed.name = "Speed";
    speed.floatValue = 1.0f;

    const int startId = graph.AddNode(
        VisualScriptNodeType::EventStart, 20.0f, 20.0f).id;
    const int setSpeedId = graph.AddNode(
        VisualScriptNodeType::SetFloatVariable, 300.0f, 20.0f).id;
    graph.FindNode(setSpeedId)->stringValue = "Speed";
    const int fiveId = graph.AddNode(
        VisualScriptNodeType::FloatValue, 20.0f, 260.0f).id;
    graph.FindNode(fiveId)->floatValue = 5.0f;
    assert(graph.Connect(startId, 0, setSpeedId, 0));
    assert(graph.Connect(fiveId, 0, setSpeedId, 1));

    const int updateId = graph.AddNode(
        VisualScriptNodeType::EventUpdate, 20.0f, 520.0f).id;
    const int getSpeedId = graph.AddNode(
        VisualScriptNodeType::GetFloatVariable, 300.0f, 300.0f).id;
    graph.FindNode(getSpeedId)->stringValue = "Speed";
    const int greaterId = graph.AddNode(
        VisualScriptNodeType::GreaterFloat, 560.0f, 300.0f).id;
    graph.FindNode(greaterId)->floatValueB = 4.0f;
    const int branchId = graph.AddNode(
        VisualScriptNodeType::Branch, 300.0f, 520.0f).id;
    const int translateId = graph.AddNode(
        VisualScriptNodeType::Translate, 600.0f, 520.0f).id;
    graph.FindNode(translateId)->vectorValue = { 1.0f, 0.0f, 0.0f };
    graph.FindNode(translateId)->boolValue = false;
    assert(graph.Connect(updateId, 0, branchId, 0));
    assert(graph.Connect(getSpeedId, 0, greaterId, 0));
    assert(graph.Connect(greaterId, 0, branchId, 1));
    assert(graph.Connect(branchId, 0, translateId, 0));

    const int findId = graph.AddNode(
        VisualScriptNodeType::FindGameObject, 850.0f, 20.0f).id;
    graph.FindNode(findId)->stringValue = "Target Object";
    const int moveTargetId = graph.AddNode(
        VisualScriptNodeType::TranslateGameObject, 1120.0f, 20.0f).id;
    graph.FindNode(moveTargetId)->vectorValue = { 0.0f, 2.0f, 0.0f };
    graph.FindNode(moveTargetId)->boolValue = false;
    const int secondStartId = graph.AddNode(
        VisualScriptNodeType::EventStart, 850.0f, 300.0f).id;
    assert(graph.Connect(secondStartId, 0, moveTargetId, 0));
    assert(graph.Connect(findId, 0, moveTargetId, 1));

    std::string connectionError;
    assert(!graph.Connect(fiveId, 0, translateId, 0, &connectionError));
    assert(!connectionError.empty());

    const std::filesystem::path scriptPath =
        std::filesystem::temp_directory_path() /
        "CG2_VisualScriptSelfTest.vscript";
    component->SetScriptPath(scriptPath.generic_string());
    assert(component->SaveGraph());
    VisualScriptGraph loadedGraph;
    std::string graphError;
    assert(loadedGraph.Load(scriptPath.generic_string(), graphError));
    assert(loadedGraph.GetNodes().size() == graph.GetNodes().size());
    assert(loadedGraph.GetLinks().size() == graph.GetLinks().size());
    assert(loadedGraph.GetVariables().size() == 1);
    assert(loadedGraph.GetVariables()[0].name == "Speed");

    scene.Update(1.0f / 60.0f);
    const Vector3 position = object.GetTransform().GetWorldPosition();
    assert(std::abs(position.x - 1.0f) < 0.0001f);
    assert(std::abs(position.y) < 0.0001f);
    assert(std::abs(position.z) < 0.0001f);
    assert(std::abs(target.GetTransform().GetWorldPosition().y - 2.0f) <
        0.0001f);
    assert(component->GetLastError().empty());

    // 衝突イベントの「相手」GameObject出力をアクションへ渡せる。
    Scene collisionScene("Visual Script Collision Self Test");
    collisionScene.SetRuntimeUpdateEnabled(true);
    GameObject& trigger = collisionScene.CreateGameObject("Trigger");
    ColliderComponent* triggerCollider = trigger.AddComponent<ColliderComponent>(
        nullptr, -1, ColliderComponent::ColliderType::Sphere);
    triggerCollider->SetTrigger(true);
    auto* collisionScript =
        trigger.AddComponent<VisualScriptComponent>(nullptr);
    VisualScriptGraph& collisionGraph = collisionScript->GetGraph();
    collisionGraph.Clear();
    const int collisionEventId = collisionGraph.AddNode(
        VisualScriptNodeType::EventCollisionEnter, 20.0f, 20.0f).id;
    const int collisionMoveId = collisionGraph.AddNode(
        VisualScriptNodeType::TranslateGameObject, 320.0f, 20.0f).id;
    collisionGraph.FindNode(collisionMoveId)->vectorValue =
        { 0.0f, 3.0f, 0.0f };
    collisionGraph.FindNode(collisionMoveId)->boolValue = false;
    assert(collisionGraph.Connect(collisionEventId, 0, collisionMoveId, 0));
    assert(collisionGraph.Connect(collisionEventId, 1, collisionMoveId, 1));
    GameObject& collisionTarget =
        collisionScene.CreateGameObject("Collision Target");
    ColliderComponent* targetCollider =
        collisionTarget.AddComponent<ColliderComponent>(
            nullptr, -1, ColliderComponent::ColliderType::Sphere);
    targetCollider->SetTrigger(true);
    collisionScene.Update(1.0f / 60.0f);
    assert(std::abs(
        collisionTarget.GetTransform().GetWorldPosition().y - 3.0f) <
        0.0001f);
    assert(collisionScript->GetLastError().empty());

    SceneSerializer serializer;
    serializer.Initialize(nullptr, nullptr, nullptr, -1, {});
    std::string json;
    std::string message;
    assert(serializer.SerializeToString(scene, json, message));
    assert(json.find("\"type\": \"VisualScript\"") != std::string::npos);
    Scene restored("Restored Visual Script Self Test");
    assert(serializer.DeserializeFromString(restored, json, message));
    const GameObject* restoredObject = restored.FindGameObject("Script Object");
    assert(restoredObject != nullptr);
    const VisualScriptComponent* restoredComponent =
        restoredObject->GetComponent<VisualScriptComponent>();
    assert(restoredComponent != nullptr);
    assert(restoredComponent->GetScriptPath() == scriptPath.generic_string());

    std::error_code removeError;
    std::filesystem::remove(scriptPath, removeError);
    OutputDebugStringA("Visual Script self-test succeeded.\n");
}

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
