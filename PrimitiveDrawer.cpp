#include "PrimitiveDrawer.h"

#include "DX12Utility.h"
#include "DirectXCommon.h"
#include "Matrix4x4.h"
#include "TextureManager.h"

#include <cassert>
#include <cmath>

PrimitiveDrawer::~PrimitiveDrawer() {
    // Mapしたリソースは、リソースを解放する前にUnmapする。
    // Uploadヒープの内容はGPUが参照するため、通常はフレーム中ずっとMapしたまま使う。
    if (triangleVertexData_ != nullptr && triangleVertexResource_ != nullptr) {
        triangleVertexResource_->Unmap(0, nullptr);
    }
    for (uint32_t i = 0; i < kMaxTriangleCount; ++i) {
        if (triangleMaterialData_[i] != nullptr && triangleMaterialResources_[i] != nullptr) {
            triangleMaterialResources_[i]->Unmap(0, nullptr);
        }
        if (triangleWvpData_[i] != nullptr && triangleWvpResources_[i] != nullptr) {
            triangleWvpResources_[i]->Unmap(0, nullptr);
        }
    }
    for (uint32_t i = 0; i < kMaxSphereCount; ++i) {
        if (sphereMaterialData_[i] != nullptr && sphereMaterialResources_[i] != nullptr) {
            sphereMaterialResources_[i]->Unmap(0, nullptr);
        }
        if (sphereWvpData_[i] != nullptr && sphereWvpResources_[i] != nullptr) {
            sphereWvpResources_[i]->Unmap(0, nullptr);
        }
    }
    if (directionalLightData_ != nullptr && directionalLightResource_ != nullptr) {
        directionalLightResource_->Unmap(0, nullptr);
    }
}

void PrimitiveDrawer::Initialize(
    DirectXCommon* dxCommon,
    TextureManager* textureManager,
    ID3D12RootSignature* rootSignature,
    ID3D12PipelineState* pipelineState,
    uint32_t windowWidth,
    uint32_t windowHeight) {
    assert(dxCommon != nullptr);
    assert(textureManager != nullptr);
    assert(rootSignature != nullptr);
    assert(pipelineState != nullptr);
    assert(windowWidth > 0 && windowHeight > 0);

    dxCommon_ = dxCommon;
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
    UpdateViewProjectionMatrix();
}

void PrimitiveDrawer::CreateTriangleResources() {
    // 最大数分の頂点を、連続した1つの頂点バッファとして確保する。
    const size_t vertexBufferSize =
        sizeof(TextureVertexData) * kTriangleVertexCount * kMaxTriangleCount;
    triangleVertexResource_.Attach(DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), vertexBufferSize));
    triangleVertexBufferView_.BufferLocation = triangleVertexResource_->GetGPUVirtualAddress();
    triangleVertexBufferView_.SizeInBytes = static_cast<UINT>(vertexBufferSize);
    triangleVertexBufferView_.StrideInBytes = sizeof(TextureVertexData);

    // MapするとCPUアドレスを取得でき、SetTriangleVerticesから直接書き込める。
    HRESULT hr = triangleVertexResource_->Map(
        0, nullptr, reinterpret_cast<void**>(&triangleVertexData_));
    assert(SUCCEEDED(hr));

}

void PrimitiveDrawer::CreateTriangleInstanceResources(uint32_t index) {
    // 同じ三角形へ頂点を再設定した場合は、既存の定数バッファを再利用する。
    if (triangleMaterialResources_[index] != nullptr) {
        return;
    }

    // 実際に登録された三角形だけ、個別のMaterialとWVPを作る。
    const UINT materialBufferSize = (sizeof(Material) + 255) & ~255u;
    triangleMaterialResources_[index].Attach(DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), materialBufferSize));
    HRESULT hr = triangleMaterialResources_[index]->Map(
        0, nullptr, reinterpret_cast<void**>(&triangleMaterialData_[index]));
    assert(SUCCEEDED(hr));
    triangleMaterialData_[index]->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    triangleMaterialData_[index]->enableLighting = 1;

    triangleWvpResources_[index].Attach(DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), sizeof(TransformationMatrix)));
    hr = triangleWvpResources_[index]->Map(
        0, nullptr, reinterpret_cast<void**>(&triangleWvpData_[index]));
    assert(SUCCEEDED(hr));
    triangleWvpData_[index]->WVP = MakeIdentity4x4();
    triangleWvpData_[index]->World = MakeIdentity4x4();
}

void PrimitiveDrawer::CreateDirectionalLightResource() {
    // b2に渡す平行光源の定数バッファを作成する。
    const UINT lightBufferSize = (sizeof(DirectionalLight) + 255) & ~255u;
    directionalLightResource_.Attach(DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), lightBufferSize));

    const HRESULT hr = directionalLightResource_->Map(
        0, nullptr, reinterpret_cast<void**>(&directionalLightData_));
    assert(SUCCEEDED(hr));
    directionalLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLightData_->direction = { 0.0f, -1.0f, 1.0f };
    directionalLightData_->direction.Normalize();
    directionalLightData_->intensity = 1.0f;
}

void PrimitiveDrawer::CreateSphereResources() {
    // 緯度×経度の各マスを2三角形に分割するため、1マスあたり6頂点になる。
    sphereVertexCount_ = kSphereSubdivision * kSphereSubdivision * 6;
    const size_t vertexBufferSize = sizeof(TextureVertexData) * sphereVertexCount_;
    sphereVertexResource_.Attach(DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), vertexBufferSize));
    sphereVertexBufferView_.BufferLocation = sphereVertexResource_->GetGPUVirtualAddress();
    sphereVertexBufferView_.SizeInBytes = static_cast<UINT>(vertexBufferSize);
    sphereVertexBufferView_.StrideInBytes = sizeof(TextureVertexData);

    TextureVertexData* vertexData = nullptr;
    HRESULT hr = sphereVertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    assert(SUCCEEDED(hr));

    constexpr float kPi = 3.1415926535f;
    uint32_t index = 0;
    // 緯度（上下）と経度（左右）を走査して単位球の頂点を生成する。
    for (uint32_t latIndex = 0; latIndex < kSphereSubdivision; ++latIndex) {
        const float lat = -kPi / 2.0f + kPi * latIndex / kSphereSubdivision;
        const float nextLat = -kPi / 2.0f + kPi * (latIndex + 1.0f) / kSphereSubdivision;
        for (uint32_t lonIndex = 0; lonIndex < kSphereSubdivision; ++lonIndex) {
            const float lon = 2.0f * kPi * lonIndex / kSphereSubdivision;
            const float nextLon = 2.0f * kPi * (lonIndex + 1.0f) / kSphereSubdivision;
            // 単位球では、中心から頂点へ向かうベクトルがそのまま法線になる。
            const auto makeVertex = [kPi](float longitude, float latitude) {
                TextureVertexData vertex{};
                vertex.position = {
                    std::cos(latitude) * std::cos(longitude),
                    std::sin(latitude),
                    std::cos(latitude) * std::sin(longitude),
                    1.0f
                };
                vertex.normal = { vertex.position.x, vertex.position.y, vertex.position.z };
                vertex.texcoord = {
                    longitude / (2.0f * kPi),
                    1.0f - (latitude + kPi / 2.0f) / kPi
                };
                return vertex;
            };

            const TextureVertexData a = makeVertex(lon, lat);
            const TextureVertexData b = makeVertex(lon, nextLat);
            const TextureVertexData c = makeVertex(nextLon, lat);
            const TextureVertexData d = makeVertex(nextLon, nextLat);
            vertexData[index++] = a;
            vertexData[index++] = b;
            vertexData[index++] = c;
            vertexData[index++] = c;
            vertexData[index++] = b;
            vertexData[index++] = d;
        }
    }
    // 球の頂点は初期化時に一度だけ書くので、ここでUnmapする。
    sphereVertexResource_->Unmap(0, nullptr);

}

void PrimitiveDrawer::CreateSphereInstanceResources(uint32_t index) {
    if (sphereMaterialResources_[index] != nullptr) {
        return;
    }

    const UINT materialBufferSize = (sizeof(Material) + 255) & ~255u;
    sphereMaterialResources_[index].Attach(DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), materialBufferSize));
    HRESULT hr = sphereMaterialResources_[index]->Map(
        0, nullptr, reinterpret_cast<void**>(&sphereMaterialData_[index]));
    assert(SUCCEEDED(hr));
    sphereMaterialData_[index]->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    sphereMaterialData_[index]->enableLighting = 1;

    sphereWvpResources_[index].Attach(DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), sizeof(TransformationMatrix)));
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

void PrimitiveDrawer::UpdateViewProjectionMatrix() {
    // カメラのワールド行列を逆行列にすると、ワールド→ビュー変換行列になる。
    const Matrix4x4 cameraMatrix = MakeAffineMatrix(
        cameraTransform_.scale, cameraTransform_.rotate, cameraTransform_.translate);
    const Matrix4x4 viewMatrix = Inverse(cameraMatrix);
    const Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(
        0.45f,
        static_cast<float>(windowWidth_) / static_cast<float>(windowHeight_),
        0.1f,
        100.0f);
    viewProjectionMatrix_ = Multiply(viewMatrix, projectionMatrix);
}

void PrimitiveDrawer::UpdateTriangleMatrix(
    uint32_t index,
    const TransformData& transform) {
    const Matrix4x4 worldMatrix = MakeAffineMatrix(
        transform.scale, transform.rotate, transform.translate);
    triangleWvpData_[index]->World = worldMatrix;
    triangleWvpData_[index]->WVP = Multiply(worldMatrix, viewProjectionMatrix_);
}

void PrimitiveDrawer::UpdateSphereMatrix(
    uint32_t index,
    const TransformData& transform) {
    const Matrix4x4 worldMatrix = MakeAffineMatrix(
        transform.scale, transform.rotate, transform.translate);
    sphereWvpData_[index]->World = worldMatrix;
    sphereWvpData_[index]->WVP = Multiply(worldMatrix, viewProjectionMatrix_);
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
    int textureHandle) {
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

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    SetCommonDrawState();
    commandList->IASetVertexBuffers(0, 1, &triangleVertexBufferView_);
    commandList->SetGraphicsRootConstantBufferView(
        0, triangleMaterialResources_[index]->GetGPUVirtualAddress());

    // 指定された三角形のb1（WVP）とt0（Texture）を設定して1個だけ描画する。
    commandList->SetGraphicsRootConstantBufferView(
        1, triangleWvpResources_[index]->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(
        2, textureManager_->GetSrvHandleGPU(textureHandle));
    commandList->DrawInstanced(
        kTriangleVertexCount, 1, index * kTriangleVertexCount, 0);
}

void PrimitiveDrawer::DrawSphere(
    const TransformData& transform,
    const Vector4& color,
    int textureHandle) {
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

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    SetCommonDrawState();
    // 球の頂点バッファ、b0（Material）、b1（WVP）、t0（Texture）を順にバインドする。
    commandList->IASetVertexBuffers(0, 1, &sphereVertexBufferView_);
    commandList->SetGraphicsRootConstantBufferView(
        0, sphereMaterialResources_[index]->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(
        1, sphereWvpResources_[index]->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(
        2, textureManager_->GetSrvHandleGPU(textureHandle));
    commandList->DrawInstanced(sphereVertexCount_, 1, 0, 0);
}
