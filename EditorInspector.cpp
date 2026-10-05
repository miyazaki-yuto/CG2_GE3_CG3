#include "Editor.h"
#include "EditorInspectorUtilities.h"

#include "AssetManager.h"
#include "CameraComponent.h"
#include "Component.h"
#include "GameObject.h"
#include "Graphics.h"
#include "LightComponent.h"
#include "Material.h"
#include "Model.h"
#include "ModelRendererComponent.h"
#include "ParticleEmitterComponent.h"
#include "PrefabInstanceComponent.h"
#include "PrefabManager.h"
#include "PrimitiveRendererComponent.h"
#include "RendererComponent.h"
#include "Scene.h"
#include "ShaderManager.h"
#include "SpriteRendererComponent.h"
#include "TransformComponent.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <utility>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"

namespace {

const char* GetComponentName(const Component& component) {
    if (dynamic_cast<const TransformComponent*>(&component) != nullptr) {
        return "トランスフォーム###Transform";
    }
    if (dynamic_cast<const ModelRendererComponent*>(&component) != nullptr) {
        return "モデルレンダラー###Model Renderer";
    }
    if (dynamic_cast<const SpriteRendererComponent*>(&component) != nullptr) {
        return "スプライトレンダラー###Sprite Renderer";
    }
    if (dynamic_cast<const PrimitiveRendererComponent*>(&component) != nullptr) {
        return "プリミティブレンダラー###Primitive Renderer";
    }
    if (dynamic_cast<const ParticleEmitterComponent*>(&component) != nullptr) {
        return "パーティクルエミッター###Particle Emitter";
    }
    if (dynamic_cast<const PrefabInstanceComponent*>(&component) != nullptr) {
        return "Prefabインスタンス###Prefab Instance";
    }
    if (dynamic_cast<const CameraComponent*>(&component) != nullptr) {
        return "カメラ###Camera";
    }
    if (dynamic_cast<const LightComponent*>(&component) != nullptr) {
        return "ライト###Light";
    }
    return "コンポーネント###Component";
}

} // namespace

using namespace EditorInspectorUtilities;

void Editor::DrawInspector(Scene& scene) {
    ImGui::Begin("インスペクター###Inspector");
    GameObject* selected = scene.FindGameObject(selectedGameObjectId_);
    if (selected == nullptr) {
        ImGui::TextUnformatted("ヒエラルキーでGameObjectを選択してください。");
        ImGui::End();
        return;
    }

    std::array<char, 128> nameBuffer{};
    const std::string& currentName = selected->GetName();
    const size_t copyLength = (std::min)(
        currentName.size(), nameBuffer.size() - 1);
    std::copy_n(currentName.data(), copyLength, nameBuffer.data());
    if (ImGui::InputText("名前###Name", nameBuffer.data(), nameBuffer.size())) {
        selected->SetName(nameBuffer.data());
    }
    ImGui::Text("ID: %llu", static_cast<unsigned long long>(selected->GetId()));
    ImGui::Text(
        "親: %s",
        selected->GetParent() != nullptr
        ? selected->GetParent()->GetName().c_str()
         : "なし");
    if (assetManager_ != nullptr && ImGui::Button("アセットを更新###RefreshAssets")) {
        assetManager_->RefreshAssets();
        lastMessage_ = "アセットデータベースを更新しました。登録数: " +
            std::to_string(assetManager_->GetAssetCount());
    }

    if (DrawPrefabInspector(scene, *selected)) {
        // Apply/RevertはGameObjectを同じIDの新しい実体へ差し替える。
        // このフレームでは古いselectedポインタを以降使用しない。
        ImGui::End();
        return;
    }

    DrawTransformInspector(scene, *selected);

    Component* removeTarget = nullptr;
    for (const std::unique_ptr<Component>& component :
        selected->GetComponents()) {
        if (component.get() == &selected->GetTransform() ||
            dynamic_cast<PrefabInstanceComponent*>(component.get()) != nullptr) {
            continue;
        }

        ImGui::PushID(component.get());
        const bool opened = ImGui::CollapsingHeader(
            GetComponentName(*component),
            ImGuiTreeNodeFlags_DefaultOpen);
        if (opened) {
            bool enabled = component->IsEnabled();
            if (ImGui::Checkbox("有効###Enabled", &enabled)) {
                component->SetEnabled(enabled);
            }
            DrawComponentInspector(*component);
            if (ImGui::Button("コンポーネントを削除###RemoveComponent")) {
                removeTarget = component.get();
            }
        }
        ImGui::PopID();
    }

    // vectorを走査し終わってから削除し、Iteratorの無効化を防ぐ。
    if (removeTarget != nullptr) {
        std::string beforeSnapshot;
        const uint64_t selectionBefore = selectedGameObjectId_;
        const std::string componentName = GetComponentName(*removeTarget);
        if (CaptureSceneSnapshot(scene, beforeSnapshot)) {
            // ModelなどのGPUリソースをComponentが所有している可能性がある。
            if (graphics_ != nullptr) {
                graphics_->FlushGpu();
            }
            if (selected->RemoveComponent(removeTarget)) {
                lastMessage_ = "コンポーネントを削除しました。";
                CommitHistory(
                    scene,
                    componentName + "を削除",
                    std::move(beforeSnapshot),
                    selectionBefore);
            }
        }
    }

    DrawAddComponent(*selected, scene);
    if (!lastMessage_.empty()) {
        ImGui::TextWrapped("%s", lastMessage_.c_str());
    }
    ImGui::End();
}

bool Editor::DrawPrefabInspector(
    Scene& scene,
    GameObject& gameObject) {
    ImGui::SeparatorText("Prefab");
    PrefabInstanceComponent* prefab =
        gameObject.GetComponent<PrefabInstanceComponent>();

    if (prefab != nullptr) {
        const std::string prefabPath = assetManager_ != nullptr
            ? assetManager_->GetAssetPath(prefab->GetPrefabGuid())
            : std::string{};
        ImGui::TextWrapped("GUID: %s", prefab->GetPrefabGuid().c_str());
        ImGui::TextWrapped("ファイル: %s", prefabPath.c_str());
        bool autoUpdate = prefab->IsAutoUpdateEnabled();
        if (ImGui::Checkbox("自動更新###AutoUpdate", &autoUpdate)) {
            prefab->SetAutoUpdateEnabled(autoUpdate);
        }

        ImGui::BeginDisabled(isPlaying_);
        const bool applyToPrefab = ImGui::Button("Prefabへ適用###ApplyToPrefab");
        ImGui::EndDisabled();
        if (applyToPrefab) {
            std::string beforeSnapshot;
            const uint64_t selectionBefore = selectedGameObjectId_;
            if (prefabManager_ != nullptr &&
                CaptureSceneSnapshot(scene, beforeSnapshot) &&
                prefabManager_->ApplyPrefab(scene, gameObject, lastMessage_)) {
                CommitHistory(
                    scene,
                    "Prefabへ適用",
                    std::move(beforeSnapshot),
                    selectionBefore);
                return true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Prefabから復元###RevertFromPrefab")) {
            std::string beforeSnapshot;
            const uint64_t selectionBefore = selectedGameObjectId_;
            if (prefabManager_ != nullptr &&
                CaptureSceneSnapshot(scene, beforeSnapshot) &&
                prefabManager_->RefreshInstance(
                    scene, gameObject, lastMessage_) != nullptr) {
                CommitHistory(
                    scene,
                    "Prefabから復元",
                    std::move(beforeSnapshot),
                    selectionBefore);
                return true;
            }
        }
    } else {
        ImGui::TextDisabled("Prefabインスタンスではありません。");
    }

    ImGui::BeginDisabled(isPlaying_);
    const bool saveAsPrefab = ImGui::Button("Prefabとして保存...###SaveAsPrefab");
    ImGui::EndDisabled();
    if (saveAsPrefab) {
        const std::string filePath = OpenPrefabSaveDialog();
        if (!filePath.empty() && prefabManager_ != nullptr) {
            std::string beforeSnapshot;
            const uint64_t selectionBefore = selectedGameObjectId_;
            if (CaptureSceneSnapshot(scene, beforeSnapshot)) {
                const std::string guid = prefabManager_->SavePrefab(
                    gameObject, filePath, lastMessage_);
                if (!guid.empty()) {
                    CommitHistory(
                        scene,
                        "Prefabを作成",
                        std::move(beforeSnapshot),
                        selectionBefore);
                }
            }
        }
    }

    ImGui::SameLine();
    const bool instantiateAsChild =
        ImGui::Button("子として生成...###InstantiateAsChild");
    ImGui::SameLine();
    const bool instantiateAsRoot =
        ImGui::Button("ルートに生成...###InstantiateAsRoot");
    if (instantiateAsChild || instantiateAsRoot) {
        const std::string filePath = OpenAssetFileDialog(AssetType::Prefab);
        if (!filePath.empty() && assetManager_ != nullptr &&
            prefabManager_ != nullptr) {
            std::string importError;
            const std::string guid = assetManager_->ImportPrefab(
                filePath, &importError);
            if (guid.empty()) {
                lastMessage_ = importError;
            } else {
                std::string beforeSnapshot;
                const uint64_t selectionBefore = selectedGameObjectId_;
                if (CaptureSceneSnapshot(scene, beforeSnapshot)) {
                    GameObject* instance = prefabManager_->Instantiate(
                        scene,
                        guid,
                        instantiateAsChild ? &gameObject : nullptr,
                        lastMessage_);
                    if (instance != nullptr) {
                        selectedGameObjectId_ = instance->GetId();
                        CommitHistory(
                            scene,
                            "Prefabを生成",
                            std::move(beforeSnapshot),
                            selectionBefore);
                    }
                }
            }
        }
    }
    if (isPlaying_) {
        ImGui::TextDisabled(
            "プレイモード中はPrefabの適用・保存を使用できません。");
    }
    return false;
}

void Editor::DrawTransformInspector(
    Scene& scene,
    GameObject& gameObject) {
    if (!ImGui::CollapsingHeader(
        "トランスフォーム###Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    TransformData& transform =
        gameObject.GetTransform().GetLocalTransform();
    const bool positionChanged = ImGui::DragFloat3(
        "ローカル位置###LocalPosition", &transform.translate.x, 0.05f);
    if (positionChanged) {
        if (CameraComponent* camera =
            gameObject.GetComponent<CameraComponent>()) {
            camera->CaptureOwnerTransform();
        }
    }
    HandleTransformHistoryItem(scene, gameObject);
    const bool rotationChanged = ImGui::DragFloat3(
        "ローカル回転###LocalRotation", &transform.rotate.x,
        0.01f, -3.141592f, 3.141592f);
    if (rotationChanged) {
        if (CameraComponent* camera =
            gameObject.GetComponent<CameraComponent>()) {
            camera->CaptureOwnerTransform();
        }
    }
    HandleTransformHistoryItem(scene, gameObject);
    ImGui::DragFloat3(
        "ローカルスケール###LocalScale", &transform.scale.x, 0.01f, 0.001f, 1000.0f);
    HandleTransformHistoryItem(scene, gameObject);

    const Vector3 worldPosition =
        gameObject.GetTransform().GetWorldPosition();
    ImGui::Text(
        "ワールド位置: %.3f, %.3f, %.3f",
        worldPosition.x, worldPosition.y, worldPosition.z);
}

void Editor::HandleTransformHistoryItem(
    Scene& scene,
    GameObject& gameObject) {
    if (ImGui::IsItemActivated()) {
        std::string snapshot;
        if (CaptureSceneSnapshot(scene, snapshot)) {
            transformBeforeSnapshot_ = std::move(snapshot);
            transformEditingGameObjectId_ = gameObject.GetId();
            transformSelectionBefore_ = selectedGameObjectId_;
        }
    }

    if (transformEditingGameObjectId_ != gameObject.GetId()) {
        return;
    }

    if (ImGui::IsItemDeactivatedAfterEdit()) {
        lastMessage_ = "トランスフォームを変更しました。";
        CommitHistory(
            scene,
            "トランスフォームを変更",
            std::move(transformBeforeSnapshot_),
            transformSelectionBefore_);
        transformBeforeSnapshot_.clear();
        transformEditingGameObjectId_ = 0;
    } else if (ImGui::IsItemDeactivated()) {
        // クリックしただけで値が変わらなかった場合は履歴を作らない。
        transformBeforeSnapshot_.clear();
        transformEditingGameObjectId_ = 0;
    }
}

void Editor::DrawComponentInspector(Component& component) {
    if (auto* renderer = dynamic_cast<RendererComponent*>(&component)) {
        DrawRendererInspector(*renderer);
    }
    if (auto* modelRenderer = dynamic_cast<ModelRendererComponent*>(&component)) {
        DrawModelRendererInspector(*modelRenderer);
        return;
    }
    if (auto* spriteRenderer = dynamic_cast<SpriteRendererComponent*>(&component)) {
        DrawSpriteRendererInspector(*spriteRenderer);
        return;
    }
    if (auto* primitiveRenderer = dynamic_cast<PrimitiveRendererComponent*>(&component)) {
        DrawPrimitiveRendererInspector(*primitiveRenderer);
        return;
    }
    if (auto* emitter = dynamic_cast<ParticleEmitterComponent*>(&component)) {
        DrawParticleEmitterInspector(*emitter);
        return;
    }
    if (auto* prefab = dynamic_cast<PrefabInstanceComponent*>(&component)) {
        DrawPrefabInstanceInspector(*prefab);
        return;
    }
    if (auto* light = dynamic_cast<LightComponent*>(&component)) {
        DrawLightInspector(*light);
        return;
    }
    if (auto* camera = dynamic_cast<CameraComponent*>(&component)) {
        DrawCameraInspector(*camera);
    }
}

void Editor::DrawAddComponent(GameObject& gameObject, Scene& scene) {
    ImGui::SeparatorText("コンポーネントを追加");
    const char* componentTypes[] = {
        "モデルレンダラー",
        "スプライトレンダラー",
        "三角形レンダラー",
        "球レンダラー",
        "平行光源",
        "点光源",
        "カメラ",
        "パーティクルエミッター",
        "スポットライト"
    };
    ImGui::Combo(
        "コンポーネント種類###ComponentType", &addComponentType_, componentTypes,
        static_cast<int>(std::size(componentTypes)));

    if (addComponentType_ == 0) {
        const std::string modelPath = assetManager_ != nullptr
            ? assetManager_->GetAssetPath(addModelGuid_)
            : std::string{};
        ImGui::TextWrapped("モデル: %s", modelPath.c_str());
        ImGui::TextWrapped("GUID: %s", addModelGuid_.c_str());
        if (ImGui::Button("モデルを選択...###SelectModel")) {
            const std::string path = OpenAssetFileDialog(AssetType::Model);
            if (!path.empty() && assetManager_ != nullptr) {
                std::string error;
                const AssetGuid guid = assetManager_->ImportModel(path, &error);
                if (!guid.empty()) {
                    addModelGuid_ = guid;
                    lastMessage_ = "モデルアセットを選択しました。";
                } else {
                    lastMessage_ = error;
                }
            }
        }
    }

    if (!ImGui::Button("コンポーネントを追加###AddComponent")) {
        return;
    }

    lastMessage_.clear();
    if (graphics_ == nullptr || assetManager_ == nullptr) {
        lastMessage_ = "GraphicsまたはAssetManagerを利用できません。";
        return;
    }

    std::string beforeSnapshot;
    const uint64_t selectionBefore = selectedGameObjectId_;
    if (!CaptureSceneSnapshot(scene, beforeSnapshot)) {
        return;
    }

    switch (addComponentType_) {
    case 0: {
        std::string error;
        std::shared_ptr<Model> model = assetManager_->LoadModel(
            addModelGuid_, false, &error);
        if (model == nullptr) {
            lastMessage_ = error;
            return;
        }
        gameObject.AddComponent<ModelRendererComponent>(
            std::move(model),
            defaultTextureHandle_,
            addModelGuid_,
            defaultTextureGuid_);
        break;
    }
    case 1:
        gameObject.AddComponent<SpriteRendererComponent>(
            graphics_->GetSprite(),
            defaultTextureHandle_,
            defaultTextureGuid_);
        break;
    case 2:
        gameObject.AddComponent<PrimitiveRendererComponent>(
            graphics_->GetPrimitiveDrawer(),
            PrimitiveRendererComponent::PrimitiveType::Triangle,
            defaultTextureHandle_,
            defaultTextureGuid_);
        break;
    case 3:
        gameObject.AddComponent<PrimitiveRendererComponent>(
            graphics_->GetPrimitiveDrawer(),
            PrimitiveRendererComponent::PrimitiveType::Sphere,
            defaultTextureHandle_,
            defaultTextureGuid_);
        break;
    case 4:
        gameObject.AddComponent<LightComponent>(
            graphics_->GetLightingManager(),
            LightComponent::LightType::Directional);
        break;
    case 5:
        gameObject.AddComponent<LightComponent>(
            graphics_->GetLightingManager(),
            LightComponent::LightType::Point);
        break;
    case 6: {
        bool cameraExists = false;
        for (const std::unique_ptr<GameObject>& object :
            scene.GetGameObjects()) {
            if (object->GetComponent<CameraComponent>() != nullptr) {
                cameraExists = true;
                break;
            }
        }
        if (cameraExists) {
            lastMessage_ = "CameraComponentは現在1つだけ使用できます。";
            return;
        }
        gameObject.AddComponent<CameraComponent>(
            graphics_->GetDebugCamera(),
            inputManager_,
            graphics_->GetLightingManager());
        break;
    }
    case 7:
        gameObject.AddComponent<ParticleEmitterComponent>(
            graphics_->GetParticleDrawer(),
            defaultTextureHandle_,
            defaultTextureGuid_);
        break;
    case 8:
        gameObject.AddComponent<LightComponent>(
            graphics_->GetLightingManager(),
            LightComponent::LightType::Spot);
        break;
    default:
        return;
    }

    lastMessage_ = "コンポーネントを追加しました。";
    CommitHistory(
        scene,
        std::string(componentTypes[addComponentType_]) + "を追加",
        std::move(beforeSnapshot),
        selectionBefore);
}
#endif
