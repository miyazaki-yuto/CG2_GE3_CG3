#include "PrimitiveDrawer.h"

#include "DebugCamera.h"
#include "DirectXCommon.h"
#include "LightingManager.h"
#include "Matrix4x4.h"
#include "TextureManager.h"

#include <cassert>
#include <cmath>
#include <vector>

namespace {

DynamicBufferAllocation AllocateMaterialData(
    DirectXCommon* dxCommon,
    const Vector4& color,
    const UVTransform& uvTransform) {
    DynamicBufferAllocation allocation = dxCommon->AllocateDynamicBuffer(
        sizeof(MaterialConstants), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    auto* material = static_cast<MaterialConstants*>(allocation.cpuAddress);
    *material = {};
    material->color = { color.x, color.y, color.z, color.w };
    material->enableLighting = 1;
    material->uvTransform = MakeUVTransformMatrix(uvTransform);
    // プリミティブにはMTLがないため、白いKsとシーン共通のShininessを使う。
    material->specularColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    material->specularShininess = 0.0f;
    material->metallic = 0.0f;
    material->roughness = 0.5f;
    return allocation;
}

DynamicBufferAllocation AllocateTransformationData(
    DirectXCommon* dxCommon,
    const Matrix4x4& worldMatrix,
    const Matrix4x4& viewProjectionMatrix) {
    DynamicBufferAllocation allocation = dxCommon->AllocateDynamicBuffer(
        sizeof(TransformationMatrix),
        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    auto* transformation =
        static_cast<TransformationMatrix*>(allocation.cpuAddress);
    transformation->World = worldMatrix;
    transformation->WVP = Multiply(worldMatrix, viewProjectionMatrix);
    transformation->WorldInverseTranspose = Transpose(Inverse(worldMatrix));
    return allocation;
}

} // namespace

void PrimitiveDrawer::Initialize(
    DirectXCommon* dxCommon,
    DebugCamera* debugCamera,
    LightingManager* lightingManager,
    TextureManager* textureManager,
    ID3D12RootSignature* rootSignature,
    const std::array<
        ID3D12PipelineState*, kBlendModeCount>& pipelineStates,
    ID3D12RootSignature* shadowRootSignature,
    ID3D12PipelineState* shadowPipelineState) {
    assert(dxCommon != nullptr);
    assert(debugCamera != nullptr);
    assert(lightingManager != nullptr);
    assert(textureManager != nullptr);
    assert(rootSignature != nullptr);
    for (ID3D12PipelineState* pipelineState : pipelineStates) {
        assert(pipelineState != nullptr);
    }
    assert(shadowRootSignature != nullptr);
    assert(shadowPipelineState != nullptr);

    dxCommon_ = dxCommon;
    debugCamera_ = debugCamera;
    lightingManager_ = lightingManager;
    textureManager_ = textureManager;
    rootSignature_ = rootSignature;
    for (size_t index = 0; index < pipelineStates_.size(); ++index) {
        pipelineStates_[index] = pipelineStates[index];
    }
    shadowRootSignature_ = shadowRootSignature;
    shadowPipelineState_ = shadowPipelineState;

    // 変更しない形状データだけを初期化時にGPU専用バッファへ転送する。
    CreateTriangleResources();
    CreateSphereResources();
}

void PrimitiveDrawer::CreateTriangleResources() {
    // 頂点はDrawごとにフレーム用領域へ書く。0→1→2のインデックスだけを共有する。
    const uint32_t indices[kTriangleIndexCount] = { 0, 1, 2 };
    const size_t indexBufferSize = sizeof(uint32_t) * kTriangleIndexCount;
    triangleIndexResource_ = dxCommon_->CreateStaticBufferResource(
        indices,
        indexBufferSize,
        D3D12_RESOURCE_STATE_INDEX_BUFFER);
    triangleIndexBufferView_.BufferLocation = triangleIndexResource_->GetGPUVirtualAddress();
    triangleIndexBufferView_.SizeInBytes = static_cast<UINT>(indexBufferSize);
    triangleIndexBufferView_.Format = DXGI_FORMAT_R32_UINT;
}

void PrimitiveDrawer::CreateSphereResources() {
    // 緯度・経度の境界頂点を共有し、各四角形を6個のインデックスで2三角形にする。
    const uint32_t verticesPerRow = kSphereSubdivision + 1;
    sphereVertexCount_ = verticesPerRow * verticesPerRow;
    sphereIndexCount_ = kSphereSubdivision * kSphereSubdivision * 6;

    std::vector<TextureVertexData> vertices(sphereVertexCount_);
    const size_t vertexBufferSize = sizeof(TextureVertexData) * sphereVertexCount_;
    constexpr float kPi = 3.1415926535f;
    // 継ぎ目のUVを0と1の両方で持つため、経度方向は分割数+1頂点作る。
    for (uint32_t latIndex = 0; latIndex <= kSphereSubdivision; ++latIndex) {
        const float v = static_cast<float>(latIndex) / kSphereSubdivision;
        const float latitude = -kPi / 2.0f + kPi * v;

        for (uint32_t lonIndex = 0; lonIndex <= kSphereSubdivision; ++lonIndex) {
            const float u = static_cast<float>(lonIndex) / kSphereSubdivision;
            const float longitude = 2.0f * kPi * u;
            const uint32_t vertexIndex = latIndex * verticesPerRow + lonIndex;

            TextureVertexData& vertex = vertices[vertexIndex];
            vertex.position = {
                std::cos(latitude) * std::cos(longitude),
                std::sin(latitude),
                std::cos(latitude) * std::sin(longitude),
                1.0f
            };
            // 単位球では位置ベクトルをそのまま法線として使える。
            vertex.normal = { vertex.position.x, vertex.position.y, vertex.position.z };
            vertex.texcoord = { u, 1.0f - v };
        }
    }
    sphereVertexResource_ = dxCommon_->CreateStaticBufferResource(
        vertices.data(),
        vertexBufferSize,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    sphereVertexBufferView_.BufferLocation = sphereVertexResource_->GetGPUVirtualAddress();
    sphereVertexBufferView_.SizeInBytes = static_cast<UINT>(vertexBufferSize);
    sphereVertexBufferView_.StrideInBytes = sizeof(TextureVertexData);

    std::vector<uint32_t> indices(sphereIndexCount_);
    const size_t indexBufferSize = sizeof(uint32_t) * sphereIndexCount_;
    uint32_t writeIndex = 0;
    for (uint32_t latIndex = 0; latIndex < kSphereSubdivision; ++latIndex) {
        for (uint32_t lonIndex = 0; lonIndex < kSphereSubdivision; ++lonIndex) {
            const uint32_t a = latIndex * verticesPerRow + lonIndex;
            const uint32_t b = (latIndex + 1) * verticesPerRow + lonIndex;
            const uint32_t c = a + 1;
            const uint32_t d = b + 1;

            indices[writeIndex++] = a;
            indices[writeIndex++] = b;
            indices[writeIndex++] = c;
            indices[writeIndex++] = c;
            indices[writeIndex++] = b;
            indices[writeIndex++] = d;
        }
    }
    assert(writeIndex == sphereIndexCount_);
    sphereIndexResource_ = dxCommon_->CreateStaticBufferResource(
        indices.data(),
        indexBufferSize,
        D3D12_RESOURCE_STATE_INDEX_BUFFER);
    sphereIndexBufferView_.BufferLocation = sphereIndexResource_->GetGPUVirtualAddress();
    sphereIndexBufferView_.SizeInBytes = static_cast<UINT>(indexBufferSize);
    sphereIndexBufferView_.Format = DXGI_FORMAT_R32_UINT;
}

void PrimitiveDrawer::BeginFrame() {
    triangleDrawCount_ = 0;
    sphereDrawCount_ = 0;
    const DynamicBufferAllocation skinningAllocation =
        dxCommon_->AllocateDynamicBuffer(sizeof(SkinningConstants));
    *static_cast<SkinningConstants*>(skinningAllocation.cpuAddress) = {};
    disabledSkinningGpuAddress_ = skinningAllocation.gpuAddress;
}

void PrimitiveDrawer::SetCommonDrawState(
    int fallbackTextureHandle,
    BlendMode blendMode) {
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap* descriptorHeaps[] = { textureManager_->GetSrvHeap() };
    // SRVを使う前に、シェーダーから参照可能なディスクリプタヒープを設定する。
    commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(
        pipelineStates_[GetBlendModeIndex(blendMode)].Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // 全描画クラスで共有する平行光源を、ルートパラメータ3（b2）へ設定する。
    commandList->SetGraphicsRootConstantBufferView(
        3, lightingManager_->GetLightingGpuAddress());
    commandList->SetGraphicsRootDescriptorTable(
        4, textureManager_->GetDirectionalShadowSrvHandleGPU());
    // PrimitiveはNormal Mapを持たないため、t2には有効なDummy SRVを設定する。
    commandList->SetGraphicsRootDescriptorTable(
        5, textureManager_->GetSrvHandleGPU(fallbackTextureHandle));
    const int environmentTextureHandle =
        lightingManager_->GetEnvironmentTextureHandle() >= 0
        ? lightingManager_->GetEnvironmentTextureHandle()
        : fallbackTextureHandle;
    commandList->SetGraphicsRootDescriptorTable(
        6, textureManager_->GetSrvHandleGPU(environmentTextureHandle));
    commandList->SetGraphicsRootDescriptorTable(
        7, textureManager_->GetPointShadowSrvHandleGPU());
    // PrimitiveはPBR Mapを持たないため、有効なDummy SRVだけ設定する。
    commandList->SetGraphicsRootDescriptorTable(
        9, textureManager_->GetSrvHandleGPU(fallbackTextureHandle));
    commandList->SetGraphicsRootConstantBufferView(
        10, disabledSkinningGpuAddress_);
}

void PrimitiveDrawer::SetShadowDrawState() {
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(shadowRootSignature_.Get());
    commandList->SetPipelineState(shadowPipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->SetGraphicsRootConstantBufferView(
        1, disabledSkinningGpuAddress_);
}

void PrimitiveDrawer::DrawTriangle(
    const TextureVertexData* vertices,
    const Matrix4x4& worldMatrix,
    const Vector4& color,
    int textureHandle,
    const UVTransform& uvTransform,
    BlendMode blendMode) {
    assert(dxCommon_ != nullptr);
    assert(vertices != nullptr);
    assert(textureHandle >= 0);

    if (triangleDrawCount_ >= kMaxTriangleCount) {
        assert(false && "Triangle draw count exceeded kMaxTriangleCount.");
        return;
    }
    ++triangleDrawCount_;

    // 動的頂点・Material・行列は、現在フレーム専用領域からDrawごとに別アドレスを確保する。
    DynamicBufferAllocation vertexAllocation = dxCommon_->AllocateDynamicBuffer(
        sizeof(TextureVertexData) * kTriangleVertexCount, 16);
    auto* dynamicVertices =
        static_cast<TextureVertexData*>(vertexAllocation.cpuAddress);
    for (uint32_t i = 0; i < kTriangleVertexCount; ++i) {
        dynamicVertices[i] = vertices[i];
    }
    D3D12_VERTEX_BUFFER_VIEW triangleVertexBufferView{};
    triangleVertexBufferView.BufferLocation = vertexAllocation.gpuAddress;
    triangleVertexBufferView.SizeInBytes =
        static_cast<UINT>(vertexAllocation.sizeInBytes);
    triangleVertexBufferView.StrideInBytes = sizeof(TextureVertexData);

    const DynamicBufferAllocation materialAllocation =
        AllocateMaterialData(dxCommon_, color, uvTransform);
    const DynamicBufferAllocation transformationAllocation =
        AllocateTransformationData(
            dxCommon_, worldMatrix, debugCamera_->GetViewProjectionMatrix());

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    SetCommonDrawState(textureHandle, blendMode);
    commandList->IASetVertexBuffers(0, 1, &triangleVertexBufferView);
    commandList->IASetIndexBuffer(&triangleIndexBufferView_);
    commandList->SetGraphicsRootConstantBufferView(
        0, materialAllocation.gpuAddress);

    // 指定された三角形のb1（WVP）とt0（Texture）を設定して1個だけ描画する。
    commandList->SetGraphicsRootConstantBufferView(
        1, transformationAllocation.gpuAddress);
    commandList->SetGraphicsRootDescriptorTable(
        2, textureManager_->GetSrvHandleGPU(textureHandle));
    commandList->DrawIndexedInstanced(kTriangleIndexCount, 1, 0, 0, 0);
}

void PrimitiveDrawer::DrawSphere(
    const Matrix4x4& worldMatrix,
    const Vector4& color,
    int textureHandle,
    const UVTransform& uvTransform,
    BlendMode blendMode) {
    assert(dxCommon_ != nullptr);
    assert(textureHandle >= 0);

    if (sphereDrawCount_ >= kMaxSphereCount) {
        assert(false && "Sphere draw count exceeded kMaxSphereCount.");
        return;
    }
    ++sphereDrawCount_;
    const DynamicBufferAllocation materialAllocation =
        AllocateMaterialData(dxCommon_, color, uvTransform);
    const DynamicBufferAllocation transformationAllocation =
        AllocateTransformationData(
            dxCommon_, worldMatrix, debugCamera_->GetViewProjectionMatrix());

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    SetCommonDrawState(textureHandle, blendMode);
    // 球の頂点バッファ、b0（Material）、b1（WVP）、t0（Texture）を順にバインドする。
    commandList->IASetVertexBuffers(0, 1, &sphereVertexBufferView_);
    commandList->IASetIndexBuffer(&sphereIndexBufferView_);
    commandList->SetGraphicsRootConstantBufferView(
        0, materialAllocation.gpuAddress);
    commandList->SetGraphicsRootConstantBufferView(
        1, transformationAllocation.gpuAddress);
    commandList->SetGraphicsRootDescriptorTable(
        2, textureManager_->GetSrvHandleGPU(textureHandle));
    commandList->DrawIndexedInstanced(sphereIndexCount_, 1, 0, 0, 0);
}

void PrimitiveDrawer::DrawTriangleShadow(
    const TextureVertexData* vertices,
    const Matrix4x4& worldMatrix,
    const Matrix4x4& lightViewProjection) {
    assert(dxCommon_ != nullptr);
    assert(vertices != nullptr);

    DynamicBufferAllocation vertexAllocation = dxCommon_->AllocateDynamicBuffer(
        sizeof(TextureVertexData) * kTriangleVertexCount, 16);
    auto* dynamicVertices =
        static_cast<TextureVertexData*>(vertexAllocation.cpuAddress);
    for (uint32_t index = 0; index < kTriangleVertexCount; ++index) {
        dynamicVertices[index] = vertices[index];
    }
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
    vertexBufferView.BufferLocation = vertexAllocation.gpuAddress;
    vertexBufferView.SizeInBytes =
        static_cast<UINT>(vertexAllocation.sizeInBytes);
    vertexBufferView.StrideInBytes = sizeof(TextureVertexData);

    const DynamicBufferAllocation transformAllocation =
        dxCommon_->AllocateDynamicBuffer(sizeof(Matrix4x4));
    *static_cast<Matrix4x4*>(transformAllocation.cpuAddress) =
        Multiply(worldMatrix, lightViewProjection);

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    SetShadowDrawState();
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
    commandList->IASetIndexBuffer(&triangleIndexBufferView_);
    commandList->SetGraphicsRootConstantBufferView(
        0, transformAllocation.gpuAddress);
    commandList->DrawIndexedInstanced(kTriangleIndexCount, 1, 0, 0, 0);
}

void PrimitiveDrawer::DrawSphereShadow(
    const Matrix4x4& worldMatrix,
    const Matrix4x4& lightViewProjection) {
    assert(dxCommon_ != nullptr);

    const DynamicBufferAllocation transformAllocation =
        dxCommon_->AllocateDynamicBuffer(sizeof(Matrix4x4));
    *static_cast<Matrix4x4*>(transformAllocation.cpuAddress) =
        Multiply(worldMatrix, lightViewProjection);

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    SetShadowDrawState();
    commandList->IASetVertexBuffers(0, 1, &sphereVertexBufferView_);
    commandList->IASetIndexBuffer(&sphereIndexBufferView_);
    commandList->SetGraphicsRootConstantBufferView(
        0, transformAllocation.gpuAddress);
    commandList->DrawIndexedInstanced(sphereIndexCount_, 1, 0, 0, 0);
}
