#include "Sprite.h"

#include "DirectXCommon.h"
#include "LightingManager.h"
#include "Matrix4x4.h"
#include "TextureManager.h"

#include <cassert>

void Sprite::Initialize(
    DirectXCommon* dxCommon,
    LightingManager* lightingManager,
    TextureManager* textureManager,
    ID3D12RootSignature* rootSignature,
    ID3D12PipelineState* pipelineState,
    uint32_t windowWidth,
    uint32_t windowHeight) {
    assert(dxCommon != nullptr);
    assert(lightingManager != nullptr);
    assert(textureManager != nullptr);
    assert(rootSignature != nullptr);
    assert(pipelineState != nullptr);
    assert(windowWidth > 0);
    assert(windowHeight > 0);

    dxCommon_ = dxCommon;
    lightingManager_ = lightingManager;
    textureManager_ = textureManager;
    rootSignature_ = rootSignature;
    pipelineState_ = pipelineState;
    Resize(windowWidth, windowHeight);

    // 0〜1の単位矩形をCPU側のひな形として保持する。
    // Draw時にフレーム専用Upload領域へコピーするため、GPU使用中の頂点を上書きしない。
    const TextureVertexData defaultVertices[kVertexCount] = {
        // 0:左下、1:左上、2:右下、3:右上。
        // 2枚の三角形で重なる頂点を、インデックスバッファによって共有する。
        { { 0.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
        { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
        { { 1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
        { { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
    };
    SetVertices(defaultVertices);
    CreateIndexBufferResource();

}

void Sprite::CreateIndexBufferResource() {
    // 三角形1は0→1→2、三角形2は1→3→2の順で頂点を参照する。
    const uint32_t indices[kIndexCount] = { 0, 1, 2, 1, 3, 2 };
    const size_t indexBufferSize = sizeof(indices);

    // 描画中に変化しないので、GPUが読みやすいDEFAULTヒープへ初期化時に転送する。
    indexResource_ = dxCommon_->CreateStaticBufferResource(
        indices,
        indexBufferSize,
        D3D12_RESOURCE_STATE_INDEX_BUFFER);
    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = static_cast<UINT>(indexBufferSize);
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
}

void Sprite::Resize(uint32_t windowWidth, uint32_t windowHeight) {
    assert(windowWidth > 0);
    assert(windowHeight > 0);
    windowWidth_ = windowWidth;
    windowHeight_ = windowHeight;
}

void Sprite::SetVertices(const TextureVertexData* vertices) {
    assert(vertices != nullptr);

    // GPUバッファではなくCPU側のひな形だけを書き換えるので、フレーム並列化後も安全。
    for (UINT i = 0; i < kVertexCount; ++i) {
        vertices_[i] = vertices[i];
    }
}

void Sprite::BeginFrame() {
    spriteDrawCount_ = 0;
}

void Sprite::Draw(
    const TransformData& transform,
    const Vector4& color,
    int textureHandle,
    const UVTransform& uvTransform) {
    assert(dxCommon_ != nullptr);
    assert(textureManager_ != nullptr);
    assert(rootSignature_ != nullptr);
    assert(pipelineState_ != nullptr);
    assert(textureHandle >= 0);

    if (spriteDrawCount_ >= kMaxSpriteCount) {
        assert(false && "Sprite draw count exceeded kMaxSpriteCount.");
        return;
    }
    ++spriteDrawCount_;

    // 頂点・Material・行列ごとに、現在フレーム専用の異なるGPUアドレスを確保する。
    DynamicBufferAllocation vertexAllocation = dxCommon_->AllocateDynamicBuffer(
        sizeof(TextureVertexData) * kVertexCount, 16);
    auto* dynamicVertices =
        static_cast<TextureVertexData*>(vertexAllocation.cpuAddress);
    for (UINT i = 0; i < kVertexCount; ++i) {
        dynamicVertices[i] = vertices_[i];
    }
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
    vertexBufferView.BufferLocation = vertexAllocation.gpuAddress;
    vertexBufferView.SizeInBytes = static_cast<UINT>(vertexAllocation.sizeInBytes);
    vertexBufferView.StrideInBytes = sizeof(TextureVertexData);

    DynamicBufferAllocation materialAllocation = dxCommon_->AllocateDynamicBuffer(
        sizeof(Material), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    auto* material = static_cast<Material*>(materialAllocation.cpuAddress);
    *material = {};
    material->color = { color.x, color.y, color.z, color.w };
    material->enableLighting = 0;
    material->uvTransform = MakeUVTransformMatrix(uvTransform);

    const Matrix4x4 worldMatrix = MakeAffineMatrix(
        transform.scale, transform.rotate, transform.translate);
    const Matrix4x4 orthographicMatrix = MakeOrthographicMatrix(
        0.0f,
        0.0f,
        static_cast<float>(windowWidth_),
        static_cast<float>(windowHeight_),
        0.0f,
        100.0f);
    DynamicBufferAllocation transformationAllocation =
        dxCommon_->AllocateDynamicBuffer(
            sizeof(TransformationMatrix),
            D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    auto* transformation =
        static_cast<TransformationMatrix*>(transformationAllocation.cpuAddress);
    transformation->World = worldMatrix;
    transformation->WVP = Multiply(worldMatrix, orthographicMatrix);
    transformation->WorldInverseTranspose = Transpose(Inverse(worldMatrix));

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap* descriptorHeaps[] = { textureManager_->GetSrvHeap() };

    // ルートパラメータの番号は3D描画と共通だが、PSOはSprite専用設定を使う。
    commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipelineState_.Get());
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
    commandList->IASetIndexBuffer(&indexBufferView_);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // b0=Material、b1=WVP、t0=Texture の対応でシェーダーへデータを渡す。
    commandList->SetGraphicsRootConstantBufferView(
        0, materialAllocation.gpuAddress);
    commandList->SetGraphicsRootConstantBufferView(
        1, transformationAllocation.gpuAddress);
    commandList->SetGraphicsRootDescriptorTable(
        2, textureManager_->GetSrvHandleGPU(textureHandle));
    commandList->SetGraphicsRootConstantBufferView(
        3, lightingManager_->GetLightingGpuAddress());

    // 4頂点を6個のインデックスで参照し、2枚の三角形として描画する。
    commandList->DrawIndexedInstanced(kIndexCount, 1, 0, 0, 0);
}
