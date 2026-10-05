#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "BlendMode.h"
#include "CommonTypes.h"

class DirectXCommon;
class DebugCamera;
class LightingManager;
class Material;
class TextureManager;

// Assimpから取得したマテリアル1つ分を保持する。
// 色・光沢・PBR値・テクスチャをCPU側で保持し、各SubMeshのDraw時にGPUへ渡す。
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
    // glTF 2.0では1枚の画像のGにRoughness、BにMetallicが入る。
    std::string metallicRoughnessTexturePath;
    int metallicRoughnessTextureHandle = -1;
};

// Assimpの1メッシュ分に対応する連続したインデックス範囲。
// メッシュごとに割り当てられたマテリアルでDrawIndexedInstancedを発行する。
struct ModelSubMesh {
    struct BoneOffset {
        uint32_t boneIndex = 0;
        Matrix4x4 offsetMatrix = MakeIdentity4x4();
    };
    uint32_t indexStart = 0;
    uint32_t indexCount = 0;
    uint32_t materialIndex = 0;
    uint32_t nodeIndex = 0;
    Matrix4x4 nodeTransform = MakeIdentity4x4();
    std::vector<BoneOffset> boneOffsets;
};

// glTF／GLB内のNodeを親子関係ごと保持する。
// localTransformは親から見た行列、modelTransformはルートから累積した行列。
struct ModelNode {
    std::string name;
    int32_t parentIndex = -1;
    Matrix4x4 localTransform = MakeIdentity4x4();
    Matrix4x4 modelTransform = MakeIdentity4x4();
    Vector3 bindScale = { 1.0f, 1.0f, 1.0f };
    Vector3 bindTranslation = { 0.0f, 0.0f, 0.0f };
    struct Quaternion {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;
    } bindRotation;
    std::vector<uint32_t> childIndices;
    std::vector<uint32_t> subMeshIndices;
};

struct ModelBone {
    std::string name;
    uint32_t nodeIndex = 0;
};

struct ModelVectorKey {
    float timeSeconds = 0.0f;
    Vector3 value{};
};

struct ModelQuaternionKey {
    float timeSeconds = 0.0f;
    ModelNode::Quaternion value{};
};

struct ModelAnimationChannel {
    uint32_t nodeIndex = 0;
    std::vector<ModelVectorKey> positions;
    std::vector<ModelQuaternionKey> rotations;
    std::vector<ModelVectorKey> scales;
};

struct ModelAnimationClip {
    std::string name;
    float durationSeconds = 0.0f;
    std::vector<ModelAnimationChannel> channels;
};

// Model本体は複数GameObjectで共有し、このPoseだけをインスタンスごとに持つ。
struct ModelPose {
    std::vector<Matrix4x4> nodeModelTransforms;
};

// Assimpで読み込んだOBJ／glTF／GLBと、描画に必要なGPUリソースを所有するクラス。
// 読み込み結果を頂点・インデックスバッファへ変換し、Drawで描画する。
class Model {
public:
    Model() = default;
    // DEFAULTヒープ上の静的GPUリソースはComPtrが自動解放する。
    ~Model() = default;

    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    // Graphics::CreateModelから呼ばれる初期化処理。
    // OBJ／glTF／GLBをAssimpで読み込めない、または有効な面がない場合はfalseを返す。
    bool Initialize(
        DirectXCommon* dxCommon,
        DebugCamera* debugCamera,
        LightingManager* lightingManager,
        TextureManager* textureManager,
        ID3D12RootSignature* rootSignature,
        const std::array<
            ID3D12PipelineState*, kBlendModeCount>& pipelineStates,
        ID3D12PipelineState* outlinePipelineState,
        ID3D12RootSignature* shadowRootSignature,
        ID3D12PipelineState* shadowPipelineState,
        const std::string& modelFilePath);

    // 拡張子をAssimpに判定させ、OBJ／glTF／GLBを共通データへ変換する。
    bool LoadModelFile(
        const std::string& filePath,
        std::vector<TextureVertexData>& vertices,
        std::vector<uint32_t>& indices,
        std::string& materialTexturePath);

    // 旧APIとの互換用。内部ではLoadModelFileと同じAssimp経路を使用する。
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
        const std::vector<std::shared_ptr<Material>>& shaderMaterials = {},
        BlendMode blendMode = BlendMode::Normal,
        const ModelPose* pose = nullptr);
    void DrawShadow(
        const Matrix4x4& worldMatrix,
        const Matrix4x4& lightViewProjection,
        const ModelPose* pose = nullptr);
    void DrawOutline(
        const Matrix4x4& worldMatrix,
        const ModelPose* pose = nullptr);

    bool EvaluateAnimation(
        uint32_t animationIndex,
        float timeSeconds,
        ModelPose& pose) const;

    uint32_t GetVertexCount() const { return vertexCount_; }
    uint32_t GetIndexCount() const { return indexCount_; }
    uint32_t GetMaterialCount() const {
        return static_cast<uint32_t>(materials_.size());
    }
    uint32_t GetSubMeshCount() const {
        return static_cast<uint32_t>(subMeshes_.size());
    }
    uint32_t GetNodeCount() const {
        return static_cast<uint32_t>(nodes_.size());
    }
    uint32_t GetBoneCount() const {
        return static_cast<uint32_t>(bones_.size());
    }
    uint32_t GetAnimationCount() const {
        return static_cast<uint32_t>(animations_.size());
    }
    const ModelAnimationClip* GetAnimation(uint32_t animationIndex) const {
        return animationIndex < animations_.size()
            ? &animations_[animationIndex]
            : nullptr;
    }
    int FindAnimationIndex(const std::string& animationName) const {
        for (size_t index = 0; index < animations_.size(); ++index) {
            if (animations_[index].name == animationName) {
                return static_cast<int>(index);
            }
        }
        return -1;
    }
    const ModelNode* GetNode(uint32_t nodeIndex) const {
        return nodeIndex < nodes_.size() ? &nodes_[nodeIndex] : nullptr;
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
    bool HasBounds() const { return hasBounds_; }
    const Vector3& GetBoundsMin() const { return boundsMin_; }
    const Vector3& GetBoundsMax() const { return boundsMax_; }

    // Assimpが取得した最初のディフューズテクスチャの画像パスを返す。
    // 読み込みとテクスチャハンドルの所有は呼び出し側が明示的に行う。
    const std::string& GetMaterialTexturePath() const { return materialTexturePath_; }
    const std::string& GetLastError() const { return lastError_; }

private:
    struct EmbeddedTextureData {
        std::vector<uint8_t> bytes;
        uint32_t width = 0;
        uint32_t height = 0;
        bool isCompressed = true;
    };

    int LoadMaterialTexture(
        const std::string& texturePath,
        bool useSrgb);
    void CreateMeshResources(
        const std::vector<TextureVertexData>& vertices,
        const std::vector<uint32_t>& indices);
    DirectXCommon* dxCommon_ = nullptr;
    DebugCamera* debugCamera_ = nullptr;
    LightingManager* lightingManager_ = nullptr;
    TextureManager* textureManager_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kBlendModeCount>
        pipelineStates_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> outlinePipelineState_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> shadowRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowPipelineState_;
    Vector3 boundsMin_{};
    Vector3 boundsMax_{};
    bool hasBounds_ = false;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    uint32_t vertexCount_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
    uint32_t indexCount_ = 0;

    std::vector<ModelMaterial> materials_;
    std::vector<ModelSubMesh> subMeshes_;
    std::vector<ModelNode> nodes_;
    std::vector<ModelBone> bones_;
    std::vector<ModelAnimationClip> animations_;
    std::unordered_map<std::string, EmbeddedTextureData> embeddedTextures_;

    std::string sourcePath_;
    std::string materialTexturePath_;
    std::string lastError_;
    // Scene読み込み時に通常モデル用PSOと天球用PSOを選び直すために保存する。
    bool isSkySphere_ = false;
};
