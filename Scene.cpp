#include "Scene.h"

#include "ColliderComponent.h"
#include "RendererComponent.h"
#include "RigidbodyComponent.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

bool OverlapsOnYZ(
    const ColliderComponent::WorldAabb& left,
    const ColliderComponent::WorldAabb& right) {
    return left.minimum.y <= right.maximum.y &&
        left.maximum.y >= right.minimum.y &&
        left.minimum.z <= right.maximum.z &&
        left.maximum.z >= right.minimum.z;
}

} // namespace

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

    ProcessCollisions();
    RemoveDestroyedGameObjects();
}

void Scene::DispatchCollision(
    ColliderComponent& source,
    ColliderComponent& other,
    CollisionEvent event) {
    GameObject* owner = source.GetOwner();
    if (owner == nullptr || owner->IsPendingDestroy()) {
        return;
    }
    const size_t componentCount = owner->GetComponents().size();
    for (size_t index = 0; index < componentCount; ++index) {
        Component* component = owner->GetComponents()[index].get();
        if (component == nullptr || !component->IsEnabled()) {
            continue;
        }
        switch (event) {
        case CollisionEvent::Enter:
            component->OnCollisionEnter(other);
            break;
        case CollisionEvent::Stay:
            component->OnCollisionStay(other);
            break;
        case CollisionEvent::Exit:
            component->OnCollisionExit(other);
            break;
        }
    }
}

void Scene::ProcessCollisions() {
    struct BroadPhaseProxy {
        ColliderComponent* collider = nullptr;
        ColliderComponent::WorldAabb bounds{};
    };
    struct CollisionRecord {
        ColliderComponent* left = nullptr;
        ColliderComponent* right = nullptr;
    };

    std::vector<BroadPhaseProxy> proxies;
    for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
        if (gameObject->IsPendingDestroy() ||
            !gameObject->IsActiveInHierarchy()) {
            continue;
        }
        ColliderComponent* collider =
            gameObject->GetComponent<ColliderComponent>();
        if (collider != nullptr && collider->IsEnabled()) {
            collider->SetColliding(false);
            proxies.push_back({ collider, collider->GetWorldAabb() });
        }
    }

    // Sweep and Prune: X軸の最小値で並べ、X範囲が離れた組を候補から除外する。
    std::sort(
        proxies.begin(),
        proxies.end(),
        [](const BroadPhaseProxy& left, const BroadPhaseProxy& right) {
            return left.bounds.minimum.x < right.bounds.minimum.x;
        });

    lastColliderCount_ = proxies.size();
    lastBroadPhaseCandidateCount_ = 0;
    lastNarrowPhaseTestCount_ = 0;

    std::set<std::pair<GameObject::Id, GameObject::Id>> currentPairs;
    std::vector<CollisionRecord> collisions;
    std::vector<const BroadPhaseProxy*> active;
    for (BroadPhaseProxy& proxy : proxies) {
        std::erase_if(
            active,
            [&proxy](const BroadPhaseProxy* candidate) {
                return candidate->bounds.maximum.x <
                    proxy.bounds.minimum.x;
            });

        for (const BroadPhaseProxy* candidate : active) {
            if (!OverlapsOnYZ(candidate->bounds, proxy.bounds)) {
                continue;
            }
            ++lastBroadPhaseCandidateCount_;
            ColliderComponent& left = *candidate->collider;
            ColliderComponent& right = *proxy.collider;
            ColliderComponent::Contact contact{};
            ++lastNarrowPhaseTestCount_;
            if (!ColliderComponent::ComputeContact(left, right, contact)) {
                continue;
            }

            GameObject::Id leftId = left.GetOwner()->GetId();
            GameObject::Id rightId = right.GetOwner()->GetId();
            if (leftId > rightId) {
                std::swap(leftId, rightId);
            }
            const std::pair<GameObject::Id, GameObject::Id> pair = {
                leftId, rightId
            };
            currentPairs.insert(pair);
            left.SetColliding(true);
            right.SetColliding(true);
            collisions.push_back({ &left, &right });
        }
        active.push_back(&proxy);
    }

    // 積み重なりでも安定しやすいよう、接触を数回反復して解決する。
    constexpr size_t kSolverIterations = 4;
    for (size_t iteration = 0; iteration < kSolverIterations; ++iteration) {
        for (const CollisionRecord& collision : collisions) {
            ColliderComponent::Contact contact{};
            if (collision.left->IsTrigger() || collision.right->IsTrigger() ||
                !ColliderComponent::ComputeContact(
                    *collision.left, *collision.right, contact)) {
                continue;
            }
            ResolveCollision(
                *collision.left,
                *collision.right,
                contact.normal,
                contact.penetration);
        }
    }

    for (const CollisionRecord& collision : collisions) {
        ColliderComponent& left = *collision.left;
        ColliderComponent& right = *collision.right;
        GameObject::Id leftId = left.GetOwner()->GetId();
        GameObject::Id rightId = right.GetOwner()->GetId();
        if (leftId > rightId) {
            std::swap(leftId, rightId);
        }
        const std::pair<GameObject::Id, GameObject::Id> pair = {
            leftId, rightId
        };
        if (!left.GetOwner()->IsPendingDestroy() &&
            !right.GetOwner()->IsPendingDestroy()) {
            const CollisionEvent event = collisionPairs_.contains(pair)
                ? CollisionEvent::Stay
                : CollisionEvent::Enter;
            DispatchCollision(left, right, event);
            DispatchCollision(right, left, event);
        }
    }

    for (const auto& previousPair : collisionPairs_) {
        if (currentPairs.contains(previousPair)) {
            continue;
        }
        GameObject* leftObject = FindGameObject(previousPair.first);
        GameObject* rightObject = FindGameObject(previousPair.second);
        ColliderComponent* left = leftObject != nullptr
            ? leftObject->GetComponent<ColliderComponent>()
            : nullptr;
        ColliderComponent* right = rightObject != nullptr
            ? rightObject->GetComponent<ColliderComponent>()
            : nullptr;
        if (left != nullptr && right != nullptr) {
            DispatchCollision(*left, *right, CollisionEvent::Exit);
            DispatchCollision(*right, *left, CollisionEvent::Exit);
        }
    }
    collisionPairs_ = std::move(currentPairs);
}

void Scene::ResolveCollision(
    ColliderComponent& left,
    ColliderComponent& right,
    const Vector3& normal,
    float penetration) {
    RigidbodyComponent* leftBody =
        left.GetOwner()->GetComponent<RigidbodyComponent>();
    RigidbodyComponent* rightBody =
        right.GetOwner()->GetComponent<RigidbodyComponent>();
    const bool leftBodyEnabled =
        leftBody != nullptr && leftBody->IsEnabled();
    const bool rightBodyEnabled =
        rightBody != nullptr && rightBody->IsEnabled();
    const float leftInverseMass = leftBodyEnabled
        ? leftBody->GetInverseMass()
        : 0.0f;
    const float rightInverseMass = rightBodyEnabled
        ? rightBody->GetInverseMass()
        : 0.0f;
    const float inverseMassSum = leftInverseMass + rightInverseMass;
    if (inverseMassSum <= 0.0f) {
        return;
    }

    constexpr float kGroundNormalThreshold = 0.5f;
    if (leftBody != nullptr && leftInverseMass > 0.0f &&
        normal.y < -kGroundNormalThreshold) {
        leftBody->SetGrounded(true);
    }
    if (rightBody != nullptr && rightInverseMass > 0.0f &&
        normal.y > kGroundNormalThreshold) {
        rightBody->SetGrounded(true);
    }

    // 小さな許容値を残して、浮動小数誤差による細かな振動を抑える。
    constexpr float kPenetrationSlop = 0.0005f;
    constexpr float kCorrectionPercent = 0.8f;
    const float correctionMagnitude =
        (std::max)(penetration - kPenetrationSlop, 0.0f) *
        kCorrectionPercent / inverseMassSum;
    const Vector3 correction = normal * correctionMagnitude;
    if (leftInverseMass > 0.0f) {
        TransformComponent& transform = left.GetOwner()->GetTransform();
        transform.SetWorldPosition(
            transform.GetWorldPosition() - correction * leftInverseMass);
    }
    if (rightInverseMass > 0.0f) {
        TransformComponent& transform = right.GetOwner()->GetTransform();
        transform.SetWorldPosition(
            transform.GetWorldPosition() + correction * rightInverseMass);
    }

    Vector3 leftVelocity = leftBodyEnabled &&
        leftBody->GetBodyType() != RigidbodyComponent::BodyType::Static
        ? leftBody->GetVelocity()
        : Vector3{};
    Vector3 rightVelocity = rightBodyEnabled &&
        rightBody->GetBodyType() != RigidbodyComponent::BodyType::Static
        ? rightBody->GetVelocity()
        : Vector3{};
    Vector3 relativeVelocity = rightVelocity - leftVelocity;
    const float velocityAlongNormal =
        Vector3::Dot(relativeVelocity, normal);
    if (velocityAlongNormal >= 0.0f) {
        return;
    }

    const float leftRestitution = leftBodyEnabled
        ? leftBody->GetRestitution()
        : 0.0f;
    const float rightRestitution = rightBodyEnabled
        ? rightBody->GetRestitution()
        : 0.0f;
    const float restitution = (std::min)(
        leftRestitution, rightRestitution);
    const float normalImpulseMagnitude =
        -(1.0f + restitution) * velocityAlongNormal / inverseMassSum;
    const Vector3 normalImpulse =
        normal * normalImpulseMagnitude;
    if (leftBody != nullptr && leftInverseMass > 0.0f) {
        leftVelocity = leftVelocity - normalImpulse * leftInverseMass;
        leftBody->SetVelocity(leftVelocity);
    }
    if (rightBody != nullptr && rightInverseMass > 0.0f) {
        rightVelocity = rightVelocity + normalImpulse * rightInverseMass;
        rightBody->SetVelocity(rightVelocity);
    }

    relativeVelocity = rightVelocity - leftVelocity;
    Vector3 tangent = relativeVelocity -
        normal * Vector3::Dot(relativeVelocity, normal);
    const float tangentLength = tangent.Length();
    if (tangentLength <= 0.000001f) {
        return;
    }
    tangent = tangent / tangentLength;
    float tangentImpulseMagnitude =
        -Vector3::Dot(relativeVelocity, tangent) / inverseMassSum;
    const float leftFriction = leftBodyEnabled
        ? leftBody->GetFriction()
        : 0.5f;
    const float rightFriction = rightBodyEnabled
        ? rightBody->GetFriction()
        : 0.5f;
    const float friction = std::sqrt(leftFriction * rightFriction);
    const float maximumFrictionImpulse =
        normalImpulseMagnitude * friction;
    tangentImpulseMagnitude = (std::clamp)(
        tangentImpulseMagnitude,
        -maximumFrictionImpulse,
        maximumFrictionImpulse);
    const Vector3 frictionImpulse = tangent * tangentImpulseMagnitude;
    if (leftBody != nullptr && leftInverseMass > 0.0f) {
        leftBody->SetVelocity(
            leftBody->GetVelocity() - frictionImpulse * leftInverseMass);
    }
    if (rightBody != nullptr && rightInverseMass > 0.0f) {
        rightBody->SetVelocity(
            rightBody->GetVelocity() + frictionImpulse * rightInverseMass);
    }
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
    collisionPairs_.clear();
    lastColliderCount_ = 0;
    lastBroadPhaseCandidateCount_ = 0;
    lastNarrowPhaseTestCount_ = 0;
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
    collisionPairs_.clear();
    lastColliderCount_ = 0;
    lastBroadPhaseCandidateCount_ = 0;
    lastNarrowPhaseTestCount_ = 0;

    // GameObjectは生成元Sceneへの非所有ポインタを持つため、移動先へ付け替える。
    for (const std::unique_ptr<GameObject>& gameObject : gameObjects_) {
        gameObject->scene_ = this;
    }
    scene.nextGameObjectId_ = 1;
    scene.runtimeUpdateEnabled_ = false;
    scene.runtimeCallbacksSuspended_ = false;
    scene.collisionPairs_.clear();
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
