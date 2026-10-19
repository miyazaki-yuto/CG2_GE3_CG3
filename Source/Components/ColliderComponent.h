#pragma once

#include "CommonTypes.h"
#include "RendererComponent.h"

#include <array>

class PrimitiveDrawer;

// Sphere／AABB／OBBをGameObjectのTransformに追従させる当たり判定Component。
// Sceneがペアを検査し、Component::OnCollisionEnter/Stay/Exitを呼び出す。
class ColliderComponent final : public RendererComponent {
public:
    enum class ColliderType {
        Sphere,
        Aabb,
        Obb
    };

    struct WorldSphere {
        Vector3 center{};
        float radius = 0.5f;
    };

    struct WorldBox {
        Vector3 center{};
        std::array<Vector3, 3> axes{
            Vector3{ 1.0f, 0.0f, 0.0f },
            Vector3{ 0.0f, 1.0f, 0.0f },
            Vector3{ 0.0f, 0.0f, 1.0f }
        };
        Vector3 halfExtents{ 0.5f, 0.5f, 0.5f };
    };

    struct WorldAabb {
        Vector3 minimum{};
        Vector3 maximum{};
    };

    // normalはleftからrightへ向き、penetrationは重なり量を表す。
    struct Contact {
        Vector3 normal{ 0.0f, 1.0f, 0.0f };
        float penetration = 0.0f;
    };

    ColliderComponent(
        PrimitiveDrawer* primitiveDrawer,
        int debugTextureHandle,
        ColliderType colliderType = ColliderType::Sphere);
    ~ColliderComponent() override = default;

    ColliderComponent(const ColliderComponent&) = delete;
    ColliderComponent& operator=(const ColliderComponent&) = delete;

    void Render() override;

    ColliderType GetColliderType() const { return colliderType_; }
    void SetColliderType(ColliderType colliderType) {
        colliderType_ = colliderType;
    }
    const Vector3& GetCenterOffset() const { return centerOffset_; }
    void SetCenterOffset(const Vector3& offset) { centerOffset_ = offset; }
    float GetRadius() const { return radius_; }
    void SetRadius(float radius);
    const Vector3& GetHalfExtents() const { return halfExtents_; }
    void SetHalfExtents(const Vector3& halfExtents);
    bool IsTrigger() const { return isTrigger_; }
    void SetTrigger(bool trigger) { isTrigger_ = trigger; }
    bool IsDebugDrawEnabled() const { return debugDrawEnabled_; }
    void SetDebugDrawEnabled(bool enabled) { debugDrawEnabled_ = enabled; }
    bool IsColliding() const { return isColliding_; }
    void SetColliding(bool colliding) { isColliding_ = colliding; }

    WorldSphere GetWorldSphere() const;
    WorldBox GetWorldBox() const;
    WorldAabb GetWorldAabb() const;
    static bool ComputeContact(
        const ColliderComponent& left,
        const ColliderComponent& right,
        Contact& contact);
    static bool Intersects(
        const ColliderComponent& left,
        const ColliderComponent& right);

private:
    void DrawDebugBox(const WorldBox& box, const Vector4& color);

    PrimitiveDrawer* primitiveDrawer_ = nullptr;
    int debugTextureHandle_ = -1;
    ColliderType colliderType_ = ColliderType::Sphere;
    Vector3 centerOffset_{};
    float radius_ = 0.5f;
    Vector3 halfExtents_{ 0.5f, 0.5f, 0.5f };
    bool isTrigger_ = false;
    bool debugDrawEnabled_ = true;
    bool isColliding_ = false;
};
