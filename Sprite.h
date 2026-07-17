#pragma once

#include <d3d12.h>
#include <wrl.h>
#include "CommonTypes.h"

class DirectXCommon;
class TextureManager;

// 2Dスプライト1枚分の頂点・マテリアル・行列を管理して描画するクラス。
// 3Dと同じルートシグネチャ／PSOを使い、Materialのライティングを無効化して2Dとして描画する。
class Sprite {
public:
    Sprite() = default;
    // GPUリソースはComPtrが自動解放する。Uploadヒープは永続Mapのまま破棄できる。
    ~Sprite() = default;

    Sprite(const Sprite&) = delete;
    Sprite& operator=(const Sprite&) = delete;

    // 画面サイズは、ピクセル座標をそのまま使う正射影行列の作成に使用する。
    void Initialize(
        DirectXCommon* dxCommon,
        TextureManager* textureManager,
        ID3D12RootSignature* rootSignature,
        ID3D12PipelineState* pipelineState,
        uint32_t windowWidth,
        uint32_t windowHeight);

    // Graphics::BeginDrawから呼び、今フレームの自動採番を0に戻す。
    void BeginFrame();

    // 頂点形状を差し替える場合だけ使用する。通常は初期化時の四角形を使う。
    void SetVertices(const TextureVertexData* vertices);

    // 描画に必要な座標・色・テクスチャ・UV変換を、Drawの引数だけで指定する。
    void Draw(
        const TransformData& transform,
        const Vector4& color,
        int textureHandle,
        const UVTransform& uvTransform);

private:
    // インデックスバッファを使わず、四角形を2枚の三角形で表現する。
    static constexpr UINT kVertexCount = 6;
    static constexpr uint32_t kMaxSpriteCount = 1000;

    void CreateInstanceResources(uint32_t index);
    void UpdateMatrix(uint32_t index, const TransformData& transform);

    DirectXCommon* dxCommon_ = nullptr;
    TextureManager* textureManager_ = nullptr;

    // ルートシグネチャとPSOは3D描画と共有する。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

    // Uploadヒープの頂点／定数バッファ。Mapしたポインタへ書くとGPUから読める。
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    TextureVertexData* vertexData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> materialResources_[kMaxSpriteCount];
    Material* materialData_[kMaxSpriteCount]{};

    // Object3d用ルートシグネチャのb2を有効な値で埋めるためのダミー光源。
    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResources_[kMaxSpriteCount];
    TransformationMatrix* wvpData_[kMaxSpriteCount]{};

    uint32_t windowWidth_ = 0;
    uint32_t windowHeight_ = 0;
    uint32_t spriteDrawCount_ = 0;
};
