#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include "CommonTypes.h"

class DirectXCommon;
class DebugCamera;
class LightingManager;
class TextureManager;

// 3Dプリミティブ（三角形・球）のGPUリソースを所有し、描画コマンドを発行するクラス。
// Graphics は共通のパイプラインを作るだけにし、形状ごとの頂点・行列・マテリアルはここで管理する。
class PrimitiveDrawer {
public:
    // 1フレーム中に発行できるプリミティブ描画数の上限。
    static constexpr uint32_t kMaxTriangleCount = 1000;
    static constexpr uint32_t kMaxSphereCount = 100;

    PrimitiveDrawer() = default;
    // 静的GPUリソースはComPtrが自動解放する。
    ~PrimitiveDrawer() = default;

    PrimitiveDrawer(const PrimitiveDrawer&) = delete;
    PrimitiveDrawer& operator=(const PrimitiveDrawer&) = delete;

    // Graphics が作成した共通のルートシグネチャ／PSOを受け取り、形状ごとのリソースを作成する。
    void Initialize(
        DirectXCommon* dxCommon,
        DebugCamera* debugCamera,
        LightingManager* lightingManager,
        TextureManager* textureManager,
        ID3D12RootSignature* rootSignature,
        ID3D12PipelineState* pipelineState,
        ID3D12RootSignature* shadowRootSignature,
        ID3D12PipelineState* shadowPipelineState);

    // Graphics::BeginDrawから呼び、今フレームの自動採番を0に戻す。
    void BeginFrame();

    // indexは内部で自動採番する。呼び出し側は描画データだけを渡す。
    void DrawTriangle(
        const TextureVertexData* vertices,
        const Matrix4x4& worldMatrix,
        const Vector4& color,
        int textureHandle,
        const UVTransform& uvTransform);
    void DrawSphere(
        const Matrix4x4& worldMatrix,
        const Vector4& color,
        int textureHandle,
        const UVTransform& uvTransform);
    void DrawTriangleShadow(
        const TextureVertexData* vertices,
        const Matrix4x4& worldMatrix,
        const Matrix4x4& lightViewProjection);
    void DrawSphereShadow(
        const Matrix4x4& worldMatrix,
        const Matrix4x4& lightViewProjection);

private:
    static constexpr uint32_t kTriangleVertexCount = 3;
    static constexpr uint32_t kTriangleIndexCount = 3;
    // 緯度・経度を16分割し、各マスを2枚の三角形で表す。
    static constexpr uint32_t kSphereSubdivision = 16;

    void CreateTriangleResources();
    void CreateSphereResources();
    void SetCommonDrawState(int fallbackTextureHandle);
    void SetShadowDrawState();

    // 所有しない参照。生成・破棄の順序は Graphics が管理する。
    DirectXCommon* dxCommon_ = nullptr;
    DebugCamera* debugCamera_ = nullptr;
    LightingManager* lightingManager_ = nullptr;
    TextureManager* textureManager_ = nullptr;
    // 共通の設定を共有するためComPtrで参照カウントを保持する。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> shadowRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowPipelineState_;

    // ----- 三角形群用リソース -----
    // インデックスは全三角形で共有し、動的頂点はフレーム用Upload領域からDrawごとに確保する。
    Microsoft::WRL::ComPtr<ID3D12Resource> triangleIndexResource_;
    D3D12_INDEX_BUFFER_VIEW triangleIndexBufferView_{};
    uint32_t triangleDrawCount_ = 0;

    // ----- 球用リソース -----
    // 球の頂点データを保持するGPUバッファ。
    Microsoft::WRL::ComPtr<ID3D12Resource> sphereVertexResource_;
    D3D12_VERTEX_BUFFER_VIEW sphereVertexBufferView_{};
    uint32_t sphereVertexCount_ = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> sphereIndexResource_;
    D3D12_INDEX_BUFFER_VIEW sphereIndexBufferView_{};
    uint32_t sphereIndexCount_ = 0;
    uint32_t sphereDrawCount_ = 0;
};
