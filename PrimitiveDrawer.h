#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include "CommonTypes.h"

class DirectXCommon;
class DebugCamera;
class TextureManager;

// 3Dプリミティブ（三角形・球）のGPUリソースを所有し、描画コマンドを発行するクラス。
// Graphics は共通のパイプラインを作るだけにし、形状ごとの頂点・行列・マテリアルはここで管理する。
class PrimitiveDrawer {
public:
    // 1つの頂点バッファに確保しておく三角形の上限。
    static constexpr uint32_t kMaxTriangleCount = 1000;
    static constexpr uint32_t kMaxSphereCount = 100;

    PrimitiveDrawer() = default;
    // GPUリソースはComPtrが自動解放する。Uploadヒープは永続Mapのまま破棄できる。
    ~PrimitiveDrawer() = default;

    PrimitiveDrawer(const PrimitiveDrawer&) = delete;
    PrimitiveDrawer& operator=(const PrimitiveDrawer&) = delete;

    // Graphics が作成した共通のルートシグネチャ／PSOを受け取り、形状ごとのリソースを作成する。
    void Initialize(
        DirectXCommon* dxCommon,
        DebugCamera* debugCamera,
        TextureManager* textureManager,
        ID3D12RootSignature* rootSignature,
        ID3D12PipelineState* pipelineState,
        uint32_t windowWidth,
        uint32_t windowHeight);

    // Graphics::BeginDrawから呼び、今フレームの自動採番を0に戻す。
    void BeginFrame();

    // indexは内部で自動採番する。呼び出し側は描画データだけを渡す。
    void DrawTriangle(
        const TextureVertexData* vertices,
        const TransformData& transform,
        const Vector4& color,
        int textureHandle,
        const UVTransform& uvTransform);
    void DrawSphere(
        const TransformData& transform,
        const Vector4& color,
        int textureHandle,
        const UVTransform& uvTransform);

private:
    static constexpr uint32_t kTriangleVertexCount = 3;
    static constexpr uint32_t kTriangleIndexCount = 3;
    // 緯度・経度を16分割し、各マスを2枚の三角形で表す。
    static constexpr uint32_t kSphereSubdivision = 16;

    void CreateTriangleResources();
    void CreateTriangleInstanceResources(uint32_t index);
    void CreateSphereResources();
    void CreateSphereInstanceResources(uint32_t index);
    void CreateDirectionalLightResource();
    void UpdateTriangleMatrix(uint32_t index, const TransformData& transform);
    void UpdateSphereMatrix(uint32_t index, const TransformData& transform);
    void SetCommonDrawState();

    // 所有しない参照。生成・破棄の順序は Graphics が管理する。
    DirectXCommon* dxCommon_ = nullptr;
    DebugCamera* debugCamera_ = nullptr;
    TextureManager* textureManager_ = nullptr;
    // 共通の設定を共有するためComPtrで参照カウントを保持する。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

    D3D12_VIEWPORT viewport_{};
    D3D12_RECT scissorRect_{};
    uint32_t windowWidth_ = 0;
    uint32_t windowHeight_ = 0;

    // ----- 三角形群用リソース -----
    // 頂点は全三角形で1つの頂点バッファを共有し、firstVertexで描画位置を切り替える。
    Microsoft::WRL::ComPtr<ID3D12Resource> triangleVertexResource_;
    D3D12_VERTEX_BUFFER_VIEW triangleVertexBufferView_{};
    TextureVertexData* triangleVertexData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> triangleIndexResource_;
    D3D12_INDEX_BUFFER_VIEW triangleIndexBufferView_{};

    // 色を三角形ごとに変えられるよう、マテリアル定数バッファも個別に持つ。
    Microsoft::WRL::ComPtr<ID3D12Resource> triangleMaterialResources_[kMaxTriangleCount];
    Material* triangleMaterialData_[kMaxTriangleCount]{};
    Microsoft::WRL::ComPtr<ID3D12Resource> triangleWvpResources_[kMaxTriangleCount];
    TransformationMatrix* triangleWvpData_[kMaxTriangleCount]{};
    uint32_t triangleDrawCount_ = 0;

    // ----- 球用リソース -----
    // 球の頂点データを保持するGPUバッファ。
    Microsoft::WRL::ComPtr<ID3D12Resource> sphereVertexResource_;
    D3D12_VERTEX_BUFFER_VIEW sphereVertexBufferView_{};
    uint32_t sphereVertexCount_ = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> sphereIndexResource_;
    D3D12_INDEX_BUFFER_VIEW sphereIndexBufferView_{};
    uint32_t sphereIndexCount_ = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> sphereMaterialResources_[kMaxSphereCount];
    Material* sphereMaterialData_[kMaxSphereCount]{};
    Microsoft::WRL::ComPtr<ID3D12Resource> sphereWvpResources_[kMaxSphereCount];
    TransformationMatrix* sphereWvpData_[kMaxSphereCount]{};
    uint32_t sphereDrawCount_ = 0;
    // b2 に設定する平行光源。三角形と球で共有する。
    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;
};
