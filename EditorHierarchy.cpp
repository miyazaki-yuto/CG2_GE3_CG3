#include "Editor.h"

#include "CameraComponent.h"
#include "Component.h"
#include "GameObject.h"
#include "Graphics.h"
#include "InputManager.h"
#include "LightComponent.h"
#include "Material.h"
#include "Model.h"
#include "ModelRendererComponent.h"
#include "ParticleEmitterComponent.h"
#include "PrefabInstanceComponent.h"
#include "PrimitiveRendererComponent.h"
#include "RendererComponent.h"
#include "Scene.h"
#include "ShaderManager.h"
#include "SpriteRendererComponent.h"
#include "TransformComponent.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"

void Editor::DrawHierarchy(Scene& scene) {
    ImGui::Begin("ヒエラルキー###Hierarchy");
    ImGui::Text("シーン: %s", scene.GetName().c_str());
    DrawSceneFileControls(scene);
    ImGui::Separator();

    ImGui::BeginDisabled(!CanUndo());
    if (ImGui::Button("元に戻す###Undo")) {
        Undo(scene);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!CanRedo());
    if (ImGui::Button("やり直す###Redo")) {
        Redo(scene);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+Z / Ctrl+Y");
    ImGui::Separator();

    // よく使う操作は上部ボタンから、同じ操作は右クリックメニューからも実行できる。
    if (ImGui::Button("＋ 作成###CreateRoot")) {
        hierarchyAction_ = { HierarchyActionType::CreateRoot, 0, 0 };
    }
    ImGui::SameLine();
    const bool hasSelection =
        scene.FindGameObject(selectedGameObjectId_) != nullptr;
    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::Button("複製###Duplicate")) {
        hierarchyAction_ = {
            HierarchyActionType::Duplicate, selectedGameObjectId_, 0
        };
    }
    ImGui::SameLine();
    if (ImGui::Button("削除###Delete")) {
        hierarchyAction_ = {
            HierarchyActionType::Delete, selectedGameObjectId_, 0
        };
    }
    ImGui::EndDisabled();

    // Unityに近いF2・Ctrl+D・Deleteのショートカットも用意する。
    // InputText編集中にGameObjectを消さないよう、ItemがActiveな間は受け付けない。
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::IsAnyItemActive() && hasSelection) {
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
            if (GameObject* selected =
                scene.FindGameObject(selectedGameObjectId_)) {
                BeginHierarchyRename(*selected);
            }
        } else if (ImGui::GetIO().KeyCtrl &&
            ImGui::IsKeyPressed(ImGuiKey_D, false)) {
            hierarchyAction_ = {
                HierarchyActionType::Duplicate,
                selectedGameObjectId_,
                0
            };
        } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            hierarchyAction_ = {
                HierarchyActionType::Delete,
                selectedGameObjectId_,
                0
            };
        }
    }
    ImGui::Separator();

    for (const std::unique_ptr<GameObject>& gameObject :
        scene.GetGameObjects()) {
        if (!gameObject->IsPendingDestroy() &&
            gameObject->GetParent() == nullptr) {
            DrawHierarchyNode(scene, *gameObject);
        }
    }
    DrawHierarchyEmptyArea(scene);
    DrawHierarchyRenamePopup(scene);
    ImGui::End();

    // SceneのvectorとGameObjectのchildrenを走査し終えてから、安全に操作を反映する。
    ExecuteHierarchyAction(scene);
}

void Editor::DrawSceneFileControls(Scene& scene) {
    // Play中のScene保存は実行時変更をファイルへ残してしまうため無効にする。
    ImGui::BeginDisabled(isPlaying_);
    ImGui::InputText(
        "シーンファイル###SceneFile", scenePath_.data(), scenePath_.size());

    const bool controlPressed = ImGui::GetIO().KeyCtrl;
    const bool directInputControlPressed = inputManager_ != nullptr &&
        inputManager_->IsKeyPressed(VK_CONTROL);
    const bool saveButtonPressed = ImGui::Button("シーンを保存###SaveScene");
    const bool saveRequested = !isPlaying_ && (saveButtonPressed ||
        (controlPressed && ImGui::IsKeyPressed(ImGuiKey_S, false)) ||
        (directInputControlPressed &&
            inputManager_->IsKeyTriggered('S')));
    if (saveRequested) {
        sceneSerializer_.Save(scene, scenePath_.data(), sceneMessage_);
    }
    ImGui::SameLine();
    const bool loadButtonPressed = ImGui::Button("シーンを読み込む###LoadScene");
    const bool loadRequested = !isPlaying_ && (loadButtonPressed ||
        (controlPressed && ImGui::IsKeyPressed(ImGuiKey_O, false)) ||
        (directInputControlPressed &&
            inputManager_->IsKeyTriggered('O')));
    if (loadRequested) {
        if (sceneSerializer_.Load(
            scene, scenePath_.data(), sceneMessage_)) {
            // 別Sceneの履歴を現在Sceneへ適用しないよう、読み込み成功時に履歴を初期化する。
            ClearHistory();
            // 読み込んだSceneに同じIDがなければ、古い選択を解除する。
            if (scene.FindGameObject(selectedGameObjectId_) == nullptr) {
                selectedGameObjectId_ = 0;
            }
            lastMessage_.clear();
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+S / Ctrl+O");
    ImGui::EndDisabled();

    if (isPlaying_) {
        ImGui::TextDisabled("プレイモード中はシーンの保存・読み込みを使用できません。");
    }

    if (!sceneMessage_.empty()) {
        ImGui::TextWrapped("%s", sceneMessage_.c_str());
    }
}

void Editor::DrawHierarchyNode(Scene& scene, GameObject& gameObject) {
    if (gameObject.IsPendingDestroy()) {
        return;
    }

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_SpanAvailWidth;
    if (selectedGameObjectId_ == gameObject.GetId()) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (gameObject.GetChildren().empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf |
            ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    // 名前入力は別Popupへ分離し、この行はTreeNodeだけにする。
    // これにより選択・Popup・D&Dの全てが同じTreeNode Itemを安全に参照できる。
    ImGui::PushID(&gameObject);
    const bool opened = ImGui::TreeNodeEx(
        "HierarchyNode",
        flags,
        "%s",
        gameObject.GetName().c_str());
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        selectedGameObjectId_ = gameObject.GetId();
    }
    if (ImGui::IsItemHovered() &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        selectedGameObjectId_ = gameObject.GetId();
        BeginHierarchyRename(gameObject);
    }

    // 右クリックしたGameObjectを操作対象として選択する。
    if (ImGui::BeginPopupContextItem("HierarchyItemContext")) {
        selectedGameObjectId_ = gameObject.GetId();
        if (ImGui::MenuItem("空の子を作成###CreateEmptyChild")) {
            hierarchyAction_ = {
                HierarchyActionType::CreateChild, gameObject.GetId(), 0
            };
        }
        if (ImGui::MenuItem("名前を変更###Rename", "F2")) {
            BeginHierarchyRename(gameObject);
        }
        if (ImGui::MenuItem("複製###Duplicate", "Ctrl+D")) {
            hierarchyAction_ = {
                HierarchyActionType::Duplicate, gameObject.GetId(), 0
            };
        }
        if (ImGui::MenuItem(
            "親子関係を解除###Unparent", nullptr, false,
            gameObject.GetParent() != nullptr)) {
            hierarchyAction_ = {
                HierarchyActionType::Unparent, gameObject.GetId(), 0
            };
        }
        ImGui::Separator();
        if (ImGui::MenuItem("削除###Delete", "Delete")) {
            hierarchyAction_ = {
                HierarchyActionType::Delete, gameObject.GetId(), 0
            };
        }
        ImGui::EndPopup();
    }

    // ポインタではなくIDをPayloadへ入れる。Scene側で削除済みならFindが失敗するため安全。
    if (ImGui::BeginDragDropSource()) {
        const GameObject::Id draggedId = gameObject.GetId();
        ImGui::SetDragDropPayload(
            "CG2_GAMEOBJECT_ID", &draggedId, sizeof(draggedId));
        ImGui::Text("%s を移動", gameObject.GetName().c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload =
            ImGui::AcceptDragDropPayload("CG2_GAMEOBJECT_ID")) {
            if (payload->IsDelivery() &&
                payload->DataSize == sizeof(GameObject::Id)) {
                GameObject::Id draggedId = 0;
                std::memcpy(
                    &draggedId, payload->Data, sizeof(draggedId));
                hierarchyAction_ = {
                    HierarchyActionType::Reparent,
                    draggedId,
                    gameObject.GetId()
                };
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (opened && !gameObject.GetChildren().empty()) {
        for (GameObject* child : gameObject.GetChildren()) {
            if (child != nullptr && !child->IsPendingDestroy()) {
                DrawHierarchyNode(scene, *child);
            }
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void Editor::DrawHierarchyEmptyArea(Scene& /*scene*/) {
    ImGui::TextDisabled(
        "ここへドラッグするとGameObjectをシーン直下へ移動します。");

    // 残りの空白を1つの大きなD&Dターゲットにする。
    // 高さが残っていない場合も最低40pxを確保し、必ずドロップできるようにする。
    ImVec2 emptySize = ImGui::GetContentRegionAvail();
    emptySize.x = (std::max)(emptySize.x, 1.0f);
    emptySize.y = (std::max)(emptySize.y, 40.0f);
    ImGui::InvisibleButton("HierarchyRootDropArea", emptySize);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        selectedGameObjectId_ = 0;
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload =
            ImGui::AcceptDragDropPayload("CG2_GAMEOBJECT_ID")) {
            if (payload->IsDelivery() &&
                payload->DataSize == sizeof(GameObject::Id)) {
                GameObject::Id draggedId = 0;
                std::memcpy(
                    &draggedId, payload->Data, sizeof(draggedId));
                hierarchyAction_ = {
                    HierarchyActionType::Unparent, draggedId, 0
                };
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Hierarchyの空白を右クリックしたときのScene用メニュー。
    if (ImGui::BeginPopupContextItem("HierarchyEmptyContext")) {
        if (ImGui::MenuItem("空のGameObjectを作成###CreateEmpty")) {
            hierarchyAction_ = { HierarchyActionType::CreateRoot, 0, 0 };
        }
        ImGui::EndPopup();
    }
}

void Editor::DrawHierarchyRenamePopup(Scene& scene) {
    if (openHierarchyRenamePopup_) {
        ImGui::OpenPopup("GameObject名を変更###Rename GameObject");
        openHierarchyRenamePopup_ = false;
    }

    if (!ImGui::BeginPopupModal(
        "GameObject名を変更###Rename GameObject", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    GameObject* target = scene.FindGameObject(renamingGameObjectId_);
    if (target == nullptr) {
        renamingGameObjectId_ = 0;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::Text("GameObject: %s", target->GetName().c_str());
    if (focusHierarchyRename_) {
        ImGui::SetKeyboardFocusHere();
        focusHierarchyRename_ = false;
    }
    const bool enterPressed = ImGui::InputText(
        "新しい名前###NewName",
        hierarchyRenameBuffer_.data(),
        hierarchyRenameBuffer_.size(),
        ImGuiInputTextFlags_AutoSelectAll |
        ImGuiInputTextFlags_EnterReturnsTrue);

    const bool confirm = enterPressed || ImGui::Button("変更###ConfirmRename");
    ImGui::SameLine();
    const bool cancel = ImGui::Button("キャンセル###CancelRename") ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (confirm) {
        target->SetName(
            hierarchyRenameBuffer_[0] != '\0'
            ? hierarchyRenameBuffer_.data()
            : "GameObject");
        renamingGameObjectId_ = 0;
        lastMessage_ = "GameObject名を変更しました。";
        ImGui::CloseCurrentPopup();
    } else if (cancel) {
        renamingGameObjectId_ = 0;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void Editor::ExecuteHierarchyAction(Scene& scene) {
    // 先に予約を空へ戻す。処理中に失敗しても翌フレームへ同じ操作を持ち越さない。
    const HierarchyAction action = hierarchyAction_;
    hierarchyAction_ = {};

    if (action.type == HierarchyActionType::None) {
        return;
    }

    // 操作前のSceneを1回だけ記録し、成功した分岐だけafterと組にして履歴へ追加する。
    std::string beforeSnapshot;
    const uint64_t selectionBefore = selectedGameObjectId_;
    if (!CaptureSceneSnapshot(scene, beforeSnapshot)) {
        return;
    }

    switch (action.type) {
    case HierarchyActionType::None:
        return;

    case HierarchyActionType::CreateRoot: {
        GameObject& created = scene.CreateGameObject("GameObject");
        selectedGameObjectId_ = created.GetId();
        lastMessage_ = "GameObjectを作成しました。";
        CommitHistory(
            scene, "GameObjectを作成",
            std::move(beforeSnapshot), selectionBefore);
        return;
    }

    case HierarchyActionType::CreateChild: {
        GameObject* parent = scene.FindGameObject(action.sourceId);
        if (parent == nullptr) {
            lastMessage_ = "作成に失敗しました: 親が見つかりません。";
            return;
        }

        GameObject& created = scene.CreateGameObject("GameObject");
        if (!created.SetParent(parent)) {
            scene.DestroyGameObject(created);
            lastMessage_ = "作成に失敗しました: 親を設定できません。";
            return;
        }
        selectedGameObjectId_ = created.GetId();
        lastMessage_ = "子GameObjectを作成しました。";
        CommitHistory(
            scene, "子GameObjectを作成",
            std::move(beforeSnapshot), selectionBefore);
        return;
    }

    case HierarchyActionType::Duplicate: {
        const GameObject* source = scene.FindGameObject(action.sourceId);
        if (source == nullptr) {
            lastMessage_ = "複製に失敗しました: GameObjectが見つかりません。";
            return;
        }

        bool skippedComponent = false;
        GameObject* duplicate = DuplicateGameObjectHierarchy(
            scene,
            *source,
            source->GetParent(),
            true,
            skippedComponent);
        if (duplicate == nullptr) {
            lastMessage_ = "複製に失敗しました。";
            return;
        }

        selectedGameObjectId_ = duplicate->GetId();
        lastMessage_ = skippedComponent
            ? "複製しました。カメラまたは未対応コンポーネントは除外されました。"
            : "GameObjectを複製しました。";
        CommitHistory(
            scene, "GameObjectを複製",
            std::move(beforeSnapshot), selectionBefore);
        return;
    }

    case HierarchyActionType::Delete: {
        GameObject* target = scene.FindGameObject(action.sourceId);
        if (target == nullptr) {
            lastMessage_ = "削除に失敗しました: GameObjectが見つかりません。";
            return;
        }

        // 親を次の選択対象にしてから破棄予約する。子はSceneの既存仕様どおり再帰削除される。
        selectedGameObjectId_ = target->GetParent() != nullptr
            ? target->GetParent()->GetId()
            : 0;
        if (renamingGameObjectId_ == target->GetId()) {
            renamingGameObjectId_ = 0;
        }
        // ModelのGPUリソースを解放する前に、使用中のフレームを完了させる。
        if (graphics_ != nullptr) {
            graphics_->FlushGpu();
        }
        scene.DestroyGameObject(*target);
        lastMessage_ = "GameObjectと子を削除対象にしました。";
        CommitHistory(
            scene, "GameObjectを削除",
            std::move(beforeSnapshot), selectionBefore);
        return;
    }

    case HierarchyActionType::Reparent: {
        GameObject* source = scene.FindGameObject(action.sourceId);
        GameObject* newParent = scene.FindGameObject(action.targetId);
        if (source == nullptr || newParent == nullptr) {
            lastMessage_ = "親の変更に失敗しました: GameObjectが見つかりません。";
            return;
        }

        // trueにより、親変更前のWorld Transformをできるだけ維持する。
        if (!source->SetParent(newParent, true)) {
            lastMessage_ =
                "親を変更できません: 循環する親子関係は作成できません。";
            return;
        }
        selectedGameObjectId_ = source->GetId();
        lastMessage_ = "親を変更しました。";
        CommitHistory(
            scene, "親を変更",
            std::move(beforeSnapshot), selectionBefore);
        return;
    }

    case HierarchyActionType::Unparent: {
        GameObject* source = scene.FindGameObject(action.sourceId);
        if (source == nullptr) {
            lastMessage_ = "親子関係の解除に失敗しました: GameObjectが見つかりません。";
            return;
        }
        if (!source->SetParent(nullptr, true)) {
            lastMessage_ = "親子関係の解除に失敗しました。";
            return;
        }
        selectedGameObjectId_ = source->GetId();
        lastMessage_ = "GameObjectをシーン直下へ移動しました。";
        CommitHistory(
            scene, "GameObjectの親子関係を解除",
            std::move(beforeSnapshot), selectionBefore);
        return;
    }
    }
}

void Editor::BeginHierarchyRename(const GameObject& gameObject) {
    renamingGameObjectId_ = gameObject.GetId();
    focusHierarchyRename_ = true;
    openHierarchyRenamePopup_ = true;
    hierarchyRenameBuffer_.fill('\0');

    const std::string& name = gameObject.GetName();
    const size_t copyLength = (std::min)(
        name.size(), hierarchyRenameBuffer_.size() - 1);
    std::copy_n(
        name.data(), copyLength, hierarchyRenameBuffer_.data());
}

GameObject* Editor::DuplicateGameObjectHierarchy(
    Scene& scene,
    const GameObject& source,
    GameObject* duplicateParent,
    bool isDuplicateRoot,
    bool& skippedComponent) {
    if (source.IsPendingDestroy()) {
        return nullptr;
    }

    // 複製の最上位だけ "Copy" 名にし、配下の名前と階層構造は元のまま保つ。
    const std::string duplicateName = isDuplicateRoot
        ? MakeDuplicateName(scene, source.GetName())
        : source.GetName();
    GameObject& duplicate = scene.CreateGameObject(duplicateName);
    duplicate.SetActive(source.IsActive());
    if (duplicateParent != nullptr) {
        duplicate.SetParent(duplicateParent);
    }
    duplicate.GetTransform().GetLocalTransform() =
        source.GetTransform().GetLocalTransform();

    CopyComponentsForDuplicate(source, duplicate, skippedComponent);

    // 子も再帰複製するため、選択したまとまりをそのまま複製できる。
    for (GameObject* child : source.GetChildren()) {
        if (child == nullptr || child->IsPendingDestroy()) {
            continue;
        }
        DuplicateGameObjectHierarchy(
            scene,
            *child,
            &duplicate,
            false,
            skippedComponent);
    }
    return &duplicate;
}

void Editor::CopyComponentsForDuplicate(
    const GameObject& source,
    GameObject& destination,
    bool& skippedComponent) {
    // Component共通の有効状態とRenderer共通設定を最後に揃える小さな補助処理。
    const auto copyCommonState = [](
        const Component& sourceComponent,
        Component& destinationComponent) {
        if (const auto* sourceRenderer =
            dynamic_cast<const RendererComponent*>(&sourceComponent)) {
            if (auto* destinationRenderer =
                dynamic_cast<RendererComponent*>(&destinationComponent)) {
                destinationRenderer->SetRenderOrder(
                    sourceRenderer->GetRenderOrder());
                destinationRenderer->SetBlendMode(
                    sourceRenderer->GetBlendMode());
            }
        }
        destinationComponent.SetEnabled(sourceComponent.IsEnabled());
    };

    for (const std::unique_ptr<Component>& component :
        source.GetComponents()) {
        if (dynamic_cast<const TransformComponent*>(component.get()) != nullptr) {
            continue;
        }

        if (const auto* sourceRenderer =
            dynamic_cast<const ModelRendererComponent*>(component.get())) {
            if (sourceRenderer->GetSharedModel() == nullptr) {
                skippedComponent = true;
                continue;
            }

            // GUIDが同じModelはAssetManagerのGPUリソースを共有する。
            auto* duplicate =
                destination.AddComponent<ModelRendererComponent>(
                    sourceRenderer->GetSharedModel(),
                    sourceRenderer->GetFallbackTextureHandle(),
                    sourceRenderer->GetModelGuid(),
                    sourceRenderer->GetFallbackTextureGuid());
            duplicate->SetColor(sourceRenderer->GetColor());
            duplicate->SetUVTransform(sourceRenderer->GetUVTransform());
            duplicate->SetLightingEnabled(
                sourceRenderer->IsLightingEnabled());
            if (duplicate->PlayAnimation(
                    sourceRenderer->GetCurrentAnimationIndex(), true)) {
                duplicate->SetAnimationTime(
                    sourceRenderer->GetAnimationTime());
                duplicate->SetAnimationLooping(
                    sourceRenderer->IsAnimationLooping());
                duplicate->SetAnimationPlaybackSpeed(
                    sourceRenderer->GetAnimationPlaybackSpeed());
                if (!sourceRenderer->IsAnimationPlaying()) {
                    duplicate->PauseAnimation();
                }
            }

            // Model本体は共有しても、Inspectorで変更したMaterial設定はGameObject固有。
            // 複製後に片方だけ編集できるよう、TextureとUVの上書き値をSlotごとにコピーする。
            for (uint32_t materialIndex = 0;
                materialIndex < sourceRenderer->GetSharedModel()->GetMaterialCount();
                ++materialIndex) {
                const int textureHandle =
                    sourceRenderer->GetMaterialTextureHandle(materialIndex);
                if (textureHandle >= 0) {
                    duplicate->SetMaterialTextureAsset(
                        materialIndex,
                        textureHandle,
                        sourceRenderer->GetMaterialTextureGuid(materialIndex));
                }
                const int normalTextureHandle =
                    sourceRenderer->GetMaterialNormalTextureHandle(
                        materialIndex);
                if (normalTextureHandle >= 0) {
                    duplicate->SetMaterialNormalTextureAsset(
                        materialIndex,
                        normalTextureHandle,
                        sourceRenderer->GetMaterialNormalTextureGuid(
                            materialIndex));
                }
                if (sourceRenderer->IsMaterialPbrOverridden(materialIndex)) {
                    duplicate->SetMaterialPbrOverridden(materialIndex, true);
                    duplicate->SetMaterialMetallic(
                        materialIndex,
                        sourceRenderer->GetMaterialMetallic(materialIndex));
                    duplicate->SetMaterialRoughness(
                        materialIndex,
                        sourceRenderer->GetMaterialRoughness(materialIndex));
                }
                if (sourceRenderer->IsMaterialUVTransformOverridden(
                    materialIndex)) {
                    duplicate->SetMaterialUVTransformOverridden(
                        materialIndex, true);
                    duplicate->SetMaterialUVTransform(
                        materialIndex,
                        sourceRenderer->GetMaterialUVTransform(materialIndex));
                }
                const std::shared_ptr<Material> sourceShaderMaterial =
                    sourceRenderer->GetShaderMaterial(materialIndex);
                if (sourceShaderMaterial != nullptr && graphics_ != nullptr) {
                    std::shared_ptr<Material> materialCopy =
                        graphics_->CreateMaterial(
                            sourceShaderMaterial->GetShaderName());
                    if (materialCopy != nullptr) {
                        for (const ShaderParameterDefinition& definition :
                            sourceShaderMaterial->
                                GetParameterDefinitions()) {
                            const ShaderParameterValue* value =
                                sourceShaderMaterial->GetParameter(
                                    definition.name);
                            if (value == nullptr) {
                                continue;
                            }
                            switch (definition.type) {
                            case ShaderParameterType::Float:
                                materialCopy->SetFloat(
                                    definition.name, value->floats[0]);
                                break;
                            case ShaderParameterType::Float2:
                                materialCopy->SetFloat2(
                                    definition.name,
                                    value->floats[0],
                                    value->floats[1]);
                                break;
                            case ShaderParameterType::Float3:
                                materialCopy->SetFloat3(
                                    definition.name,
                                    value->floats[0],
                                    value->floats[1],
                                    value->floats[2]);
                                break;
                            case ShaderParameterType::Float4:
                                materialCopy->SetFloat4(
                                    definition.name,
                                    value->floats[0],
                                    value->floats[1],
                                    value->floats[2],
                                    value->floats[3]);
                                break;
                            case ShaderParameterType::Int:
                                materialCopy->SetInt(
                                    definition.name, value->integer);
                                break;
                            case ShaderParameterType::Bool:
                                materialCopy->SetBool(
                                    definition.name,
                                    value->integer != 0);
                                break;
                            }
                        }
                        duplicate->SetShaderMaterial(
                            materialIndex, std::move(materialCopy));
                    }
                }
            }
            copyCommonState(*sourceRenderer, *duplicate);
            continue;
        }

        if (const auto* sourceRenderer =
            dynamic_cast<const SpriteRendererComponent*>(component.get())) {
            auto* duplicate =
                destination.AddComponent<SpriteRendererComponent>(
                    sourceRenderer->GetSprite(),
                    sourceRenderer->GetTextureHandle(),
                    sourceRenderer->GetTextureGuid());
            duplicate->SetColor(sourceRenderer->GetColor());
            duplicate->SetUVTransform(sourceRenderer->GetUVTransform());
            copyCommonState(*sourceRenderer, *duplicate);
            continue;
        }

        if (const auto* sourceRenderer =
            dynamic_cast<const PrimitiveRendererComponent*>(component.get())) {
            if (graphics_ == nullptr) {
                skippedComponent = true;
                continue;
            }
            auto* duplicate =
                destination.AddComponent<PrimitiveRendererComponent>(
                    graphics_->GetPrimitiveDrawer(),
                    sourceRenderer->GetPrimitiveType(),
                    sourceRenderer->GetTextureHandle(),
                    sourceRenderer->GetTextureGuid());
            duplicate->SetTriangleVertices(
                sourceRenderer->GetTriangleVertices());
            duplicate->SetColor(sourceRenderer->GetColor());
            duplicate->SetUVTransform(sourceRenderer->GetUVTransform());
            copyCommonState(*sourceRenderer, *duplicate);
            continue;
        }

        if (const auto* sourceEmitter =
            dynamic_cast<const ParticleEmitterComponent*>(component.get())) {
            if (graphics_ == nullptr) {
                skippedComponent = true;
                continue;
            }
            auto* duplicate =
                destination.AddComponent<ParticleEmitterComponent>(
                    graphics_->GetParticleDrawer(),
                    sourceEmitter->GetTextureHandle(),
                    sourceEmitter->GetTextureGuid());
            duplicate->SetMaxParticles(sourceEmitter->GetMaxParticles());
            duplicate->SetEmissionRate(sourceEmitter->GetEmissionRate());
            duplicate->SetBurstCount(sourceEmitter->GetBurstCount());
            duplicate->SetPlayOnAwake(sourceEmitter->IsPlayOnAwake());
            duplicate->SetLifetimeRange(
                sourceEmitter->GetLifetimeMin(),
                sourceEmitter->GetLifetimeMax());
            duplicate->SetSpeedRange(
                sourceEmitter->GetSpeedMin(),
                sourceEmitter->GetSpeedMax());
            duplicate->SetDirection(sourceEmitter->GetDirection());
            duplicate->SetSpread(sourceEmitter->GetSpread());
            duplicate->SetGravity(sourceEmitter->GetGravity());
            duplicate->SetSizeRange(
                sourceEmitter->GetStartSize(),
                sourceEmitter->GetEndSize());
            duplicate->SetStartColor(sourceEmitter->GetStartColor());
            duplicate->SetEndColor(sourceEmitter->GetEndColor());
            copyCommonState(*sourceEmitter, *duplicate);
            continue;
        }

        if (const auto* sourceLight =
            dynamic_cast<const LightComponent*>(component.get())) {
            if (graphics_ == nullptr) {
                skippedComponent = true;
                continue;
            }
            auto* duplicate = destination.AddComponent<LightComponent>(
                graphics_->GetLightingManager(),
                sourceLight->GetLightType());
            duplicate->SetLightEnabled(sourceLight->IsLightEnabled());
            duplicate->SetColor(sourceLight->GetColor());
            duplicate->SetIntensity(sourceLight->GetIntensity());
            duplicate->SetRadius(sourceLight->GetRadius());
            duplicate->SetDecay(sourceLight->GetDecay());
            duplicate->SetOuterAngle(sourceLight->GetOuterAngle());
            duplicate->SetInnerAngle(sourceLight->GetInnerAngle());
            copyCommonState(*sourceLight, *duplicate);
            duplicate->ApplyLight();
            continue;
        }

        if (const auto* sourcePrefab =
            dynamic_cast<const PrefabInstanceComponent*>(component.get())) {
            auto* duplicate =
                destination.AddComponent<PrefabInstanceComponent>(
                    sourcePrefab->GetPrefabGuid(),
                    sourcePrefab->IsAutoUpdateEnabled());
            copyCommonState(*sourcePrefab, *duplicate);
            continue;
        }

        // DebugCameraはGraphicsが1個だけ共有し、SceneSerializerもCameraを最大1個に制限する。
        // Camera付きGameObject自体は複製するが、CameraComponentは重複させない。
        if (dynamic_cast<const CameraComponent*>(component.get()) != nullptr) {
            skippedComponent = true;
            continue;
        }

        // 将来追加されたComponentにClone処理がまだ無い場合も、破損した複製は作らない。
        skippedComponent = true;
    }
}

std::string Editor::MakeDuplicateName(
    const Scene& scene,
    const std::string& sourceName) const {
    const std::string baseName = sourceName + " Copy";
    if (scene.FindGameObject(baseName) == nullptr) {
        return baseName;
    }

    for (uint32_t suffix = 2; ; ++suffix) {
        const std::string candidate =
            baseName + " (" + std::to_string(suffix) + ")";
        if (scene.FindGameObject(candidate) == nullptr) {
            return candidate;
        }
    }
}

#endif
