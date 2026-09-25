#pragma once

#include "Component.h"
#include "TransformComponent.h"

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

class Scene;

// Scene内に存在する1つのオブジェクト。
// データを直接増やさず、Componentを追加することで能力を組み合わせる。
class GameObject {
public:
    using Id = uint64_t;

    GameObject(Scene* scene, Id id, std::string name);
    ~GameObject();

    GameObject(const GameObject&) = delete;
    GameObject& operator=(const GameObject&) = delete;

    Id GetId() const { return id_; }
    const std::string& GetName() const { return name_; }
    void SetName(const std::string& name) { name_ = name; }

    bool IsActive() const { return active_; }
    void SetActive(bool active) { active_ = active; }
    bool IsActiveInHierarchy() const;

    bool IsPendingDestroy() const { return pendingDestroy_; }

    TransformComponent& GetTransform() { return *transform_; }
    const TransformComponent& GetTransform() const { return *transform_; }

    Scene* GetScene() const { return scene_; }
    GameObject* GetParent() const { return parent_; }
    const std::vector<GameObject*>& GetChildren() const { return children_; }

    // newParent=nullptrでルートへ戻す。成功時はローカル座標を維持する。
    // 自分自身・自分の子孫・別SceneのGameObjectは親に指定できない。
    bool SetParent(GameObject* newParent);

    // keepWorldTransform=trueなら、親変更前の見た目上のWorld座標を維持する。
    // Hierarchyのドラッグ＆ドロップではtrue、Scene読み込みではfalseを使用する。
    bool SetParent(GameObject* newParent, bool keepWorldTransform);

    template<class T, class... Args>
    T* AddComponent(Args&&... args) {
        static_assert(
            std::is_base_of_v<Component, T>,
            "T must derive from Component.");

        // Unityと同様にTransformは必ず1つだけにする。
        if constexpr (std::is_same_v<T, TransformComponent>) {
            return transform_;
        }

        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T* componentPointer = component.get();
        componentPointer->owner_ = this;
        components_.push_back(std::move(component));
        componentPointer->Awake();
        return componentPointer;
    }

    template<class T>
    T* GetComponent() const {
        static_assert(
            std::is_base_of_v<Component, T>,
            "T must derive from Component.");

        for (const std::unique_ptr<Component>& component : components_) {
            if (T* result = dynamic_cast<T*>(component.get())) {
                return result;
            }
        }
        return nullptr;
    }

    const std::vector<std::unique_ptr<Component>>& GetComponents() const {
        return components_;
    }

    // Transformは必須Componentなので削除できない。
    // 削除時はOnDestroyを呼び、成功した場合だけtrueを返す。
    bool RemoveComponent(Component* component);

private:
    friend class Scene;

    void UpdateComponents(float deltaTime);
    void MarkForDestroy();
    void DestroyComponents();
    void DetachFromHierarchy();
    void RemoveChild(GameObject* child);

    Scene* scene_ = nullptr;
    Id id_ = 0;
    std::string name_;
    bool active_ = true;
    bool pendingDestroy_ = false;
    bool destroyCallbacksCalled_ = false;

    GameObject* parent_ = nullptr;
    std::vector<GameObject*> children_;

    std::vector<std::unique_ptr<Component>> components_;
    TransformComponent* transform_ = nullptr;
};
