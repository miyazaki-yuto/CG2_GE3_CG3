#pragma once

#include "CommonTypes.h"
#include "RendererComponent.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

class Model;
class Material;

// GameObjectのTransformを使用して1つのOBJ Modelを描画するComponent。
// Modelの所有権も持つため、GameObject破棄時にGPUリソースも安全に解放される。
class ModelRendererComponent final : public RendererComponent {
public:
    ModelRendererComponent(
        std::shared_ptr<Model> model,
        int fallbackTextureHandle,
        std::string modelGuid = {},
        std::string fallbackTextureGuid = {});
    ~ModelRendererComponent() override;

    ModelRendererComponent(const ModelRendererComponent&) = delete;
    ModelRendererComponent& operator=(const ModelRendererComponent&) = delete;

    void Render() override;
    void RenderShadow(const Matrix4x4& lightViewProjection) override;

    Model* GetModel() const { return model_.get(); }
    const std::shared_ptr<Model>& GetSharedModel() const { return model_; }
    const std::string& GetModelGuid() const { return modelGuid_; }
    void SetModel(std::shared_ptr<Model> model, std::string modelGuid = {});

    int GetFallbackTextureHandle() const { return fallbackTextureHandle_; }
    const std::string& GetFallbackTextureGuid() const {
        return fallbackTextureGuid_;
    }
    void SetFallbackTextureHandle(int textureHandle) {
        fallbackTextureHandle_ = textureHandle;
        fallbackTextureGuid_.clear();
    }
    void SetFallbackTextureAsset(
        int textureHandle,
        std::string textureGuid) {
        fallbackTextureHandle_ = textureHandle;
        fallbackTextureGuid_ = std::move(textureGuid);
    }

    int GetMaterialTextureHandle(uint32_t materialIndex) const;
    const std::string& GetMaterialTextureGuid(uint32_t materialIndex) const;
    void SetMaterialTextureAsset(
        uint32_t materialIndex,
        int textureHandle,
        std::string textureGuid);
    void ClearMaterialTextureAsset(uint32_t materialIndex);

    int GetMaterialNormalTextureHandle(uint32_t materialIndex) const;
    const std::string& GetMaterialNormalTextureGuid(
        uint32_t materialIndex) const;
    void SetMaterialNormalTextureAsset(
        uint32_t materialIndex,
        int textureHandle,
        std::string textureGuid);
    void ClearMaterialNormalTextureAsset(uint32_t materialIndex);

    bool IsMaterialPbrOverridden(uint32_t materialIndex) const;
    float GetMaterialMetallic(uint32_t materialIndex) const;
    float GetMaterialRoughness(uint32_t materialIndex) const;
    void SetMaterialPbrOverridden(uint32_t materialIndex, bool overridden);
    void SetMaterialMetallic(uint32_t materialIndex, float metallic);
    void SetMaterialRoughness(uint32_t materialIndex, float roughness);

    bool IsMaterialUVTransformOverridden(uint32_t materialIndex) const;
    UVTransform& GetMaterialUVTransform(uint32_t materialIndex);
    const UVTransform& GetMaterialUVTransform(uint32_t materialIndex) const;
    void SetMaterialUVTransform(
        uint32_t materialIndex,
        const UVTransform& uvTransform);
    void SetMaterialUVTransformOverridden(
        uint32_t materialIndex,
        bool overridden);

    std::shared_ptr<Material> GetShaderMaterial(
        uint32_t materialIndex) const;
    void SetShaderMaterial(
        uint32_t materialIndex,
        std::shared_ptr<Material> material);
    void ClearShaderMaterial(uint32_t materialIndex);

    Vector4& GetColor() { return color_; }
    const Vector4& GetColor() const { return color_; }
    void SetColor(const Vector4& color) { color_ = color; }

    UVTransform& GetUVTransform() { return uvTransform_; }
    const UVTransform& GetUVTransform() const { return uvTransform_; }
    void SetUVTransform(const UVTransform& uvTransform) {
        uvTransform_ = uvTransform;
    }

    bool IsLightingEnabled() const { return enableLighting_; }
    void SetLightingEnabled(bool enabled) { enableLighting_ = enabled; }

private:
    void ResetMaterialOverrides();

    // AssetManagerのキャッシュを複数GameObjectで共有する。
    std::shared_ptr<Model> model_;
    std::string modelGuid_;
    int fallbackTextureHandle_ = -1;
    std::string fallbackTextureGuid_;
    std::vector<int> materialTextureHandles_;
    std::vector<std::string> materialTextureGuids_;
    std::vector<int> materialNormalTextureHandles_;
    std::vector<std::string> materialNormalTextureGuids_;
    std::vector<float> materialMetallicValues_;
    std::vector<float> materialRoughnessValues_;
    std::vector<uint8_t> materialPbrOverrideEnabled_;
    std::vector<UVTransform> materialUVTransforms_;
    std::vector<uint8_t> materialUVTransformEnabled_;
    std::vector<std::shared_ptr<Material>> shaderMaterials_;
    Vector4 color_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    UVTransform uvTransform_ = {
        { 1.0f, 1.0f }, 0.0f, { 0.0f, 0.0f }
    };
    bool enableLighting_ = true;
};
