#include "TextureManager.h"
#include "DX12Utility.h"

#include <algorithm>
#include <cassert>
#include <utility>
#include "externals/DirectXTex/DirectXTex.h"

namespace {

DirectX::ScratchImage LoadTextureFile(const std::string& filePath) {
    // WICを使ってpngなどを読み込み、GPUで扱える画像データに変換する。
    DirectX::ScratchImage image{};
    const std::wstring filePathW = DX12Utility::ConvertString(filePath);

    // WIC_FLAGS_FORCE_SRGBにより、色テクスチャをsRGBとして扱う。
    HRESULT hr = DirectX::LoadFromWICFile(
        filePathW.c_str(),
        DirectX::WIC_FLAGS_FORCE_SRGB,
        nullptr,
        image);
    assert(SUCCEEDED(hr));

    // 縮小表示でも粗くなりにくいように、ミップマップを自動生成する。
    DirectX::ScratchImage mipImages{};
    hr = DirectX::GenerateMipMaps(
        image.GetImages(),
        image.GetImageCount(),
        image.GetMetadata(),
        DirectX::TEX_FILTER_SRGB,
        0,
        mipImages);
    assert(SUCCEEDED(hr));

    return mipImages;
}

} // namespace

void TextureManager::Initialize(ID3D12Device* device) {
    assert(device != nullptr);

    device_ = device;
    // CPU/GPUハンドルを次のSRVへ進める際に必要な間隔を取得する。
    descriptorSize_ = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // ImGui用1枠 + ゲーム用テクスチャkMaxTextures枠を、1つのshaderVisibleヒープに確保する。
    srvDescriptorHeap_ = DX12Utility::CreateDescriptorHeap(
        device_,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        static_cast<UINT>(kMaxTextures) + kImGuiDescriptorCount,
        true);
}

int TextureManager::LoadTexture(
    const std::string& filePath,
    ID3D12GraphicsCommandList* commandList) {
    assert(device_ != nullptr);
    assert(commandList != nullptr);

    // 一度読み込んだ画像は、同じ番号を返して再読み込みを避ける。
    const auto cacheIt = textureCache_.find(filePath);
    if (cacheIt != textureCache_.end()) {
        return cacheIt->second;
    }

    if (textureCount_ >= kMaxTextures) {
        assert(false && "Texture loading limit exceeded.");
        return -1;
    }

    // 1. 画像を読み込み、ミップマップを含むCPU側データを作る。
    DirectX::ScratchImage mipImages = LoadTextureFile(filePath);
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();

    // 2. GPU専用テクスチャを作成し、中間バッファからコピーするコマンドを記録する。
    textureResources_[textureCount_] =
        DX12Utility::CreateTextureResource(device_, metadata);

    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource =
        DX12Utility::UploadTextureData(
        textureResources_[textureCount_].Get(),
        mipImages,
        device_,
        commandList);
    // コピーがGPUで完了するまで中間バッファを生かしておく。
    intermediateResources_.push_back(std::move(intermediateResource));

    // 3. ImGui予約枠の次から、テクスチャ番号に対応するSRVを作成する。
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU =
        srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
    srvHandleCPU.ptr += descriptorSize_ *
        (kTextureDescriptorOffset + textureCount_);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = static_cast<UINT>(metadata.mipLevels);

    device_->CreateShaderResourceView(
        textureResources_[textureCount_].Get(),
        &srvDesc,
        srvHandleCPU);

    const int textureIndex = static_cast<int>(textureCount_);
    textureCache_[filePath] = textureIndex;
    ++textureCount_;
    return textureIndex;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(int textureIndex) const {
    assert(textureIndex >= 0 && textureIndex < static_cast<int>(textureCount_));

    // 描画時はCPUハンドルではなく、このGPUハンドルをルートディスクリプタテーブルへ渡す。
    D3D12_GPU_DESCRIPTOR_HANDLE handle =
        srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += descriptorSize_ *
        (kTextureDescriptorOffset + textureIndex);
    return handle;
}

void TextureManager::ReleaseIntermediateResources(
    uint64_t submittedFenceValue,
    uint64_t completedFenceValue) {
    // 今回のコマンドリストに含まれたUploadリソースへ、完了判定用のフェンス値を付ける。
    if (!intermediateResources_.empty()) {
        IntermediateResourceBatch batch{};
        batch.fenceValue = submittedFenceValue;
        batch.resources = std::move(intermediateResources_);
        submittedIntermediateResourceBatches_.push_back(std::move(batch));
        intermediateResources_.clear();
    }

    // GPUが該当フェンスまで完了したバッチだけを破棄する。
    // 未完了のUploadリソースは次フレーム以降も保持される。
    const auto removeBegin = std::remove_if(
        submittedIntermediateResourceBatches_.begin(),
        submittedIntermediateResourceBatches_.end(),
        [completedFenceValue](const IntermediateResourceBatch& batch) {
            return batch.fenceValue <= completedFenceValue;
        });
    submittedIntermediateResourceBatches_.erase(
        removeBegin, submittedIntermediateResourceBatches_.end());
}
