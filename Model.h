#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "CommonTypes.h"

class DirectXCommon;
class DebugCamera;
class LightingManager;
class Material;
class TextureManager;

// MTLのnewmtl 1つ分を保持する。
// Kd・Ks・Ns・d・map_KdをCPU側で保持し、各SubMeshのDraw時にGPUへ渡す。
struct ModelMaterial {
    std::string name;
    Color4 diffuseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    Color4 specularColor = { 0.0f, 0.0f, 0.0f, 1.0f };
    float shininess = 0.0f;
    float metallic = 0.0f;
    float roughness = 0.5f;
    bool hasExplicitRoughness = false;
    float opacity = 1.0f;
    std::string texturePath;
    int textureHandle = -1;
    std::string normalTexturePath;
    int normalTextureHandle = -1;
};

// OBJのusemtlが同じ連続したインデックス範囲。
// マテリアルが切り替わるたびに別のDrawIndexedInstancedを発行する。
struct ModelSubMesh {
    uint32_t indexStart = 0;
    uint32_t indexCount = 0;
    uint32_t materialIndex = 0;
};

// 1つのOBJモデルと、その描画に必要なGPUリソースを所有するクラス。
// OBJの読み込み結果を頂点・インデックスバッファへ変換し、Drawで描画する。
class Model {
public:
    Model() = default;
    // DEFAULTヒープ上の静的GPUリソースはComPtrが自動解放する。
    ~Model() = default;

    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    // Graphics::CreateModelから呼ばれる初期化処理。
    // OBJファイルが開けない、または有効な面がない場合はfalseを返す。
    bool Initialize(
        DirectXCommon* dxCommon,
        DebugCamera* debugCamera,
        LightingManager* lightingManager,
        TextureManager* textureManager,
        ID3D12RootSignature* rootSignature,
        ID3D12PipelineState* pipelineState,
        ID3D12RootSignature* shadowRootSignature,
        ID3D12PipelineState* shadowPipelineState,
        const std::string& objFilePath);

    // OBJをCPU側の頂点・インデックス配列へ変換する。
    // GPUリソースを作らず、読み込み結果だけ利用したい場合にも使用できる。
    bool LoadObjFile(
        const std::string& filePath,
        std::vector<TextureVertexData>& vertices,
        std::vector<uint32_t>& indices,
        std::string& materialTexturePath);

    // 座標・色・テクスチャ・UV変換を指定してモデルを描画する。
    void Draw(
        const Matrix4x4& worldMatrix,
        const Vector4& color,
        int textureHandle,
        const UVTransform& uvTransform,
        bool enableLighting = true,
        const std::vector<int>& materialTextureHandles = {},
        const std::vector<int>& materialNormalTextureHandles = {},
        const std::vector<float>& materialMetallicValues = {},
        const std::vector<float>& materialRoughnessValues = {},
        const std::vector<uint8_t>& materialPbrOverrideEnabled = {},
        const std::vector<UVTransform>& materialUVTransforms = {},
        const std::vector<uint8_t>& materialUVTransformEnabled = {},
        const std::vector<std::shared_ptr<Material>>& shaderMaterials = {});
    void DrawShadow(
        const Matrix4x4& worldMatrix,
        const Matrix4x4& lightViewProjection);

    uint32_t GetVertexCount() const { return vertexCount_; }
    uint32_t GetIndexCount() const { return indexCount_; }
    uint32_t GetMaterialCount() const {
        return static_cast<uint32_t>(materials_.size());
    }
    uint32_t GetSubMeshCount() const {
        return static_cast<uint32_t>(subMeshes_.size());
    }
    const ModelMaterial* GetMaterial(uint32_t materialIndex) const {
        return materialIndex < materials_.size()
            ? &materials_[materialIndex]
            : nullptr;
    }
    bool IsMaterialUsed(uint32_t materialIndex) const {
        for (const ModelSubMesh& subMesh : subMeshes_) {
            if (subMesh.materialIndex == materialIndex) {
                return true;
            }
        }
        return false;
    }
    int FindMaterialIndex(const std::string& materialName) const {
        for (size_t index = 0; index < materials_.size(); ++index) {
            if (materials_[index].name == materialName) {
                return static_cast<int>(index);
            }
        }
        return -1;
    }
    const std::string& GetSourcePath() const { return sourcePath_; }
    bool IsSkySphere() const { return isSkySphere_; }
    void SetSkySphere(bool isSkySphere) { isSkySphere_ = isSkySphere; }

    // OBJが参照するMTLにmap_Kdがある場合、その画像パスを返す。
    // 読み込みとテクスチャハンドルの所有は呼び出し側が明示的に行う。
    const std::string& GetMaterialTexturePath() const { return materialTexturePath_; }
    const std::string& GetLastError() const { return lastError_; }

private:
    void CreateMeshResources(
        const std::vector<TextureVertexData>& vertices,
        const std::vector<uint32_t>& indices);
    DirectXCommon* dxCommon_ = nullptr;
    DebugCamera* debugCamera_ = nullptr;
    LightingManager* lightingManager_ = nullptr;
    TextureManager* textureManager_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> shadowRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowPipelineState_;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    uint32_t vertexCount_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
    uint32_t indexCount_ = 0;

    std::vector<ModelMaterial> materials_;
    std::vector<ModelSubMesh> subMeshes_;

    std::string sourcePath_;
    std::string materialTexturePath_;
    std::string lastError_;
    // Scene読み込み時に通常モデル用PSOと天球用PSOを選び直すために保存する。
    bool isSkySphere_ = false;
};
