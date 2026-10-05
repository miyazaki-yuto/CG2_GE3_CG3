#include "ModelRendererComponent.h"

#include "GameObject.h"
#include "Material.h"
#include "Model.h"

#include <algorithm>
#include <cmath>
#include <utility>

ModelRendererComponent::ModelRendererComponent(
    std::shared_ptr<Model> model,
    int fallbackTextureHandle,
    std::string modelGuid,
    std::string fallbackTextureGuid)
    : model_(std::move(model)),
      modelGuid_(std::move(modelGuid)),
      fallbackTextureHandle_(fallbackTextureHandle),
      fallbackTextureGuid_(std::move(fallbackTextureGuid)) {
    SetRenderOrder(kOpaqueRenderOrder);
    ResetMaterialOverrides();
    ResetAnimationState();
}

ModelRendererComponent::~ModelRendererComponent() = default;

void ModelRendererComponent::SetModel(
    std::shared_ptr<Model> model,
    std::string modelGuid) {
    model_ = std::move(model);
    modelGuid_ = std::move(modelGuid);
    ResetMaterialOverrides();
    ResetAnimationState();
}

void ModelRendererComponent::ResetAnimationState() {
    animationIndex_ = 0;
    animationTimeSeconds_ = 0.0f;
    animationPlaying_ = model_ != nullptr && model_->GetAnimationCount() > 0;
    hasAnimationPose_ = false;
    animationPose_.nodeModelTransforms.clear();
    EvaluateCurrentAnimation();
}

void ModelRendererComponent::EvaluateCurrentAnimation() {
    hasAnimationPose_ = model_ != nullptr &&
        model_->EvaluateAnimation(
            animationIndex_, animationTimeSeconds_, animationPose_);
}

bool ModelRendererComponent::PlayAnimation(
    uint32_t animationIndex,
    bool restart) {
    if (model_ == nullptr || animationIndex >= model_->GetAnimationCount()) {
        return false;
    }
    if (restart || animationIndex_ != animationIndex) {
        animationTimeSeconds_ = 0.0f;
    }
    animationIndex_ = animationIndex;
    animationPlaying_ = true;
    EvaluateCurrentAnimation();
    return true;
}

bool ModelRendererComponent::PlayAnimation(
    const std::string& animationName,
    bool restart) {
    if (model_ == nullptr) {
        return false;
    }
    const int animationIndex = model_->FindAnimationIndex(animationName);
    return animationIndex >= 0 && PlayAnimation(
        static_cast<uint32_t>(animationIndex), restart);
}

void ModelRendererComponent::ResumeAnimation() {
    if (model_ != nullptr && animationIndex_ < model_->GetAnimationCount()) {
        animationPlaying_ = true;
    }
}

void ModelRendererComponent::StopAnimation() {
    animationPlaying_ = false;
    animationTimeSeconds_ = 0.0f;
    EvaluateCurrentAnimation();
}

void ModelRendererComponent::SetAnimationTime(float timeSeconds) {
    animationTimeSeconds_ = (std::clamp)(
        timeSeconds, 0.0f, GetCurrentAnimationDuration());
    EvaluateCurrentAnimation();
}

const std::string& ModelRendererComponent::GetCurrentAnimationName() const {
    static const std::string emptyName;
    if (model_ == nullptr) {
        return emptyName;
    }
    const ModelAnimationClip* animation = model_->GetAnimation(animationIndex_);
    return animation != nullptr ? animation->name : emptyName;
}

float ModelRendererComponent::GetCurrentAnimationDuration() const {
    if (model_ == nullptr) {
        return 0.0f;
    }
    const ModelAnimationClip* animation = model_->GetAnimation(animationIndex_);
    return animation != nullptr ? animation->durationSeconds : 0.0f;
}

void ModelRendererComponent::SetAnimationPlaybackSpeed(float speed) {
    animationPlaybackSpeed_ = (std::clamp)(speed, 0.0f, 100.0f);
}

void ModelRendererComponent::Update(float deltaTime) {
    if (!animationPlaying_ || model_ == nullptr ||
        animationIndex_ >= model_->GetAnimationCount()) {
        return;
    }
    const float duration = GetCurrentAnimationDuration();
    animationTimeSeconds_ +=
        (std::max)(deltaTime, 0.0f) * animationPlaybackSpeed_;
    if (duration > 0.000001f && animationTimeSeconds_ > duration) {
        if (animationLooping_) {
            animationTimeSeconds_ = std::fmod(animationTimeSeconds_, duration);
        } else {
            animationTimeSeconds_ = duration;
            animationPlaying_ = false;
        }
    }
    EvaluateCurrentAnimation();
}

void ModelRendererComponent::ResetMaterialOverrides() {
    const size_t materialCount = model_ != nullptr
        ? static_cast<size_t>(model_->GetMaterialCount())
        : 0;

    // Modelを交換するとMaterial数も変わる可能性があるため、
    // 全ての個別設定を新しいMaterial Slot数へ揃え直す。
    // -1と無効状態は「読み込んだModel本来の設定を使う」という意味になる。
    materialTextureHandles_.assign(materialCount, -1);
    materialTextureGuids_.assign(materialCount, {});
    materialNormalTextureHandles_.assign(materialCount, -1);
    materialNormalTextureGuids_.assign(materialCount, {});
    materialMetallicValues_.assign(materialCount, 0.0f);
    materialRoughnessValues_.assign(materialCount, 0.5f);
    materialPbrOverrideEnabled_.assign(materialCount, 0);
    materialUVTransforms_.assign(materialCount, {
        { 1.0f, 1.0f }, 0.0f, { 0.0f, 0.0f }
    });
    materialUVTransformEnabled_.assign(materialCount, 0);
    shaderMaterials_.assign(materialCount, {});
}

int ModelRendererComponent::GetMaterialTextureHandle(
    uint32_t materialIndex) const {
    return materialIndex < materialTextureHandles_.size()
        ? materialTextureHandles_[materialIndex]
        : -1;
}

const std::string& ModelRendererComponent::GetMaterialTextureGuid(
    uint32_t materialIndex) const {
    static const std::string emptyGuid;
    return materialIndex < materialTextureGuids_.size()
        ? materialTextureGuids_[materialIndex]
        : emptyGuid;
}

void ModelRendererComponent::SetMaterialTextureAsset(
    uint32_t materialIndex,
    int textureHandle,
    std::string textureGuid) {
    if (materialIndex >= materialTextureHandles_.size() || textureHandle < 0) {
        return;
    }
    materialTextureHandles_[materialIndex] = textureHandle;
    materialTextureGuids_[materialIndex] = std::move(textureGuid);
}

void ModelRendererComponent::ClearMaterialTextureAsset(
    uint32_t materialIndex) {
    if (materialIndex >= materialTextureHandles_.size()) {
        return;
    }
    // HandleとGUIDを両方消すことで、描画時はMTLのmap_Kdが再び選ばれる。
    materialTextureHandles_[materialIndex] = -1;
    materialTextureGuids_[materialIndex].clear();
}

int ModelRendererComponent::GetMaterialNormalTextureHandle(
    uint32_t materialIndex) const {
    return materialIndex < materialNormalTextureHandles_.size()
        ? materialNormalTextureHandles_[materialIndex]
        : -1;
}

const std::string& ModelRendererComponent::GetMaterialNormalTextureGuid(
    uint32_t materialIndex) const {
    static const std::string emptyGuid;
    return materialIndex < materialNormalTextureGuids_.size()
        ? materialNormalTextureGuids_[materialIndex]
        : emptyGuid;
}

void ModelRendererComponent::SetMaterialNormalTextureAsset(
    uint32_t materialIndex,
    int textureHandle,
    std::string textureGuid) {
    if (materialIndex >= materialNormalTextureHandles_.size() ||
        textureHandle < 0) {
        return;
    }
    materialNormalTextureHandles_[materialIndex] = textureHandle;
    materialNormalTextureGuids_[materialIndex] = std::move(textureGuid);
}

void ModelRendererComponent::ClearMaterialNormalTextureAsset(
    uint32_t materialIndex) {
    if (materialIndex >= materialNormalTextureHandles_.size()) {
        return;
    }
    // 上書きを外した後は、MTLのmap_Bump／bump／normを再び使用する。
    materialNormalTextureHandles_[materialIndex] = -1;
    materialNormalTextureGuids_[materialIndex].clear();
}

bool ModelRendererComponent::IsMaterialPbrOverridden(
    uint32_t materialIndex) const {
    return materialIndex < materialPbrOverrideEnabled_.size() &&
        materialPbrOverrideEnabled_[materialIndex] != 0;
}

float ModelRendererComponent::GetMaterialMetallic(
    uint32_t materialIndex) const {
    return materialIndex < materialMetallicValues_.size()
        ? materialMetallicValues_[materialIndex]
        : 0.0f;
}

float ModelRendererComponent::GetMaterialRoughness(
    uint32_t materialIndex) const {
    return materialIndex < materialRoughnessValues_.size()
        ? materialRoughnessValues_[materialIndex]
        : 0.5f;
}

void ModelRendererComponent::SetMaterialPbrOverridden(
    uint32_t materialIndex,
    bool overridden) {
    if (materialIndex >= materialPbrOverrideEnabled_.size()) {
        return;
    }
    if (overridden && materialPbrOverrideEnabled_[materialIndex] == 0 &&
        model_ != nullptr) {
        if (const ModelMaterial* material =
            model_->GetMaterial(materialIndex)) {
            materialMetallicValues_[materialIndex] = material->metallic;
            materialRoughnessValues_[materialIndex] = material->roughness;
        }
    }
    materialPbrOverrideEnabled_[materialIndex] = overridden ? 1 : 0;
}

void ModelRendererComponent::SetMaterialMetallic(
    uint32_t materialIndex,
    float metallic) {
    if (materialIndex < materialMetallicValues_.size()) {
        materialMetallicValues_[materialIndex] =
            (std::clamp)(metallic, 0.0f, 1.0f);
    }
}

void ModelRendererComponent::SetMaterialRoughness(
    uint32_t materialIndex,
    float roughness) {
    if (materialIndex < materialRoughnessValues_.size()) {
        materialRoughnessValues_[materialIndex] =
            (std::clamp)(roughness, 0.04f, 1.0f);
    }
}

bool ModelRendererComponent::IsMaterialUVTransformOverridden(
    uint32_t materialIndex) const {
    return materialIndex < materialUVTransformEnabled_.size() &&
        materialUVTransformEnabled_[materialIndex] != 0;
}

UVTransform& ModelRendererComponent::GetMaterialUVTransform(
    uint32_t materialIndex) {
    return materialUVTransforms_[materialIndex];
}

const UVTransform& ModelRendererComponent::GetMaterialUVTransform(
    uint32_t materialIndex) const {
    return materialUVTransforms_[materialIndex];
}

void ModelRendererComponent::SetMaterialUVTransform(
    uint32_t materialIndex,
    const UVTransform& uvTransform) {
    if (materialIndex >= materialUVTransforms_.size()) {
        return;
    }
    materialUVTransforms_[materialIndex] = uvTransform;
}

void ModelRendererComponent::SetMaterialUVTransformOverridden(
    uint32_t materialIndex,
    bool overridden) {
    if (materialIndex >= materialUVTransformEnabled_.size()) {
        return;
    }

    // 初めて個別UVをONにした瞬間は、全体UVの現在値から始める。
    // Identityへ急に戻して見た目が跳ねることを避け、そのまま微調整できるようにする。
    if (overridden && materialUVTransformEnabled_[materialIndex] == 0) {
        materialUVTransforms_[materialIndex] = uvTransform_;
    }
    materialUVTransformEnabled_[materialIndex] = overridden ? 1 : 0;
}

std::shared_ptr<Material> ModelRendererComponent::GetShaderMaterial(
    uint32_t materialIndex) const {
    return materialIndex < shaderMaterials_.size()
        ? shaderMaterials_[materialIndex]
        : std::shared_ptr<Material>{};
}

void ModelRendererComponent::SetShaderMaterial(
    uint32_t materialIndex,
    std::shared_ptr<Material> material) {
    if (materialIndex < shaderMaterials_.size()) {
        shaderMaterials_[materialIndex] = std::move(material);
    }
}

void ModelRendererComponent::ClearShaderMaterial(
    uint32_t materialIndex) {
    if (materialIndex < shaderMaterials_.size()) {
        shaderMaterials_[materialIndex].reset();
    }
}

void ModelRendererComponent::Render() {
    GameObject* owner = GetOwner();
    if (owner == nullptr || model_ == nullptr || fallbackTextureHandle_ < 0) {
        return;
    }

    // 描画座標はComponent内に重複保持せず、OwnerのTransformを唯一の情報源にする。
    // Materialごとの配列もModelへ渡し、各SubMeshを描く直前に対応Slotの値を選ばせる。
    model_->Draw(
        owner->GetTransform().GetWorldMatrix(),
        color_,
        fallbackTextureHandle_,
        uvTransform_,
        enableLighting_,
        materialTextureHandles_,
        materialNormalTextureHandles_,
        materialMetallicValues_,
        materialRoughnessValues_,
        materialPbrOverrideEnabled_,
        materialUVTransforms_,
        materialUVTransformEnabled_,
        shaderMaterials_,
        GetBlendMode(),
        hasAnimationPose_ ? &animationPose_ : nullptr);
}

void ModelRendererComponent::RenderOutline() {
    GameObject* owner = GetOwner();
    if (owner == nullptr || model_ == nullptr || model_->IsSkySphere()) {
        return;
    }
    model_->DrawOutline(
        owner->GetTransform().GetWorldMatrix(),
        hasAnimationPose_ ? &animationPose_ : nullptr);
}

void ModelRendererComponent::RenderShadow(
    const Matrix4x4& lightViewProjection) {
    GameObject* owner = GetOwner();
    if (owner == nullptr || model_ == nullptr || model_->IsSkySphere()) {
        return;
    }
    model_->DrawShadow(
        owner->GetTransform().GetWorldMatrix(),
        lightViewProjection,
        hasAnimationPose_ ? &animationPose_ : nullptr);
}
