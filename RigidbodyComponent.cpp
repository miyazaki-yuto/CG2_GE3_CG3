#include "RigidbodyComponent.h"

#include "GameObject.h"

#include <algorithm>

void RigidbodyComponent::SetBodyType(BodyType bodyType) {
    bodyType_ = bodyType;
    isGrounded_ = false;
    accumulatedForce_ = {};
    if (bodyType_ == BodyType::Static) {
        velocity_ = {};
    }
}

void RigidbodyComponent::SetMass(float mass) {
    mass_ = (std::max)(mass, 0.001f);
}

float RigidbodyComponent::GetInverseMass() const {
    return bodyType_ == BodyType::Dynamic ? 1.0f / mass_ : 0.0f;
}

void RigidbodyComponent::SetDrag(float drag) {
    drag_ = (std::max)(drag, 0.0f);
}

void RigidbodyComponent::SetRestitution(float restitution) {
    restitution_ = (std::clamp)(restitution, 0.0f, 1.0f);
}

void RigidbodyComponent::SetFriction(float friction) {
    friction_ = (std::clamp)(friction, 0.0f, 1.0f);
}

void RigidbodyComponent::AddForce(const Vector3& force) {
    if (bodyType_ == BodyType::Dynamic) {
        accumulatedForce_ = accumulatedForce_ + force;
    }
}

void RigidbodyComponent::AddImpulse(const Vector3& impulse) {
    const float inverseMass = GetInverseMass();
    if (inverseMass > 0.0f) {
        velocity_ = velocity_ + impulse * inverseMass;
    }
}

void RigidbodyComponent::Update(float deltaTime) {
    isGrounded_ = false;
    if (GetOwner() == nullptr || deltaTime <= 0.0f ||
        bodyType_ == BodyType::Static) {
        accumulatedForce_ = {};
        return;
    }

    if (bodyType_ == BodyType::Dynamic) {
        Vector3 totalAcceleration = acceleration_ +
            accumulatedForce_ * GetInverseMass();
        if (useGravity_) {
            totalAcceleration = totalAcceleration + kGravity;
        }
        velocity_ = velocity_ + totalAcceleration * deltaTime;

        // 1 / (1 + drag * dt) は大きいdeltaTimeでも符号が反転しない。
        const float damping = 1.0f / (1.0f + drag_ * deltaTime);
        velocity_ = velocity_ * damping;
    }
    accumulatedForce_ = {};

    TransformComponent& transform = GetOwner()->GetTransform();
    transform.SetWorldPosition(
        transform.GetWorldPosition() + velocity_ * deltaTime);
}
