#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <string>
#include <vector>

#include "CommonTypes.h"

class DirectXCommon;
class DebugCamera;
class LightingManager;
class TextureManager;

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
        const TransformData& transform,
        const Vector4& color,
        int textureHandle,
        const UVTransform& uvTransform);

    uint32_t GetVertexCount() const { return vertexCount_; }
    uint32_t GetIndexCount() const { return indexCount_; }
    const std::string& GetSourcePath() const { return sourcePath_; }

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

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    uint32_t vertexCount_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
    uint32_t indexCount_ = 0;

    std::string sourcePath_;
    std::string materialTexturePath_;
    std::string lastError_;
};
