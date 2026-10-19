#include "Editor.h"

#include "RigidbodyComponent.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"

#include <iterator>

void Editor::DrawRigidbodyInspector(RigidbodyComponent& rigidbody) {
    const char* bodyTypes[] = {
        "Static", "Dynamic", "Kinematic"
    };
    int bodyType = static_cast<int>(rigidbody.GetBodyType());
    if (ImGui::Combo(
            "物理タイプ###RigidbodyType",
            &bodyType,
            bodyTypes,
            static_cast<int>(std::size(bodyTypes)))) {
        rigidbody.SetBodyType(
            static_cast<RigidbodyComponent::BodyType>(bodyType));
    }

    float mass = rigidbody.GetMass();
    ImGui::BeginDisabled(
        rigidbody.GetBodyType() != RigidbodyComponent::BodyType::Dynamic);
    if (ImGui::DragFloat(
            "質量###RigidbodyMass", &mass, 0.05f, 0.001f, 100000.0f)) {
        rigidbody.SetMass(mass);
    }
    ImGui::EndDisabled();

    Vector3 velocity = rigidbody.GetVelocity();
    if (ImGui::DragFloat3(
            "速度###RigidbodyVelocity", &velocity.x, 0.02f)) {
        rigidbody.SetVelocity(velocity);
    }
    Vector3 acceleration = rigidbody.GetAcceleration();
    if (ImGui::DragFloat3(
            "加速度###RigidbodyAcceleration", &acceleration.x, 0.02f)) {
        rigidbody.SetAcceleration(acceleration);
    }

    bool useGravity = rigidbody.UsesGravity();
    ImGui::BeginDisabled(
        rigidbody.GetBodyType() != RigidbodyComponent::BodyType::Dynamic);
    if (ImGui::Checkbox(
            "重力を使用###RigidbodyGravity", &useGravity)) {
        rigidbody.SetUseGravity(useGravity);
    }
    ImGui::EndDisabled();

    float drag = rigidbody.GetDrag();
    if (ImGui::DragFloat(
            "空気抵抗###RigidbodyDrag", &drag, 0.01f, 0.0f, 1000.0f)) {
        rigidbody.SetDrag(drag);
    }
    float restitution = rigidbody.GetRestitution();
    if (ImGui::SliderFloat(
            "反発係数###RigidbodyRestitution", &restitution, 0.0f, 1.0f)) {
        rigidbody.SetRestitution(restitution);
    }
    float friction = rigidbody.GetFriction();
    if (ImGui::SliderFloat(
            "摩擦係数###RigidbodyFriction", &friction, 0.0f, 1.0f)) {
        rigidbody.SetFriction(friction);
    }

    ImGui::TextColored(
        rigidbody.IsGrounded()
            ? ImVec4(0.2f, 1.0f, 0.35f, 1.0f)
            : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
        rigidbody.IsGrounded() ? "接地中" : "非接地");
    ImGui::TextDisabled(
        "ColliderがTriggerの場合、通知だけ行い物理応答しません。");
}
#endif
