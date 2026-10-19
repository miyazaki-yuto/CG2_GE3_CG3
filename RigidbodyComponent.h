#pragma once

#include "CommonTypes.h"
#include "Component.h"

// GameObjectへ並進運動を追加する物理Component。
// 回転運動は扱わず、ColliderComponentとの接触解決をSceneへ任せる。
class RigidbodyComponent final : public Component {
public:
    enum class BodyType {
        Static,
        Dynamic,
        Kinematic
    };

    RigidbodyComponent() = default;
    ~RigidbodyComponent() override = default;

    RigidbodyComponent(const RigidbodyComponent&) = delete;
    RigidbodyComponent& operator=(const RigidbodyComponent&) = delete;

    void Update(float deltaTime) override;
    void OnDisable() override {
        isGrounded_ = false;
        accumulatedForce_ = {};
    }

    BodyType GetBodyType() const { return bodyType_; }
    void SetBodyType(BodyType bodyType);

    float GetMass() const { return mass_; }
    void SetMass(float mass);
    float GetInverseMass() const;

    const Vector3& GetVelocity() const { return velocity_; }
    void SetVelocity(const Vector3& velocity) { velocity_ = velocity; }
    const Vector3& GetAcceleration() const { return acceleration_; }
    void SetAcceleration(const Vector3& acceleration) {
        acceleration_ = acceleration;
    }

    bool UsesGravity() const { return useGravity_; }
    void SetUseGravity(bool useGravity) { useGravity_ = useGravity; }
    float GetDrag() const { return drag_; }
    void SetDrag(float drag);
    float GetRestitution() const { return restitution_; }
    void SetRestitution(float restitution);
    float GetFriction() const { return friction_; }
    void SetFriction(float friction);

    bool IsGrounded() const { return isGrounded_; }

    // Dynamic Bodyへ継続力または瞬間的な速度変化を与える。
    void AddForce(const Vector3& force);
    void AddImpulse(const Vector3& impulse);
    void ClearForces() { accumulatedForce_ = {}; }

    static constexpr Vector3 kGravity{ 0.0f, -9.80665f, 0.0f };

private:
    friend class Scene;

    void SetGrounded(bool grounded) { isGrounded_ = grounded; }

    BodyType bodyType_ = BodyType::Dynamic;
    float mass_ = 1.0f;
    Vector3 velocity_{};
    Vector3 acceleration_{};
    Vector3 accumulatedForce_{};
    bool useGravity_ = true;
    float drag_ = 0.0f;
    float restitution_ = 0.0f;
    float friction_ = 0.5f;
    bool isGrounded_ = false;
};
