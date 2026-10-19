#include "ColliderComponent.h"

#include "GameObject.h"
#include "PrimitiveDrawer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

float GetAxisValue(const Vector3& value, size_t axis) {
    return axis == 0 ? value.x : (axis == 1 ? value.y : value.z);
}

Vector3 GetWorldBasis(const Matrix4x4& matrix, size_t axis) {
    return {
        matrix.m[axis][0],
        matrix.m[axis][1],
        matrix.m[axis][2]
    };
}

Vector3 ClosestPointOnBox(
    const Vector3& point,
    const ColliderComponent::WorldBox& box) {
    Vector3 closest = box.center;
    const Vector3 difference = point - box.center;
    for (size_t axis = 0; axis < 3; ++axis) {
        const float distance = (std::clamp)(
            Vector3::Dot(difference, box.axes[axis]),
            -GetAxisValue(box.halfExtents, axis),
            GetAxisValue(box.halfExtents, axis));
        closest = closest + box.axes[axis] * distance;
    }
    return closest;
}

bool ComputeSphereBoxContact(
    const ColliderComponent::WorldSphere& sphere,
    const ColliderComponent::WorldBox& box,
    ColliderComponent::Contact& contact) {
    const Vector3 closest = ClosestPointOnBox(sphere.center, box);
    const Vector3 sphereToBox = closest - sphere.center;
    const float distanceSquared = Vector3::Dot(sphereToBox, sphereToBox);
    if (distanceSquared > sphere.radius * sphere.radius) {
        return false;
    }

    constexpr float kEpsilon = 0.000001f;
    if (distanceSquared > kEpsilon * kEpsilon) {
        const float distance = std::sqrt(distanceSquared);
        contact.normal = sphereToBox / distance;
        contact.penetration = sphere.radius - distance;
        return true;
    }

    // Sphere中心がBox内部にある場合、最近接点が中心と一致する。
    // 最も近い面の外へSphereを出す向きを選ぶ。
    const Vector3 localCenter = sphere.center - box.center;
    float nearestFaceDistance = (std::numeric_limits<float>::max)();
    Vector3 outward{ 1.0f, 0.0f, 0.0f };
    for (size_t axis = 0; axis < 3; ++axis) {
        const float projected = Vector3::Dot(localCenter, box.axes[axis]);
        const float faceDistance =
            GetAxisValue(box.halfExtents, axis) - std::abs(projected);
        if (faceDistance < nearestFaceDistance) {
            nearestFaceDistance = faceDistance;
            outward = box.axes[axis] * (projected >= 0.0f ? 1.0f : -1.0f);
        }
    }
    // 補正ではleft -= normalを行うため、外向きと逆をcontact normalにする。
    contact.normal = outward * -1.0f;
    contact.penetration = sphere.radius + nearestFaceDistance;
    return true;
}

float ProjectBoxRadius(
    const ColliderComponent::WorldBox& box,
    const Vector3& axis) {
    return box.halfExtents.x * std::abs(Vector3::Dot(box.axes[0], axis)) +
        box.halfExtents.y * std::abs(Vector3::Dot(box.axes[1], axis)) +
        box.halfExtents.z * std::abs(Vector3::Dot(box.axes[2], axis));
}

bool ComputeBoxContact(
    const ColliderComponent::WorldBox& left,
    const ColliderComponent::WorldBox& right,
    ColliderComponent::Contact& contact) {
    constexpr float kAxisEpsilon = 0.000001f;
    const Vector3 centerDifference = right.center - left.center;
    float minimumOverlap = (std::numeric_limits<float>::max)();
    Vector3 minimumAxis{ 1.0f, 0.0f, 0.0f };

    const auto testAxis = [&](Vector3 axis) {
        const float length = axis.Length();
        if (length <= kAxisEpsilon) {
            return true;
        }
        axis = axis / length;
        const float overlap = ProjectBoxRadius(left, axis) +
            ProjectBoxRadius(right, axis) -
            std::abs(Vector3::Dot(centerDifference, axis));
        if (overlap < 0.0f) {
            return false;
        }
        if (overlap < minimumOverlap) {
            minimumOverlap = overlap;
            minimumAxis = Vector3::Dot(centerDifference, axis) >= 0.0f
                ? axis
                : axis * -1.0f;
        }
        return true;
    };

    for (size_t axis = 0; axis < 3; ++axis) {
        if (!testAxis(left.axes[axis]) || !testAxis(right.axes[axis])) {
            return false;
        }
    }
    for (size_t leftAxis = 0; leftAxis < 3; ++leftAxis) {
        for (size_t rightAxis = 0; rightAxis < 3; ++rightAxis) {
            if (!testAxis(Cross(
                    left.axes[leftAxis], right.axes[rightAxis]))) {
                return false;
            }
        }
    }
    contact.normal = minimumAxis;
    contact.penetration = minimumOverlap;
    return true;
}

} // namespace

ColliderComponent::ColliderComponent(
    PrimitiveDrawer* primitiveDrawer,
    int debugTextureHandle,
    ColliderType colliderType)
    : primitiveDrawer_(primitiveDrawer),
      debugTextureHandle_(debugTextureHandle),
      colliderType_(colliderType) {
    SetRenderOrder(kOverlayRenderOrder - 1);
    SetBlendMode(BlendMode::Normal);
}

void ColliderComponent::SetRadius(float radius) {
    radius_ = (std::max)(radius, 0.001f);
}

void ColliderComponent::SetHalfExtents(const Vector3& halfExtents) {
    halfExtents_ = {
        (std::max)(std::abs(halfExtents.x), 0.001f),
        (std::max)(std::abs(halfExtents.y), 0.001f),
        (std::max)(std::abs(halfExtents.z), 0.001f)
    };
}

ColliderComponent::WorldSphere ColliderComponent::GetWorldSphere() const {
    WorldSphere sphere{};
    if (GetOwner() == nullptr) {
        sphere.radius = radius_;
        return sphere;
    }
    const Matrix4x4 world = GetOwner()->GetTransform().GetWorldMatrix();
    sphere.center = Transform(centerOffset_, world);
    const float scaleX = GetWorldBasis(world, 0).Length();
    const float scaleY = GetWorldBasis(world, 1).Length();
    const float scaleZ = GetWorldBasis(world, 2).Length();
    sphere.radius = radius_ * (std::max)({ scaleX, scaleY, scaleZ });
    return sphere;
}

ColliderComponent::WorldBox ColliderComponent::GetWorldBox() const {
    WorldBox box{};
    if (GetOwner() == nullptr) {
        box.center = centerOffset_;
        box.halfExtents = halfExtents_;
        return box;
    }

    const Matrix4x4 world = GetOwner()->GetTransform().GetWorldMatrix();
    box.center = Transform(centerOffset_, world);
    if (colliderType_ == ColliderType::Aabb) {
        const float maximum = (std::numeric_limits<float>::max)();
        Vector3 minimum{ maximum, maximum, maximum };
        Vector3 maximumValue{ -maximum, -maximum, -maximum };
        for (uint32_t cornerIndex = 0; cornerIndex < 8; ++cornerIndex) {
            const Vector3 localCorner = {
                centerOffset_.x +
                    ((cornerIndex & 1) != 0 ? halfExtents_.x : -halfExtents_.x),
                centerOffset_.y +
                    ((cornerIndex & 2) != 0 ? halfExtents_.y : -halfExtents_.y),
                centerOffset_.z +
                    ((cornerIndex & 4) != 0 ? halfExtents_.z : -halfExtents_.z)
            };
            const Vector3 corner = Transform(localCorner, world);
            minimum.x = (std::min)(minimum.x, corner.x);
            minimum.y = (std::min)(minimum.y, corner.y);
            minimum.z = (std::min)(minimum.z, corner.z);
            maximumValue.x = (std::max)(maximumValue.x, corner.x);
            maximumValue.y = (std::max)(maximumValue.y, corner.y);
            maximumValue.z = (std::max)(maximumValue.z, corner.z);
        }
        box.center = (minimum + maximumValue) * 0.5f;
        box.halfExtents = (maximumValue - minimum) * 0.5f;
        return box;
    }

    for (size_t axis = 0; axis < 3; ++axis) {
        Vector3 basis = GetWorldBasis(world, axis);
        const float length = basis.Length();
        if (length > 0.000001f) {
            basis = basis / length;
        }
        box.axes[axis] = basis;
        if (axis == 0) {
            box.halfExtents.x = halfExtents_.x * length;
        } else if (axis == 1) {
            box.halfExtents.y = halfExtents_.y * length;
        } else {
            box.halfExtents.z = halfExtents_.z * length;
        }
    }
    return box;
}

ColliderComponent::WorldAabb ColliderComponent::GetWorldAabb() const {
    if (colliderType_ == ColliderType::Sphere) {
        const WorldSphere sphere = GetWorldSphere();
        const Vector3 radius{
            sphere.radius, sphere.radius, sphere.radius
        };
        return { sphere.center - radius, sphere.center + radius };
    }

    const WorldBox box = GetWorldBox();
    const Vector3 extent = {
        std::abs(box.axes[0].x) * box.halfExtents.x +
            std::abs(box.axes[1].x) * box.halfExtents.y +
            std::abs(box.axes[2].x) * box.halfExtents.z,
        std::abs(box.axes[0].y) * box.halfExtents.x +
            std::abs(box.axes[1].y) * box.halfExtents.y +
            std::abs(box.axes[2].y) * box.halfExtents.z,
        std::abs(box.axes[0].z) * box.halfExtents.x +
            std::abs(box.axes[1].z) * box.halfExtents.y +
            std::abs(box.axes[2].z) * box.halfExtents.z
    };
    return { box.center - extent, box.center + extent };
}

bool ColliderComponent::ComputeContact(
    const ColliderComponent& left,
    const ColliderComponent& right,
    Contact& contact) {
    const bool leftSphere = left.colliderType_ == ColliderType::Sphere;
    const bool rightSphere = right.colliderType_ == ColliderType::Sphere;
    if (leftSphere && rightSphere) {
        const WorldSphere leftWorld = left.GetWorldSphere();
        const WorldSphere rightWorld = right.GetWorldSphere();
        const Vector3 difference = rightWorld.center - leftWorld.center;
        const float distanceSquared = Vector3::Dot(difference, difference);
        const float radius = leftWorld.radius + rightWorld.radius;
        if (distanceSquared > radius * radius) {
            return false;
        }
        constexpr float kEpsilon = 0.000001f;
        if (distanceSquared > kEpsilon * kEpsilon) {
            const float distance = std::sqrt(distanceSquared);
            contact.normal = difference / distance;
            contact.penetration = radius - distance;
        } else {
            contact.normal = { 1.0f, 0.0f, 0.0f };
            contact.penetration = radius;
        }
        return true;
    }
    if (leftSphere) {
        return ComputeSphereBoxContact(
            left.GetWorldSphere(), right.GetWorldBox(), contact);
    }
    if (rightSphere) {
        const bool collided = ComputeSphereBoxContact(
            right.GetWorldSphere(), left.GetWorldBox(), contact);
        contact.normal = contact.normal * -1.0f;
        return collided;
    }
    return ComputeBoxContact(left.GetWorldBox(), right.GetWorldBox(), contact);
}

bool ColliderComponent::Intersects(
    const ColliderComponent& left,
    const ColliderComponent& right) {
    Contact contact{};
    return ComputeContact(left, right, contact);
}

void ColliderComponent::DrawDebugBox(
    const WorldBox& box,
    const Vector4& color) {
    const Vector3 positions[8] = {
        { -0.5f, -0.5f, -0.5f }, { 0.5f, -0.5f, -0.5f },
        { -0.5f,  0.5f, -0.5f }, { 0.5f,  0.5f, -0.5f },
        { -0.5f, -0.5f,  0.5f }, { 0.5f, -0.5f,  0.5f },
        { -0.5f,  0.5f,  0.5f }, { 0.5f,  0.5f,  0.5f }
    };
    constexpr uint32_t indices[36] = {
        0, 2, 1, 1, 2, 3, 5, 7, 4, 4, 7, 6,
        4, 6, 0, 0, 6, 2, 1, 3, 5, 5, 3, 7,
        2, 6, 3, 3, 6, 7, 4, 0, 5, 5, 0, 1
    };
    Matrix4x4 world = MakeIdentity4x4();
    const float extents[3] = {
        box.halfExtents.x * 2.0f,
        box.halfExtents.y * 2.0f,
        box.halfExtents.z * 2.0f
    };
    for (size_t axis = 0; axis < 3; ++axis) {
        world.m[axis][0] = box.axes[axis].x * extents[axis];
        world.m[axis][1] = box.axes[axis].y * extents[axis];
        world.m[axis][2] = box.axes[axis].z * extents[axis];
    }
    world.m[3][0] = box.center.x;
    world.m[3][1] = box.center.y;
    world.m[3][2] = box.center.z;
    const UVTransform uv = {
        { 1.0f, 1.0f }, 0.0f, { 0.0f, 0.0f }
    };
    for (size_t triangle = 0; triangle < 12; ++triangle) {
        TextureVertexData vertices[3]{};
        for (size_t vertexIndex = 0; vertexIndex < 3; ++vertexIndex) {
            const Vector3 position = positions[indices[triangle * 3 + vertexIndex]];
            vertices[vertexIndex].position = {
                position.x, position.y, position.z, 1.0f
            };
            vertices[vertexIndex].texcoord = {
                vertexIndex == 1 ? 1.0f : 0.0f,
                vertexIndex == 2 ? 1.0f : 0.0f
            };
        }
        Vector3 normal = Cross(
            positions[indices[triangle * 3 + 1]] -
                positions[indices[triangle * 3]],
            positions[indices[triangle * 3 + 2]] -
                positions[indices[triangle * 3]]);
        normal.Normalize();
        for (TextureVertexData& vertex : vertices) {
            vertex.normal = normal;
        }
        primitiveDrawer_->DrawTriangle(
            vertices, world, color, debugTextureHandle_, uv, GetBlendMode());
    }
}

void ColliderComponent::Render() {
    if (!debugDrawEnabled_ || primitiveDrawer_ == nullptr ||
        debugTextureHandle_ < 0 || GetOwner() == nullptr) {
        return;
    }
    const Vector4 color = isColliding_
        ? Vector4{ 1.0f, 0.12f, 0.05f, 0.32f }
        : Vector4{ 0.1f, 1.0f, 0.25f, 0.22f };
    if (colliderType_ == ColliderType::Sphere) {
        const WorldSphere sphere = GetWorldSphere();
        const Matrix4x4 world = MakeAffineMatrix(
            { sphere.radius, sphere.radius, sphere.radius },
            { 0.0f, 0.0f, 0.0f },
            sphere.center);
        const UVTransform uv = {
            { 1.0f, 1.0f }, 0.0f, { 0.0f, 0.0f }
        };
        primitiveDrawer_->DrawSphere(
            world, color, debugTextureHandle_, uv, GetBlendMode());
        return;
    }
    DrawDebugBox(GetWorldBox(), color);
}
