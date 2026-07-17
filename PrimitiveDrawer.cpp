#include "PrimitiveDrawer.h"

#include "DX12Utility.h"
#include "DebugCamera.h"
#include "DirectXCommon.h"
#include "Matrix4x4.h"
#include "TextureManager.h"

#include <cassert>
#include <cmath>

void PrimitiveDrawer::Initialize(
    DirectXCommon* dxCommon,
    DebugCamera* debugCamera,
    TextureManager* textureManager,
    ID3D12RootSignature* rootSignature,
    ID3D12PipelineState* pipelineState,
    uint32_t windowWidth,
    uint32_t windowHeight) {
    assert(dxCommon != nullptr);
    assert(debugCamera != nullptr);
    assert(textureManager != nullptr);
    assert(rootSignature != nullptr);
    assert(pipelineState != nullptr);
    assert(windowWidth > 0 && windowHeight > 0);

    dxCommon_ = dxCommon;
    debugCamera_ = debugCamera;
    textureManager_ = textureManager;
    rootSignature_ = rootSignature;
    pipelineState_ = pipelineState;
    windowWidth_ = windowWidth;
    windowHeight_ = windowHeight;

    // 描画範囲をウィンドウ全体に設定する。
    viewport_.Width = static_cast<float>(windowWidth_);
    viewport_.Height = static_cast<float>(windowHeight_);
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;
    scissorRect_.right = static_cast<LONG>(windowWidth_);
    scissorRect_.bottom = static_cast<LONG>(windowHeight_);

    // 形状ごとの頂点・定数バッファと、両方で共有するライトを作成する。
    CreateTriangleResources();
    CreateDirectionalLightResource();
    CreateSphereResources();
}

void PrimitiveDrawer::CreateTriangleResources() {
    // 最大数分の頂点を、連続した1つの頂点バッファとして確保する。
    const size_t vertexBufferSize =
        sizeof(TextureVertexData) * kTriangleVertexCount * kMaxTriangleCount;
    triangleVertexResource_ = DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), vertexBufferSize);
    triangleVertexBufferView_.BufferLocation = triangleVertexResource_->GetGPUVirtualAddress();
    triangleVertexBufferView_.SizeInBytes = static_cast<UINT>(vertexBufferSize);
    triangleVertexBufferView_.StrideInBytes = sizeof(TextureVertexData);

    // MapするとCPUアドレスを取得でき、SetTriangleVerticesから直接書き込める。
    HRESULT hr = triangleVertexResource_->Map(
        0, nullptr, reinterpret_cast<void**>(&triangleVertexData_));
    assert(SUCCEEDED(hr));

    // 1枚の三角形は3頂点を0→1→2の順で参照する。
    const size_t indexBufferSize = sizeof(uint32_t) * kTriangleIndexCount;
    triangleIndexResource_ = DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), indexBufferSize);
    triangleIndexBufferView_.BufferLocation = triangleIndexResource_->GetGPUVirtualAddress();
    triangleIndexBufferView_.SizeInBytes = static_cast<UINT>(indexBufferSize);
    triangleIndexBufferView_.Format = DXGI_FORMAT_R32_UINT;

    uint32_t* indexData = nullptr;
    hr = triangleIndexResource_->Map(
        0, nullptr, reinterpret_cast<void**>(&indexData));
    assert(SUCCEEDED(hr));
    indexData[0] = 0;
    indexData[1] = 1;
    indexData[2] = 2;
    triangleIndexResource_->Unmap(0, nullptr);

}

void PrimitiveDrawer::CreateTriangleInstanceResources(uint32_t index) {
    // 同じ三角形へ頂点を再設定した場合は、既存の定数バッファを再利用する。
    if (triangleMaterialResources_[index] != nullptr) {
        return;
    }

    // 実際に登録された三角形だけ、個別のMaterialとWVPを作る。
    const UINT materialBufferSize = (sizeof(Material) + 255) & ~255u;
    triangleMaterialResources_[index] = DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), materialBufferSize);
    HRESULT hr = triangleMaterialResources_[index]->Map(
        0, nullptr, reinterpret_cast<void**>(&triangleMaterialData_[index]));
    assert(SUCCEEDED(hr));
    triangleMaterialData_[index]->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    triangleMaterialData_[index]->enableLighting = 1;
    triangleMaterialData_[index]->uvTransform = MakeIdentity4x4();

    triangleWvpResources_[index] = DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), sizeof(TransformationMatrix));
    hr = triangleWvpResources_[index]->Map(
        0, nullptr, reinterpret_cast<void**>(&triangleWvpData_[index]));
    assert(SUCCEEDED(hr));
    triangleWvpData_[index]->WVP = MakeIdentity4x4();
    triangleWvpData_[index]->World = MakeIdentity4x4();
}

void PrimitiveDrawer::CreateDirectionalLightResource() {
    // b2に渡す平行光源の定数バッファを作成する。
    const UINT lightBufferSize = (sizeof(DirectionalLight) + 255) & ~255u;
    directionalLightResource_ = DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), lightBufferSize);

    const HRESULT hr = directionalLightResource_->Map(
        0, nullptr, reinterpret_cast<void**>(&directionalLightData_));
    assert(SUCCEEDED(hr));
    directionalLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLightData_->direction = { 0.0f, -1.0f, 1.0f };
    directionalLightData_->direction.Normalize();
    directionalLightData_->intensity = 1.0f;
}

void PrimitiveDrawer::CreateSphereResources() {
    // 緯度・経度の境界頂点を共有し、各四角形を6個のインデックスで2三角形にする。
    const uint32_t verticesPerRow = kSphereSubdivision + 1;
    sphereVertexCount_ = verticesPerRow * verticesPerRow;
    sphereIndexCount_ = kSphereSubdivision * kSphereSubdivision * 6;

    const size_t vertexBufferSize = sizeof(TextureVertexData) * sphereVertexCount_;
    sphereVertexResource_ = DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), vertexBufferSize);
    sphereVertexBufferView_.BufferLocation = sphereVertexResource_->GetGPUVirtualAddress();
    sphereVertexBufferView_.SizeInBytes = static_cast<UINT>(vertexBufferSize);
    sphereVertexBufferView_.StrideInBytes = sizeof(TextureVertexData);

    TextureVertexData* vertexData = nullptr;
    HRESULT hr = sphereVertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    assert(SUCCEEDED(hr));

    constexpr float kPi = 3.1415926535f;
    // 継ぎ目のUVを0と1の両方で持つため、経度方向は分割数+1頂点作る。
    for (uint32_t latIndex = 0; latIndex <= kSphereSubdivision; ++latIndex) {
        const float v = static_cast<float>(latIndex) / kSphereSubdivision;
        const float latitude = -kPi / 2.0f + kPi * v;

        for (uint32_t lonIndex = 0; lonIndex <= kSphereSubdivision; ++lonIndex) {
            const float u = static_cast<float>(lonIndex) / kSphereSubdivision;
            const float longitude = 2.0f * kPi * u;
            const uint32_t vertexIndex = latIndex * verticesPerRow + lonIndex;

            TextureVertexData& vertex = vertexData[vertexIndex];
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
    sphereVertexResource_->Unmap(0, nullptr);

    const size_t indexBufferSize = sizeof(uint32_t) * sphereIndexCount_;
    sphereIndexResource_ = DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), indexBufferSize);
    sphereIndexBufferView_.BufferLocation = sphereIndexResource_->GetGPUVirtualAddress();
    sphereIndexBufferView_.SizeInBytes = static_cast<UINT>(indexBufferSize);
    sphereIndexBufferView_.Format = DXGI_FORMAT_R32_UINT;

    uint32_t* indexData = nullptr;
    hr = sphereIndexResource_->Map(
        0, nullptr, reinterpret_cast<void**>(&indexData));
    assert(SUCCEEDED(hr));

    uint32_t writeIndex = 0;
    for (uint32_t latIndex = 0; latIndex < kSphereSubdivision; ++latIndex) {
        for (uint32_t lonIndex = 0; lonIndex < kSphereSubdivision; ++lonIndex) {
            const uint32_t a = latIndex * verticesPerRow + lonIndex;
            const uint32_t b = (latIndex + 1) * verticesPerRow + lonIndex;
            const uint32_t c = a + 1;
            const uint32_t d = b + 1;

            indexData[writeIndex++] = a;
            indexData[writeIndex++] = b;
            indexData[writeIndex++] = c;
            indexData[writeIndex++] = c;
            indexData[writeIndex++] = b;
            indexData[writeIndex++] = d;
        }
    }
    assert(writeIndex == sphereIndexCount_);
    sphereIndexResource_->Unmap(0, nullptr);
}

void PrimitiveDrawer::CreateSphereInstanceResources(uint32_t index) {
    if (sphereMaterialResources_[index] != nullptr) {
        return;
    }

    const UINT materialBufferSize = (sizeof(Material) + 255) & ~255u;
    sphereMaterialResources_[index] = DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), materialBufferSize);
    HRESULT hr = sphereMaterialResources_[index]->Map(
        0, nullptr, reinterpret_cast<void**>(&sphereMaterialData_[index]));
    assert(SUCCEEDED(hr));
    sphereMaterialData_[index]->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    sphereMaterialData_[index]->enableLighting = 1;
    sphereMaterialData_[index]->uvTransform = MakeIdentity4x4();

    sphereWvpResources_[index] = DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), sizeof(TransformationMatrix));
    hr = sphereWvpResources_[index]->Map(
        0, nullptr, reinterpret_cast<void**>(&sphereWvpData_[index]));
    assert(SUCCEEDED(hr));
    sphereWvpData_[index]->WVP = MakeIdentity4x4();
    sphereWvpData_[index]->World = MakeIdentity4x4();
}

void PrimitiveDrawer::BeginFrame() {
    triangleDrawCount_ = 0;
    sphereDrawCount_ = 0;
}

void PrimitiveDrawer::UpdateTriangleMatrix(
    uint32_t index,
    const TransformData& transform) {
    const Matrix4x4 worldMatrix = MakeAffineMatrix(
        transform.scale, transform.rotate, transform.translate);
    triangleWvpData_[index]->World = worldMatrix;
    // Graphicsが所有する共通カメラを参照し、全3D描画で同じ視点を使う。
    triangleWvpData_[index]->WVP = Multiply(
        worldMatrix, debugCamera_->GetViewProjectionMatrix());
}

void PrimitiveDrawer::UpdateSphereMatrix(
    uint32_t index,
    const TransformData& transform) {
    const Matrix4x4 worldMatrix = MakeAffineMatrix(
        transform.scale, transform.rotate, transform.translate);
    sphereWvpData_[index]->World = worldMatrix;
    sphereWvpData_[index]->WVP = Multiply(
        worldMatrix, debugCamera_->GetViewProjectionMatrix());
}

void PrimitiveDrawer::SetCommonDrawState() {
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap* descriptorHeaps[] = { textureManager_->GetSrvHeap() };
    // SRVを使う前に、シェーダーから参照可能なディスクリプタヒープを設定する。
    commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipelineState_.Get());
    commandList->RSSetViewports(1, &viewport_);
    commandList->RSSetScissorRects(1, &scissorRect_);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // ルートパラメータ3（b2）へ平行光源を設定する。
    commandList->SetGraphicsRootConstantBufferView(
        3, directionalLightResource_->GetGPUVirtualAddress());
}

void PrimitiveDrawer::DrawTriangle(
    const TextureVertexData* vertices,
    const TransformData& transform,
    const Vector4& color,
    int textureHandle,
    const UVTransform& uvTransform) {
    assert(dxCommon_ != nullptr);
    assert(vertices != nullptr);
    assert(textureHandle >= 0);

    if (triangleDrawCount_ >= kMaxTriangleCount) {
        assert(false && "Triangle draw count exceeded kMaxTriangleCount.");
        return;
    }
    const uint32_t index = triangleDrawCount_++;
    CreateTriangleInstanceResources(index);

    // Draw順から決めた内部スロットへ、今回の3頂点をコピーする。
    for (uint32_t i = 0; i < kTriangleVertexCount; ++i) {
        triangleVertexData_[index * kTriangleVertexCount + i] = vertices[i];
    }

    // Drawの引数を、その三角形専用の定数バッファへ反映する。
    UpdateTriangleMatrix(index, transform);
    triangleMaterialData_[index]->color = { color.x, color.y, color.z, color.w };
    // この三角形専用のUV変換をMaterial定数バッファへ書き込む。
    triangleMaterialData_[index]->uvTransform = MakeUVTransformMatrix(uvTransform);

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    SetCommonDrawState();
    commandList->IASetVertexBuffers(0, 1, &triangleVertexBufferView_);
    commandList->IASetIndexBuffer(&triangleIndexBufferView_);
    commandList->SetGraphicsRootConstantBufferView(
        0, triangleMaterialResources_[index]->GetGPUVirtualAddress());

    // 指定された三角形のb1（WVP）とt0（Texture）を設定して1個だけ描画する。
    commandList->SetGraphicsRootConstantBufferView(
        1, triangleWvpResources_[index]->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(
        2, textureManager_->GetSrvHandleGPU(textureHandle));
    // インデックスは常に0,1,2で、BaseVertexLocationで内部スロットの頂点へずらす。
    commandList->DrawIndexedInstanced(
        kTriangleIndexCount,
        1,
        0,
        static_cast<INT>(index * kTriangleVertexCount),
        0);
}

void PrimitiveDrawer::DrawSphere(
    const TransformData& transform,
    const Vector4& color,
    int textureHandle,
    const UVTransform& uvTransform) {
    assert(dxCommon_ != nullptr);
    assert(textureHandle >= 0);

    if (sphereDrawCount_ >= kMaxSphereCount) {
        assert(false && "Sphere draw count exceeded kMaxSphereCount.");
        return;
    }
    const uint32_t index = sphereDrawCount_++;
    CreateSphereInstanceResources(index);

    UpdateSphereMatrix(index, transform);
    sphereMaterialData_[index]->color = { color.x, color.y, color.z, color.w };
    // 球にも三角形とは独立したUV変換を設定できる。
    sphereMaterialData_[index]->uvTransform = MakeUVTransformMatrix(uvTransform);

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    SetCommonDrawState();
    // 球の頂点バッファ、b0（Material）、b1（WVP）、t0（Texture）を順にバインドする。
    commandList->IASetVertexBuffers(0, 1, &sphereVertexBufferView_);
    commandList->IASetIndexBuffer(&sphereIndexBufferView_);
    commandList->SetGraphicsRootConstantBufferView(
        0, sphereMaterialResources_[index]->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(
        1, sphereWvpResources_[index]->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(
        2, textureManager_->GetSrvHandleGPU(textureHandle));
    commandList->DrawIndexedInstanced(sphereIndexCount_, 1, 0, 0, 0);
}
