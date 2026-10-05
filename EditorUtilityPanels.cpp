#include "EditorUtilityPanels.h"

#include "AudioManager.h"
#include "CommonTypes.h"
#include "Graphics.h"
#include "LightingManager.h"

#include <iterator>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

void PerformancePanel::Draw() {
#ifdef USE_IMGUI
    ImGui::Begin("パフォーマンス###Performance");
    const float currentFps = ImGui::GetIO().Framerate;
    const float frameTimeMilliseconds =
        currentFps > 0.0f ? 1000.0f / currentFps : 0.0f;
    const ImVec4 fpsColor = currentFps >= 55.0f
        ? ImVec4(0.35f, 0.9f, 0.45f, 1.0f)
        : (currentFps >= 30.0f
            ? ImVec4(1.0f, 0.75f, 0.25f, 1.0f)
            : ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
    ImGui::TextColored(fpsColor, "FPS: %.1f", currentFps);
    ImGui::Text("フレーム時間: %.2f ms", frameTimeMilliseconds);
    ImGui::End();
#endif
}

void LightingPanel::Draw(
    Graphics* graphics,
    LightingManager* lightingManager) {
#ifdef USE_IMGUI
    ImGui::Begin("ライティング###Lighting");
    if (graphics == nullptr || lightingManager == nullptr) {
        ImGui::TextUnformatted("GraphicsまたはLightingManagerを利用できません。");
        ImGui::End();
        return;
    }

    ImGui::Text(
        "登録数: 平行光源 %u/%u、点光源 %u/%u、スポットライト %u/%u",
        lightingManager->GetDirectionalLightCount(), kMaxDirectionalLights,
        lightingManager->GetPointLightCount(), kMaxPointLights,
        lightingManager->GetSpotLightCount(), kMaxSpotLights);
    ImGui::TextWrapped(
        "リアルタイムシャドウ: 最初に有効化された平行光源（4カスケード）と、"
        "最初に有効化された点光源（キューブ6面）を使用します。");

    ImGui::SeparatorText("ライティングモデル");
    const char* lightingModes[] = {
        "ランバート", "ハーフランバート",
        "現在方式（ハーフランバート＋スペキュラー）",
        "PBR（メタリック／ラフネス）"
    };
    int lightingMode = static_cast<int>(lightingManager->GetLightingMode());
    if (ImGui::Combo(
            "方式###LightingMode", &lightingMode, lightingModes,
            static_cast<int>(std::size(lightingModes)))) {
        lightingManager->SetLightingMode(
            static_cast<LightingMode>(lightingMode));
    }
    switch (lightingManager->GetLightingMode()) {
    case LightingMode::Lambert:
        ImGui::TextWrapped(
            "標準的な拡散反射です。裏向きの面は暗くなります。");
        break;
    case LightingMode::HalfLambert:
        ImGui::TextWrapped(
            "スペキュラーハイライトを使用しない柔らかな拡散反射です。");
        break;
    case LightingMode::Current:
        ImGui::TextWrapped(
            "従来方式: ハーフランバート拡散反射、Blinn-Phongスペキュラー、"
            "バウンスライトを使用します。");
        break;
    case LightingMode::PBR:
    default:
        ImGui::TextWrapped(
            "Cook-Torrance PBR: GGX、Smith幾何減衰、Schlickフレネル、"
            "メタリックとラフネスを使用します。");
        break;
    }

    ImGui::SeparatorText("スペキュラー");
    ImGui::BeginDisabled(
        lightingManager->GetLightingMode() != LightingMode::Current);
    float specularStrength = lightingManager->GetSpecularStrength();
    if (ImGui::DragFloat(
            "スペキュラー強度###SpecularStrength", &specularStrength, 0.01f, 0.0f, 10.0f)) {
        lightingManager->SetSpecularStrength(specularStrength);
    }
    float specularShininess = lightingManager->GetSpecularShininess();
    if (ImGui::DragFloat(
            "スペキュラー光沢###SpecularShininess", &specularShininess,
            1.0f, 1.0f, 256.0f)) {
        lightingManager->SetSpecularShininess(specularShininess);
    }
    ImGui::TextUnformatted(
        "強度: 明るさ／光沢: ハイライトの鋭さ");
    ImGui::EndDisabled();

    ImGui::SeparatorText("環境光／IBL");
    bool environmentEnabled = lightingManager->IsEnvironmentEnabled();
    if (ImGui::Checkbox("環境光を使用###UseEnvironmentLight", &environmentEnabled)) {
        lightingManager->SetEnvironmentEnabled(environmentEnabled);
    }
    ImGui::BeginDisabled(!environmentEnabled);
    float environmentIntensity = lightingManager->GetEnvironmentIntensity();
    if (ImGui::DragFloat(
            "IBL強度###IBLIntensity", &environmentIntensity, 0.01f, 0.0f, 4.0f)) {
        lightingManager->SetEnvironmentIntensity(environmentIntensity);
    }
    constexpr float kRadiansToDegrees = 57.29577951308232f;
    constexpr float kDegreesToRadians = 0.017453292519943295f;
    float environmentRotationDegrees =
        lightingManager->GetEnvironmentRotation() * kRadiansToDegrees;
    if (ImGui::DragFloat(
            "環境光の回転###EnvironmentRotation", &environmentRotationDegrees,
            0.5f, -360.0f, 360.0f, "%.1f deg")) {
        lightingManager->SetEnvironmentRotation(
            environmentRotationDegrees * kDegreesToRadians);
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped(
        "スカイスフィアのテクスチャを環境光と反射に使用します。");

    ImGui::SeparatorText("HDR／トーンマッピング");
    const char* toneMappingModes[] = {
        "なし（クランプ）", "Reinhard", "ACES Filmic"
    };
    int toneMappingMode = static_cast<int>(graphics->GetToneMappingMode());
    if (ImGui::Combo(
            "トーンマッピング###ToneMapping", &toneMappingMode, toneMappingModes,
            static_cast<int>(std::size(toneMappingModes)))) {
        graphics->SetToneMappingMode(
            static_cast<ToneMappingMode>(toneMappingMode));
    }
    float exposure = graphics->GetExposure();
    if (ImGui::SliderFloat("露出###Exposure", &exposure, -5.0f, 5.0f, "%+.2f EV")) {
        graphics->SetExposure(exposure);
    }
    ImGui::TextWrapped(
        "この処理の前にSceneをRGBA16Fへ描画します。");

    ImGui::SeparatorText("ポストプロセス");
    const char* aaModes[] = { "なし", "FXAA", "TAA", "MAA" };
    int aaMode = static_cast<int>(graphics->GetAntiAliasingMode());
    if (ImGui::Combo(
            "アンチエイリアス###AntiAliasing", &aaMode, aaModes,
            static_cast<int>(std::size(aaModes)))) {
        graphics->SetAntiAliasingMode(static_cast<AntiAliasingMode>(aaMode));
    }
    bool ssaoEnabled = graphics->IsSsaoEnabled();
    if (ImGui::Checkbox("SSAO", &ssaoEnabled)) {
        graphics->SetSsaoEnabled(ssaoEnabled);
    }
    float ssaoStrength = graphics->GetSsaoStrength();
    if (ImGui::SliderFloat(
            "SSAO強度###SSAOStrength", &ssaoStrength, 0.0f, 2.0f)) {
        graphics->SetSsaoStrength(ssaoStrength);
    }
    bool bloomEnabled = graphics->IsBloomEnabled();
    if (ImGui::Checkbox("ブルーム###Bloom", &bloomEnabled)) {
        graphics->SetBloomEnabled(bloomEnabled);
    }
    float bloomIntensity = graphics->GetBloomIntensity();
    if (ImGui::SliderFloat(
            "ブルーム強度###BloomIntensity", &bloomIntensity, 0.0f, 2.0f)) {
        graphics->SetBloomIntensity(bloomIntensity);
    }
    float bloomThreshold = graphics->GetBloomThreshold();
    if (ImGui::SliderFloat(
            "ブルーム閾値###BloomThreshold", &bloomThreshold, 0.0f, 5.0f)) {
        graphics->SetBloomThreshold(bloomThreshold);
    }
    ImGui::End();
#else
    static_cast<void>(graphics);
    static_cast<void>(lightingManager);
#endif
}

void SoundPanel::Draw(
    AudioManager* audioManager,
    int bgmHandle,
    float* bgmVolume,
    bool isPlaying) {
#ifdef USE_IMGUI
    ImGui::Begin("サウンド###Sound Control");
    if (audioManager != nullptr && bgmHandle >= 0 && bgmVolume != nullptr) {
        ImGui::BeginDisabled(!isPlaying);
        if (ImGui::SliderFloat("BGM音量###BGMVolume", bgmVolume, 0.0f, 1.0f)) {
            audioManager->SetVolume(bgmHandle, *bgmVolume);
        }
        if (ImGui::Button("最初から再生###PlayFromStart")) {
            audioManager->Play(bgmHandle, true);
        }
        ImGui::SameLine();
        if (ImGui::Button("一時停止###Pause")) {
            audioManager->Pause(bgmHandle);
        }
        ImGui::SameLine();
        if (ImGui::Button("再開###Resume")) {
            audioManager->Resume(bgmHandle);
        }
        ImGui::SameLine();
        if (ImGui::Button("停止###Stop")) {
            audioManager->Stop(bgmHandle);
        }
        ImGui::EndDisabled();
        const char* soundState = audioManager->IsPaused(bgmHandle)
            ? "一時停止中"
            : (audioManager->IsPlaying(bgmHandle) ? "再生中" : "停止中");
        ImGui::Text("状態: %s", soundState);
    } else {
        ImGui::TextUnformatted("BGMを読み込めませんでした。");
    }
    ImGui::End();
#else
    static_cast<void>(audioManager);
    static_cast<void>(bgmHandle);
    static_cast<void>(bgmVolume);
    static_cast<void>(isPlaying);
#endif
}
