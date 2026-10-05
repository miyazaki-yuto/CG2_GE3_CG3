#include "Editor.h"
#include "AssetManager.h"
#include "EditorInspectorUtilities.h"
#include "Graphics.h"
#include "Material.h"
#include "Model.h"
#include "ModelRendererComponent.h"
#include "ShaderManager.h"
#include <algorithm>
#include <array>
#include <memory>
#include <utility>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"

using namespace EditorInspectorUtilities;

void Editor::DrawModelRendererInspector(ModelRendererComponent& modelRenderer) {
    DrawColor(modelRenderer.GetColor());
    DrawUVTransform(modelRenderer.GetUVTransform());
    bool lighting = modelRenderer.IsLightingEnabled();
    if (ImGui::Checkbox("ライティング###Lighting", &lighting)) {
        modelRenderer.SetLightingEnabled(lighting);
    }
    if (Model* model = modelRenderer.GetModel()) {
        const std::string modelPath = assetManager_ != nullptr
            ? assetManager_->GetAssetPath(modelRenderer.GetModelGuid())
            : std::string{};
        ImGui::TextWrapped(
            "モデルGUID: %s", modelRenderer.GetModelGuid().c_str());
        ImGui::TextWrapped("モデル: %s", modelPath.c_str());
        ImGui::Text(
            "頂点: %u  インデックス: %u  ノード: %u  ボーン: %u",
            model->GetVertexCount(), model->GetIndexCount(),
            model->GetNodeCount(), model->GetBoneCount());
        if (model->GetAnimationCount() > 0) {
            ImGui::SeparatorText("アニメーション");
            const char* currentAnimationName =
                modelRenderer.GetCurrentAnimationName().empty()
                ? "(none)"
                : modelRenderer.GetCurrentAnimationName().c_str();
            if (ImGui::BeginCombo(
                    "アニメーションクリップ###AnimationClip", currentAnimationName)) {
                for (uint32_t animationIndex = 0;
                    animationIndex < model->GetAnimationCount();
                    ++animationIndex) {
                    const ModelAnimationClip* animation =
                        model->GetAnimation(animationIndex);
                    if (animation == nullptr) {
                        continue;
                    }
                    const bool selected = animationIndex ==
                        modelRenderer.GetCurrentAnimationIndex();
                    if (ImGui::Selectable(
                            animation->name.c_str(), selected)) {
                        modelRenderer.PlayAnimation(
                            animationIndex, true);
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            if (modelRenderer.IsAnimationPlaying()) {
                if (ImGui::Button("一時停止###PauseAnimation")) {
                    modelRenderer.PauseAnimation();
                }
            } else if (ImGui::Button("再生###PlayAnimation")) {
                modelRenderer.ResumeAnimation();
            }
            ImGui::SameLine();
            if (ImGui::Button("停止###StopAnimation")) {
                modelRenderer.StopAnimation();
            }
            bool looping = modelRenderer.IsAnimationLooping();
            if (ImGui::Checkbox("ループ###LoopAnimation", &looping)) {
                modelRenderer.SetAnimationLooping(looping);
            }
            float playbackSpeed =
                modelRenderer.GetAnimationPlaybackSpeed();
            if (ImGui::DragFloat(
                    "再生速度###PlaybackSpeed", &playbackSpeed, 0.05f, 0.0f, 10.0f)) {
                modelRenderer.SetAnimationPlaybackSpeed(playbackSpeed);
            }
            float animationTime = modelRenderer.GetAnimationTime();
            const float animationDuration =
                modelRenderer.GetCurrentAnimationDuration();
            if (ImGui::SliderFloat(
                    "アニメーション時間###AnimationTime",
                    &animationTime,
                    0.0f,
                    (std::max)(animationDuration, 0.0001f),
                    "%.3f s")) {
                modelRenderer.SetAnimationTime(animationTime);
            }
            ImGui::Text(
                "時間: %.3f / %.3f 秒",
                modelRenderer.GetAnimationTime(),
                animationDuration);
        }
    }
    if (ImGui::Button("モデルを選択...###SelectModel")) {
        const std::string path = OpenAssetFileDialog(AssetType::Model);
        if (!path.empty() && assetManager_ != nullptr) {
            std::string error;
            const AssetGuid guid = assetManager_->ImportModel(path, &error);
            const bool skySphere = modelRenderer.GetModel() != nullptr &&
                modelRenderer.GetModel()->IsSkySphere();
            std::shared_ptr<Model> model = guid.empty()
                ? nullptr
                : assetManager_->LoadModel(guid, skySphere, &error);
            if (model != nullptr) {
                modelRenderer.SetModel(std::move(model), guid);
                lastMessage_ = "モデルアセットを変更しました。";
            } else {
                lastMessage_ = error;
            }
        }
    }

    const std::string fallbackPath = assetManager_ != nullptr
        ? assetManager_->GetAssetPath(
            modelRenderer.GetFallbackTextureGuid())
        : std::string{};
    ImGui::TextWrapped(
        "代替テクスチャGUID: %s",
        modelRenderer.GetFallbackTextureGuid().c_str());
    ImGui::TextWrapped("代替テクスチャ: %s", fallbackPath.c_str());
    if (ImGui::Button("代替テクスチャを選択...###SelectFallbackTexture")) {
        const std::string path = OpenAssetFileDialog(AssetType::Texture);
        if (!path.empty() && assetManager_ != nullptr) {
            std::string error;
            const AssetGuid guid = assetManager_->ImportTexture(path, &error);
            const int handle = guid.empty()
                ? -1 : assetManager_->LoadTexture(guid, &error);
            if (handle >= 0) {
                modelRenderer.SetFallbackTextureAsset(handle, guid);
                lastMessage_ = "代替テクスチャアセットを変更しました。";
            } else {
                lastMessage_ = error;
            }
        }
    }

    if (Model* model = modelRenderer.GetModel()) {
        ImGui::SeparatorText("マテリアルテクスチャ");
        ImGui::TextWrapped(
            "インポートした各マテリアルのベースカラーテクスチャを上書きできます。");
        for (uint32_t materialIndex = 0;
            materialIndex < model->GetMaterialCount();
            ++materialIndex) {
            if (!model->IsMaterialUsed(materialIndex)) {
                continue;
            }

            const ModelMaterial* material = model->GetMaterial(materialIndex);
            if (material == nullptr) {
                continue;
            }

            ImGui::PushID(static_cast<int>(materialIndex));
            const std::string& overrideGuid =
                modelRenderer.GetMaterialTextureGuid(materialIndex);
            const std::string overridePath =
                assetManager_ != nullptr && !overrideGuid.empty()
                ? assetManager_->GetAssetPath(overrideGuid)
                : std::string{};

            ImGui::Text("スロット %u: %s", materialIndex, material->name.c_str());

            std::shared_ptr<Material> shaderMaterial =
                modelRenderer.GetShaderMaterial(materialIndex);
            const char* currentShader = shaderMaterial != nullptr
                ? shaderMaterial->GetShaderName().c_str()
                : "（標準エンジンマテリアル）";
            if (ImGui::BeginCombo("シェーダー###Shader", currentShader)) {
                if (ImGui::Selectable(
                        "（標準エンジンマテリアル）",
                        shaderMaterial == nullptr)) {
                    modelRenderer.ClearShaderMaterial(materialIndex);
                    shaderMaterial.reset();
                    lastMessage_ = "標準マテリアルに戻しました。";
                }
                if (graphics_ != nullptr &&
                    graphics_->GetShaderManager() != nullptr) {
                    for (const std::string& shaderName :
                        graphics_->GetShaderManager()->GetShaderNames()) {
                        const bool selected =
                            shaderMaterial != nullptr &&
                            shaderMaterial->GetShaderName() == shaderName;
                        if (ImGui::Selectable(
                                shaderName.c_str(), selected)) {
                            shaderMaterial =
                                graphics_->CreateMaterial(shaderName);
                            modelRenderer.SetShaderMaterial(
                                materialIndex, shaderMaterial);
                            lastMessage_ =
                                "マテリアルシェーダーを変更しました: " + shaderName;
                        }
                    }
                }
                ImGui::EndCombo();
            }

            if (shaderMaterial != nullptr) {
                ImGui::Indent();
                for (const ShaderParameterDefinition& parameter :
                    shaderMaterial->GetParameterDefinitions()) {
                    const ShaderParameterValue* currentValue =
                        shaderMaterial->GetParameter(parameter.name);
                    if (currentValue == nullptr) {
                        continue;
                    }
                    std::array<float, 4> values = currentValue->floats;
                    if (parameter.type == ShaderParameterType::Float &&
                        ImGui::DragFloat(
                            parameter.name.c_str(),
                            &values[0],
                            0.01f)) {
                        shaderMaterial->SetFloat(
                            parameter.name, values[0]);
                    } else if (
                        parameter.type == ShaderParameterType::Float2 &&
                        ImGui::DragFloat2(
                            parameter.name.c_str(),
                            values.data(),
                            0.01f)) {
                        shaderMaterial->SetFloat2(
                            parameter.name, values[0], values[1]);
                    } else if (
                        parameter.type == ShaderParameterType::Float3 &&
                        ImGui::DragFloat3(
                            parameter.name.c_str(),
                            values.data(),
                            0.01f)) {
                        shaderMaterial->SetFloat3(
                            parameter.name,
                            values[0],
                            values[1],
                            values[2]);
                    } else if (
                        parameter.type == ShaderParameterType::Float4 &&
                        ImGui::ColorEdit4(
                            parameter.name.c_str(),
                            values.data())) {
                        shaderMaterial->SetFloat4(
                            parameter.name,
                            values[0],
                            values[1],
                            values[2],
                            values[3]);
                    } else if (
                        parameter.type == ShaderParameterType::Int) {
                        int value = currentValue->integer;
                        if (ImGui::InputInt(
                                parameter.name.c_str(), &value)) {
                            shaderMaterial->SetInt(
                                parameter.name, value);
                        }
                    } else if (
                        parameter.type == ShaderParameterType::Bool) {
                        bool value = currentValue->integer != 0;
                        if (ImGui::Checkbox(
                                parameter.name.c_str(), &value)) {
                            shaderMaterial->SetBool(
                                parameter.name, value);
                        }
                    }
                }
                ImGui::Unindent();
            }
            ImGui::TextWrapped(
                "ベースカラーテクスチャ: %s",
                material->texturePath.empty()
                ? "(none - fallback is used)"
                : material->texturePath.c_str());
            ImGui::TextWrapped(
                "上書き: %s",
                overridePath.empty()
                ? "(Use MTL Texture)"
                : overridePath.c_str());

            if (ImGui::Button("テクスチャを選択...###SelectMaterialTexture")) {
                const std::string path =
                    OpenAssetFileDialog(AssetType::Texture);
                if (!path.empty() && assetManager_ != nullptr) {
                    std::string error;
                    const AssetGuid guid =
                        assetManager_->ImportTexture(path, &error);
                    const int handle = guid.empty()
                        ? -1 : assetManager_->LoadTexture(guid, &error);
                    if (handle >= 0) {
                        modelRenderer.SetMaterialTextureAsset(
                            materialIndex, handle, guid);
                        lastMessage_ = "マテリアルテクスチャを変更しました。";
                    } else {
                        lastMessage_ = error;
                    }
                }
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(overrideGuid.empty());
            if (ImGui::Button("MTLテクスチャを使用###UseMtlTexture")) {
                modelRenderer.ClearMaterialTextureAsset(materialIndex);
                lastMessage_ = "マテリアルテクスチャの上書きを解除しました。";
            }
            ImGui::EndDisabled();

            const std::string& normalOverrideGuid =
                modelRenderer.GetMaterialNormalTextureGuid(materialIndex);
            const std::string normalOverridePath =
                assetManager_ != nullptr && !normalOverrideGuid.empty()
                ? assetManager_->GetAssetPath(normalOverrideGuid)
                : std::string{};
            ImGui::TextWrapped(
                "法線マップ: %s",
                material->normalTexturePath.empty()
                ? "(none)"
                : material->normalTexturePath.c_str());
            ImGui::TextWrapped(
                "法線マップ上書き: %s",
                normalOverridePath.empty()
                ? "(Use MTL Normal Map)"
                : normalOverridePath.c_str());
            if (ImGui::Button("法線マップを選択...###SelectNormalMap")) {
                const std::string path =
                    OpenAssetFileDialog(AssetType::Texture);
                if (!path.empty() && assetManager_ != nullptr) {
                    std::string error;
                    const AssetGuid guid =
                        assetManager_->ImportTexture(path, &error);
                    const int handle = guid.empty()
                        ? -1
                        : assetManager_->LoadLinearTexture(guid, &error);
                    if (handle >= 0) {
                        modelRenderer.SetMaterialNormalTextureAsset(
                            materialIndex, handle, guid);
                        lastMessage_ = "マテリアルの法線マップを変更しました。";
                    } else {
                        lastMessage_ = error;
                    }
                }
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(normalOverrideGuid.empty());
            if (ImGui::Button("MTL法線マップを使用###UseMtlNormalMap")) {
                modelRenderer.ClearMaterialNormalTextureAsset(
                    materialIndex);
                lastMessage_ = "法線マップの上書きを解除しました。";
            }
            ImGui::EndDisabled();

            ImGui::Text(
                "PBR: メタリック %.3f／ラフネス %.3f",
                material->metallic,
                material->roughness);
            ImGui::TextWrapped(
                "メタリック・ラフネスマップ: %s",
                material->metallicRoughnessTexturePath.empty()
                ? "(none)"
                : material->metallicRoughnessTexturePath.c_str());
            bool overridePbr =
                modelRenderer.IsMaterialPbrOverridden(materialIndex);
            if (ImGui::Checkbox(
                    "メタリック／ラフネスを上書き###OverridePbr", &overridePbr)) {
                modelRenderer.SetMaterialPbrOverridden(
                    materialIndex, overridePbr);
                lastMessage_ = overridePbr
                    ? "マテリアルPBRの上書きを有効にしました。"
                    : "マテリアルPBR値をMTLの標準値へ戻しました。";
            }
            if (overridePbr) {
                float metallic =
                    modelRenderer.GetMaterialMetallic(materialIndex);
                if (ImGui::SliderFloat(
                        "メタリック###Metallic", &metallic, 0.0f, 1.0f)) {
                    modelRenderer.SetMaterialMetallic(
                        materialIndex, metallic);
                }
                float roughness =
                    modelRenderer.GetMaterialRoughness(materialIndex);
                if (ImGui::SliderFloat(
                        "ラフネス###Roughness", &roughness, 0.04f, 1.0f)) {
                    modelRenderer.SetMaterialRoughness(
                        materialIndex, roughness);
                }
            }

            // OFFでは上部のModel Renderer共通UVを使い、
            // ONにしたSlotだけ独立したScale／Rotation／Positionを表示する。
            bool overrideUV =
                modelRenderer.IsMaterialUVTransformOverridden(
                    materialIndex);
            if (ImGui::Checkbox("UVトランスフォームを上書き###OverrideUVTransform", &overrideUV)) {
                modelRenderer.SetMaterialUVTransformOverridden(
                    materialIndex, overrideUV);
                lastMessage_ = overrideUV
                    ? "マテリアルUVトランスフォームの上書きを有効にしました。"
                    : "モデルレンダラーのUVトランスフォームを使用します。";
            }
            if (overrideUV) {
                DrawUVTransform(
                    modelRenderer.GetMaterialUVTransform(materialIndex));
            } else {
                ImGui::TextDisabled(
                    "上のモデルレンダラーUVトランスフォームを使用します。");
            }
            ImGui::Separator();
            ImGui::PopID();
        }
    }
}
#endif
