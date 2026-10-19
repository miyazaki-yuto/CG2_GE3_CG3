#include "GameObject.h"

#include <algorithm>
#include <cassert>

GameObject::GameObject(Scene* scene, Id id, std::string name)
    : scene_(scene), id_(id), name_(std::move(name)) {
    // Transformだけは任意Componentではなく、GameObject生成時に必ず作る
    auto transform = std::make_unique<TransformComponent>();
    transform_ = transform.get();
    transform_->owner_ = this;
    components_.push_back(std::move(transform));
    transform_->Awake();
}

GameObject::~GameObject() {
    DetachFromHierarchy();
    DestroyComponents();
}

bool GameObject::IsActiveInHierarchy() const {
    if (!active_) {
        return false;
    }
    return parent_ == nullptr || parent_->IsActiveInHierarchy();
}

bool GameObject::SetParent(GameObject* newParent) {
    return SetParent(newParent, false);
}

bool GameObject::SetParent(
    GameObject* newParent,
    bool keepWorldTransform) {
    if (newParent == parent_) {
        return true;
    }

    if (newParent != nullptr) {
        if (newParent == this || newParent->scene_ != scene_ ||
            newParent->pendingDestroy_ || pendingDestroy_) {
            return false;
        }

        // 新しい親から祖先をたどり、自分へ戻る場合は循環になるため拒否する
        for (GameObject* ancestor = newParent;
            ancestor != nullptr;
            ancestor = ancestor->parent_) {
            if (ancestor == this) {
                return false;
            }
        }
    }

    // 親を差し替える前に保存しないと、GetWorldMatrixの計算結果も変化する
    Matrix4x4 previousWorldMatrix{};
    if (keepWorldTransform) {
        previousWorldMatrix = transform_->GetWorldMatrix();
    }

    if (parent_ != nullptr) {
        parent_->RemoveChild(this);
    }

    parent_ = newParent;
    if (parent_ != nullptr) {
        parent_->children_.push_back(this);
    }
    if (keepWorldTransform) {
        transform_->SetWorldMatrix(previousWorldMatrix);
    }
    return true;
}

void GameObject::RemoveChild(GameObject* child) {
    const auto iterator = std::remove(
        children_.begin(), children_.end(), child);
    children_.erase(iterator, children_.end());
}

bool GameObject::RemoveComponent(Component* component) {
    if (component == nullptr || component == transform_) {
        return false;
    }

    const auto iterator = std::find_if(
        components_.begin(),
        components_.end(),
        [component](const std::unique_ptr<Component>& candidate) {
            return candidate.get() == component;
        });
    if (iterator == components_.end()) {
        return false;
    }

    (*iterator)->OnDestroy();
    (*iterator)->owner_ = nullptr;
    components_.erase(iterator);
    return true;
}

void GameObject::MarkForDestroy() {
    if (pendingDestroy_) {
        return;
    }

    pendingDestroy_ = true;
    // 親をDestroyした場合は、その配下も同じフレーム末尾に削除する
    for (GameObject* child : children_) {
        if (child != nullptr) {
            child->MarkForDestroy();
        }
    }
}

void GameObject::DetachFromHierarchy() {
    if (parent_ != nullptr) {
        parent_->RemoveChild(this);
        parent_ = nullptr;
    }

    // Scene削除処理では子も削除されるが、デストラクタ単体でもダングリングを残さない
    for (GameObject* child : children_) {
        if (child != nullptr && child->parent_ == this) {
            child->parent_ = nullptr;
        }
    }
    children_.clear();
}

void GameObject::UpdateComponents(float deltaTime) {
    if (!IsActiveInHierarchy() || pendingDestroy_) {
        return;
    }

    // Componentは追加順に実行する。実行中に追加されたComponentは次フレームから更新する
    const size_t updateCount = components_.size();
    for (size_t index = 0; index < updateCount; ++index) {
        Component* component = components_[index].get();
        assert(component != nullptr);
        if (!component->enabled_) {
            continue;
        }

        if (!component->started_) {
            component->Start();
            component->started_ = true;
        }
        component->Update(deltaTime);

        // Component内でOwnerが破棄予約された場合は、残りの更新を止める
        if (pendingDestroy_) {
            break;
        }
    }
}

void GameObject::DestroyComponents() {
    if (destroyCallbacksCalled_) {
        return;
    }
    destroyCallbacksCalled_ = true;

    // 依存するComponentから先に片付けられるよう、追加と逆順で破棄通知する
    for (auto iterator = components_.rbegin();
        iterator != components_.rend();
        ++iterator) {
        (*iterator)->OnDestroy();
    }
}
