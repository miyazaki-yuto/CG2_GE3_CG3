#include "Editor.h"
#include "LightComponent.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"

void Editor::DrawLightInspector(LightComponent& light) {
    int lightType = static_cast<int>(light.GetLightType());
    const char* types[] = { "平行光源", "点光源", "スポットライト" };
    if (ImGui::Combo("ライト種類###LightType", &lightType, types, 3)) {
        light.SetLightType(static_cast<LightComponent::LightType>(lightType));
    }
    bool lightEnabled = light.IsLightEnabled();
    if (ImGui::Checkbox("ライト有効###LightEnabled", &lightEnabled)) {
        light.SetLightEnabled(lightEnabled);
    }
    ImGui::ColorEdit4("ライト色###LightColor", &light.GetColor().r);
    float intensity = light.GetIntensity();
    if (ImGui::DragFloat(
        "強度###Intensity", &intensity, 0.05f, 0.0f, 1000.0f)) {
        light.SetIntensity(intensity);
    }
    if (light.GetLightType() != LightComponent::LightType::Directional) {
        float radius = light.GetRadius();
        if (ImGui::DragFloat(
            "半径###Radius", &radius, 0.05f, 0.001f, 1000.0f)) {
            light.SetRadius(radius);
        }
        float decay = light.GetDecay();
        if (ImGui::DragFloat(
            "減衰###Decay", &decay, 0.05f, 0.001f, 16.0f)) {
            light.SetDecay(decay);
        }
    }
    if (light.GetLightType() == LightComponent::LightType::Spot) {
        constexpr float kRadiansToDegrees = 57.29577951308232f;
        constexpr float kDegreesToRadians = 0.017453292519943295f;
        float innerAngle = light.GetInnerAngle() * kRadiansToDegrees;
        float outerAngle = light.GetOuterAngle() * kRadiansToDegrees;
        if (ImGui::DragFloat(
            "内側角度###InnerAngle", &innerAngle, 0.25f, 0.1f, 89.0f)) {
            light.SetInnerAngle(innerAngle * kDegreesToRadians);
        }
        if (ImGui::DragFloat(
            "外側角度###OuterAngle", &outerAngle, 0.25f, 0.1f, 89.0f)) {
            light.SetOuterAngle(outerAngle * kDegreesToRadians);
        }
        ImGui::TextDisabled("方向はGameObjectの回転（+Z）に従います。");
    }
    ImGui::Text("スロット: %d", light.GetLightHandle());
    light.ApplyLight();
}
#endif
