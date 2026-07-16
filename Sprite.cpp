#include "Sprite.h"

#include "DX12Utility.h"
#include "DirectXCommon.h"
#include "Matrix4x4.h"
#include "TextureManager.h"

#include <cassert>

Sprite::~Sprite() {
    // InitializeでMapした各Uploadリソースを対応するUnmapで閉じる。
    if (vertexData_ != nullptr && vertexResource_ != nullptr) {
        vertexResource_->Unmap(0, nullptr);
    }
    for (uint32_t i = 0; i < kMaxSpriteCount; ++i) {
        if (materialData_[i] != nullptr && materialResources_[i] != nullptr) {
            materialResources_[i]->Unmap(0, nullptr);
        }
        if (wvpData_[i] != nullptr && wvpResources_[i] != nullptr) {
            wvpResources_[i]->Unmap(0, nullptr);
        }
    }
    if (directionalLightData_ != nullptr && directionalLightResource_ != nullptr) {
        directionalLightResource_->Unmap(0, nullptr);
    }
}

void Sprite::Initialize(
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
    assert(windowWidth > 0);
    assert(windowHeight > 0);

    dxCommon_ = dxCommon;
    textureManager_ = textureManager;
    rootSignature_ = rootSignature;
    pipelineState_ = pipelineState;
    windowWidth_ = windowWidth;
    windowHeight_ = windowHeight;

    // 四角形を表す6頂点分のバッファを作る。
    const size_t vertexBufferSize = sizeof(TextureVertexData) * kVertexCount;
    vertexResource_.Attach(DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), vertexBufferSize));
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = static_cast<UINT>(vertexBufferSize);
    vertexBufferView_.StrideInBytes = sizeof(TextureVertexData);

    HRESULT hr = vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));
    assert(SUCCEEDED(hr));

    // 0〜1の単位矩形。Transformのscaleを幅・高さ、translateを表示位置として使う。
    const TextureVertexData defaultVertices[kVertexCount] = {
        { { 0.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
        { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
        { { 1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
        { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
        { { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
        { { 1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
    };
    SetVertices(defaultVertices);

    // Object3d用シェーダーはb2を宣言しているので、Spriteでも有効なCBVを設定できるようにする。
    const UINT lightBufferSize = (sizeof(DirectionalLight) + 255) & ~255u;
    directionalLightResource_.Attach(DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), lightBufferSize));
    hr = directionalLightResource_->Map(
        0, nullptr, reinterpret_cast<void**>(&directionalLightData_));
    assert(SUCCEEDED(hr));
    directionalLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLightData_->direction = { 0.0f, -1.0f, 1.0f };
    directionalLightData_->direction.Normalize();
    directionalLightData_->intensity = 1.0f;

}

void Sprite::SetVertices(const TextureVertexData* vertices) {
    assert(vertices != nullptr);
    assert(vertexData_ != nullptr);

    // Map済みバッファなので、コピーした内容を次のDrawでそのまま使える。
    for (UINT i = 0; i < kVertexCount; ++i) {
        vertexData_[i] = vertices[i];
    }
}

void Sprite::BeginFrame() {
    spriteDrawCount_ = 0;
}

void Sprite::CreateInstanceResources(uint32_t index) {
    if (materialResources_[index] != nullptr) {
        return;
    }

    const UINT materialBufferSize = (sizeof(Material) + 255) & ~255u;
    materialResources_[index].Attach(DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), materialBufferSize));
    HRESULT hr = materialResources_[index]->Map(
        0, nullptr, reinterpret_cast<void**>(&materialData_[index]));
    assert(SUCCEEDED(hr));
    materialData_[index]->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData_[index]->enableLighting = 0;

    wvpResources_[index].Attach(DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), sizeof(TransformationMatrix)));
    hr = wvpResources_[index]->Map(
        0, nullptr, reinterpret_cast<void**>(&wvpData_[index]));
    assert(SUCCEEDED(hr));
    wvpData_[index]->WVP = MakeIdentity4x4();
    wvpData_[index]->World = MakeIdentity4x4();
}

void Sprite::UpdateMatrix(
    uint32_t index,
    const TransformData& transform) {
    assert(wvpData_[index] != nullptr);

    // scale・rotate・translateを合成して、スプライトのワールド行列を作る。
    const Matrix4x4 worldMatrix = MakeAffineMatrix(
        transform.scale,
        transform.rotate,
        transform.translate);
    // 透視投影ではなく正射影を使うので、(0,0)〜(画面幅,画面高)をピクセル座標として扱える。
    const Matrix4x4 orthographicMatrix = MakeOrthographicMatrix(
        0.0f,
        0.0f,
        static_cast<float>(windowWidth_),
        static_cast<float>(windowHeight_),
        0.0f,
        100.0f);

    wvpData_[index]->World = worldMatrix;
    wvpData_[index]->WVP = Multiply(worldMatrix, orthographicMatrix);
}

void Sprite::Draw(
    const TransformData& transform,
    const Vector4& color,
    int textureHandle) {
    assert(dxCommon_ != nullptr);
    assert(textureManager_ != nullptr);
    assert(rootSignature_ != nullptr);
    assert(pipelineState_ != nullptr);
    assert(textureHandle >= 0);

    if (spriteDrawCount_ >= kMaxSpriteCount) {
        assert(false && "Sprite draw count exceeded kMaxSpriteCount.");
        return;
    }
    const uint32_t index = spriteDrawCount_++;
    CreateInstanceResources(index);

    // Draw順から選ばれた専用スロットへ、今回の行列と色を反映する。
    UpdateMatrix(index, transform);
    materialData_[index]->color = { color.x, color.y, color.z, color.w };

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap* descriptorHeaps[] = { textureManager_->GetSrvHeap() };

    // ルートパラメータの番号は3D描画と共通だが、PSOはSprite専用設定を使う。
    commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipelineState_.Get());
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // b0=Material、b1=WVP、t0=Texture の対応でシェーダーへデータを渡す。
    commandList->SetGraphicsRootConstantBufferView(
        0, materialResources_[index]->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(
        1, wvpResources_[index]->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(
        2, textureManager_->GetSrvHandleGPU(textureHandle));
    commandList->SetGraphicsRootConstantBufferView(
        3, directionalLightResource_->GetGPUVirtualAddress());

    commandList->DrawInstanced(kVertexCount, 1, 0, 0);
}
