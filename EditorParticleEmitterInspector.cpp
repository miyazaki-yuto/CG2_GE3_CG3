#include "Editor.h"
#include "AssetManager.h"
#include "EditorInspectorUtilities.h"
#include "ParticleDrawer.h"
#include "ParticleEmitterComponent.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"

using namespace EditorInspectorUtilities;

void Editor::DrawParticleEmitterInspector(ParticleEmitterComponent& emitter) {
    ImGui::Text("生存数: %u / %u",
        emitter.GetAliveParticleCount(), emitter.GetMaxParticles());

    int maxParticles = static_cast<int>(emitter.GetMaxParticles());
    if (ImGui::DragInt(
        "最大パーティクル数###MaxParticles", &maxParticles, 10.0f, 1,
        static_cast<int>(ParticleDrawer::kMaxParticleCount))) {
        emitter.SetMaxParticles(static_cast<uint32_t>(maxParticles));
    }
    float emissionRate = emitter.GetEmissionRate();
    if (ImGui::DragFloat(
        "1秒あたりの生成数###EmissionPerSecond", &emissionRate, 1.0f, 0.0f, 10000.0f)) {
        emitter.SetEmissionRate(emissionRate);
    }
    int burstCount = static_cast<int>(emitter.GetBurstCount());
    if (ImGui::DragInt(
        "開始時バースト数###StartBurst", &burstCount, 1.0f, 0,
        static_cast<int>(ParticleDrawer::kMaxParticleCount))) {
        emitter.SetBurstCount(static_cast<uint32_t>(burstCount));
    }
    bool playOnAwake = emitter.IsPlayOnAwake();
    if (ImGui::Checkbox("開始時に再生###PlayOnAwake", &playOnAwake)) {
        emitter.SetPlayOnAwake(playOnAwake);
    }
    if (ImGui::Button(emitter.IsPlaying() ? "停止###ParticleStop" : "再生###ParticlePlay")) {
        if (emitter.IsPlaying()) {
            emitter.Stop(false);
        } else {
            emitter.Play();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("バースト生成###EmitBurst")) {
        emitter.Emit(emitter.GetBurstCount() > 0
            ? emitter.GetBurstCount() : 100u);
    }
    ImGui::SameLine();
    if (ImGui::Button("クリア###ClearParticles")) {
        emitter.Clear();
    }

    float lifetime[2] = {
        emitter.GetLifetimeMin(), emitter.GetLifetimeMax()
    };
    if (ImGui::DragFloat2("寿命 最小／最大###LifetimeMinMax", lifetime, 0.01f, 0.01f, 60.0f)) {
        emitter.SetLifetimeRange(lifetime[0], lifetime[1]);
    }
    float speed[2] = {
        emitter.GetSpeedMin(), emitter.GetSpeedMax()
    };
    if (ImGui::DragFloat2("速度 最小／最大###SpeedMinMax", speed, 0.05f, -100.0f, 100.0f)) {
        emitter.SetSpeedRange(speed[0], speed[1]);
    }
    Vector3 direction = emitter.GetDirection();
    if (ImGui::DragFloat3("方向###Direction", &direction.x, 0.01f)) {
        emitter.SetDirection(direction);
    }
    float spread = emitter.GetSpread();
    if (ImGui::DragFloat("拡散###Spread", &spread, 0.01f, 0.0f, 10.0f)) {
        emitter.SetSpread(spread);
    }
    Vector3 gravity = emitter.GetGravity();
    if (ImGui::DragFloat3("重力###Gravity", &gravity.x, 0.01f)) {
        emitter.SetGravity(gravity);
    }
    float sizes[2] = {
        emitter.GetStartSize(), emitter.GetEndSize()
    };
    if (ImGui::DragFloat2("サイズ 開始／終了###SizeStartEnd", sizes, 0.01f, 0.0f, 100.0f)) {
        emitter.SetSizeRange(sizes[0], sizes[1]);
    }
    Color4 startColor = emitter.GetStartColor();
    if (ImGui::ColorEdit4("開始色###StartColor", &startColor.r)) {
        emitter.SetStartColor(startColor);
    }
    Color4 endColor = emitter.GetEndColor();
    if (ImGui::ColorEdit4("終了色###EndColor", &endColor.r)) {
        emitter.SetEndColor(endColor);
    }

    const std::string texturePath = assetManager_ != nullptr
        ? assetManager_->GetAssetPath(emitter.GetTextureGuid())
        : std::string{};
    ImGui::TextWrapped("テクスチャ: %s", texturePath.c_str());
    if (ImGui::Button("パーティクルテクスチャを選択...###SelectParticleTexture")) {
        const std::string path = OpenAssetFileDialog(AssetType::Texture);
        if (!path.empty() && assetManager_ != nullptr) {
            std::string error;
            const AssetGuid guid = assetManager_->ImportTexture(path, &error);
            const int handle = guid.empty()
                ? -1 : assetManager_->LoadTexture(guid, &error);
            if (handle >= 0) {
                emitter.SetTextureAsset(handle, guid);
                lastMessage_ = "パーティクルテクスチャを変更しました。";
            } else {
                lastMessage_ = error;
            }
        }
    }
}
#endif
