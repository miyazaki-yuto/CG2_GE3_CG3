#include "Scene.h"

#include "RendererComponent.h"

#include <algorithm>
#include <utility>

Scene::Scene(std::string name)
    : name_(std::move(name)) {
}

Scene::~Scene() {
    Clear();
}

GameObject& Scene::CreateGameObject(const std::string& name) {
    auto gameObject = std::make_unique<GameObject>(
        this, nextGameObjectId_++, name);
    GameObject& result = *gameObject;
    gameObjects_.push_back(std::move(gameObject));
    return result;
}

GameObject* Scene::CreateGameObjectWithId(
    GameObject::Id id,
    const std::string& name) {
    if (id == 0 || FindGameObject(id) != nullptr) {
        return nullptr;
    }

    auto gameObject = std::make_unique<GameObject>(this, id, name);
    GameObject* result = gameObject.get();
    gameObjects_.push_back(std::move(gameObject));
    if (id >= nextGameObjectId_) {
        nextGameObjectId_ = id + 1;
    }
    return result;
}

void Scene::DestroyGameObject(GameObject& gameObject) {
    if (gameObject.GetScene() != this) {
        return;
    }
    // その場でvectorから消すとComponent更新ループが壊れるため、フレーム末尾まで待つ
    gameObject.MarkForDestroy();
}

void Scene::DestroyGameObject(GameObject::Id id) {
    if (GameObject* gameObject = FindGameObject(id)) {
        DestroyGameObject(*gameObject);
    }
}

void Scene::DestroyGameObjectImmediate(GameObject& gameObject) {
    if (gameObject.GetScene() != this) {
        return;
    }
    gameObject.MarkForDestroy();
    RemoveDestroyedGameObjects();
}

GameObject* Scene::AppendHierarchy(
    Scene&& sourceScene,
    GameObject::Id sourceRootId,
    GameObject* parent,
    GameObject::Id forcedRootId) {
    GameObject* sourceRoot = sourceScene.FindGameObject(sourceRootId);
    if (sourceRoot == nullptr || sourceRoot->GetParent() != nullptr ||
        (parent != nullptr && parent->GetScene() != this) ||
        (forcedRootId != 0 && FindGameObject(forcedRootId) != nullptr)) {
        return nullptr;
    }

    // unique_ptrを移動してもGameObject本体のアドレスは変化しない。
    // そのため、階層内のparent/childrenポインタはそのまま利用できる
    for (const std::unique_ptr<GameObject>& gameObject :
        sourceScene.gameObjects_) {
        if (gameObject.get() == sourceRoot && forcedRootId != 0) {
            gameObject->id_ = forcedRootId;
        } else {
            if (nextGameObjectId_ == forcedRootId) {
                ++nextGameObjectId_;
            }
            gameObject->id_ = nextGameObjectId_++;
        }
        gameObject->scene_ = this;
    }
    if (forcedRootId >= nextGameObjectId_) {
        nextGameObjectId_ = forcedRootId + 1;
    }

    for (std::unique_ptr<GameObject>& gameObject :
        sourceScene.gameObjects_) {
        gameObjects_.push_back(std::move(gameObject));
    }
    sourceScene.gameObjects_.clear();
    sourceScene.nextGameObjectId_ = 1;

    if (!sourceRoot->SetParent(parent, false)) {
        // ここへ到達するのはparentの検証漏れがある場合だけ。
        DestroyGameObjectImmediate(*sourceRoot);
        return nullptr;
    }
    return sourceRoot;
}

GameObject* Scene::FindGameObject(GameObject::Id id) const {
    for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
        if (gameObject->GetId() == id && !gameObject->IsPendingDestroy()) {
            return gameObject.get();
        }
    }
    return nullptr;
}

GameObject* Scene::FindGameObject(const std::string& name) const {
    for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
        if (gameObject->GetName() == name && !gameObject->IsPendingDestroy()) {
            return gameObject.get();
        }
    }
    return nullptr;
}

void Scene::Update(float deltaTime) {
    // Edit Sceneへ誤ってUpdateを呼んでも、ComponentやVisual Scriptを動かさない。
    if (!runtimeUpdateEnabled_) {
        return;
    }

    // Update中に新しく生成されたGameObjectは次フレームから実行する。
    const size_t updateCount = gameObjects_.size();
    for (size_t index = 0; index < updateCount; ++index) {
        gameObjects_[index]->UpdateComponents(deltaTime);
    }

    RemoveDestroyedGameObjects();
}

void Scene::SuspendRuntimeCallbacks() {
    if (runtimeCallbacksSuspended_) {
        return;
    }
    runtimeCallbacksSuspended_ = true;

    for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
        for (const std::unique_ptr<Component>& component :
            gameObject->GetComponents()) {
            if (component->IsEnabled()) {
                component->OnDisable();
            }
        }
    }
}

void Scene::ResumeRuntimeCallbacks() {
    if (!runtimeCallbacksSuspended_) {
        return;
    }
    runtimeCallbacksSuspended_ = false;

    for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
        for (const std::unique_ptr<Component>& component :
            gameObject->GetComponents()) {
            if (component->IsEnabled()) {
                component->OnEnable();
            }
        }
    }
}

void Scene::Render() {
    std::vector<RendererComponent*> renderers;

    // GameObjectの保持順と描画順を分離することで、Hierarchyの並びを変えずに描画を最適化できる。
    for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
        if (!gameObject->IsActiveInHierarchy() ||
            gameObject->IsPendingDestroy()) {
            continue;
        }

        for (const std::unique_ptr<Component>& component :
            gameObject->GetComponents()) {
            auto* renderer = dynamic_cast<RendererComponent*>(component.get());
            if (renderer != nullptr && renderer->IsEnabled()) {
                renderers.push_back(renderer);
            }
        }
    }

    // 同じRenderOrder同士はGameObjectの登録順を維持する。
    std::stable_sort(
        renderers.begin(),
        renderers.end(),
        [](const RendererComponent* left, const RendererComponent* right) {
            return left->GetRenderOrder() < right->GetRenderOrder();
        });

    for (RendererComponent* renderer : renderers) {
        renderer->Render();
    }
}

void Scene::RenderShadow(const Matrix4x4& lightViewProjection) {
    for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
        if (!gameObject->IsActiveInHierarchy() ||
            gameObject->IsPendingDestroy()) {
            continue;
        }

        for (const std::unique_ptr<Component>& component :
            gameObject->GetComponents()) {
            auto* renderer = dynamic_cast<RendererComponent*>(component.get());
            if (renderer != nullptr && renderer->IsEnabled()) {
                renderer->RenderShadow(lightViewProjection);
            }
        }
    }
}

void Scene::Clear() {
    // unique_ptrの破棄順に依存して親子ポインタが残らないよう、全関係を先に外す。
    for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
        gameObject->DetachFromHierarchy();
    }
    for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
        gameObject->DestroyComponents();
    }
    gameObjects_.clear();
    nextGameObjectId_ = 1;
    runtimeUpdateEnabled_ = false;
    runtimeCallbacksSuspended_ = false;
}

void Scene::ReplaceWith(Scene&& scene) {
    if (&scene == this) {
        return;
    }

    Clear();
    name_ = std::move(scene.name_);
    nextGameObjectId_ = scene.nextGameObjectId_;
    gameObjects_ = std::move(scene.gameObjects_);
    runtimeUpdateEnabled_ = scene.runtimeUpdateEnabled_;
    runtimeCallbacksSuspended_ = scene.runtimeCallbacksSuspended_;

    // GameObjectは生成元Sceneへの非所有ポインタを持つため、移動先へ付け替える。
    for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
        gameObject->scene_ = this;
    }
    scene.nextGameObjectId_ = 1;
    scene.runtimeUpdateEnabled_ = false;
    scene.runtimeCallbacksSuspended_ = false;
}

void Scene::RemoveDestroyedGameObjects() {
    // eraseで実体が破棄される前なら、親・子の両方がまだ安全に参照できる。
    for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
        if (gameObject->IsPendingDestroy()) {
            gameObject->DetachFromHierarchy();
        }
    }

    const auto removeBegin = std::remove_if(
        gameObjects_.begin(),
        gameObjects_.end(),
        [](const std::unique_ptr<GameObject>& gameObject) {
            if (!gameObject->IsPendingDestroy()) {
                return false;
            }
            gameObject->DestroyComponents();
            return true;
        });
    gameObjects_.erase(removeBegin, gameObjects_.end());
}
