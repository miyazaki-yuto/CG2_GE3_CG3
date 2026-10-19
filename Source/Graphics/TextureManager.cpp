#include "TextureManager.h"
#include "DX12Utility.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <utility>
#include "externals/DirectXTex/DirectXTex.h"

namespace {

DirectX::ScratchImage LoadTextureFile(
    const std::string& filePath,
    bool useSrgb,
    bool& succeeded) {
    succeeded = false;
    // WICを使ってpngなどを読み込み、GPUで扱える画像データに変換する。
    DirectX::ScratchImage image{};
    const std::wstring filePathW = DX12Utility::ConvertString(filePath);

    // WIC_FLAGS_FORCE_SRGBにより、色テクスチャをsRGBとして扱う。
    HRESULT hr = DirectX::LoadFromWICFile(
        filePathW.c_str(),
        useSrgb
            ? DirectX::WIC_FLAGS_FORCE_SRGB
            : DirectX::WIC_FLAGS_IGNORE_SRGB,
        nullptr,
        image);
    if (FAILED(hr)) {
        return {};
    }

    // 縮小表示でも粗くなりにくいように、ミップマップを自動生成する。
    DirectX::ScratchImage mipImages{};
    hr = DirectX::GenerateMipMaps(
        image.GetImages(),
        image.GetImageCount(),
        image.GetMetadata(),
        useSrgb ? DirectX::TEX_FILTER_SRGB : DirectX::TEX_FILTER_DEFAULT,
        0,
        mipImages);
    if (FAILED(hr)) {
        return {};
    }

    succeeded = true;
    return mipImages;
}

DirectX::ScratchImage LoadTextureMemory(
    const uint8_t* encodedData,
    size_t encodedSize,
    bool useSrgb,
    bool& succeeded) {
    succeeded = false;
    if (encodedData == nullptr || encodedSize == 0) {
        return {};
    }

    DirectX::ScratchImage image{};
    HRESULT hr = DirectX::LoadFromWICMemory(
        encodedData,
        encodedSize,
        useSrgb
            ? DirectX::WIC_FLAGS_FORCE_SRGB
            : DirectX::WIC_FLAGS_IGNORE_SRGB,
        nullptr,
        image);
    if (FAILED(hr)) {
        return {};
    }

    DirectX::ScratchImage mipImages{};
    hr = DirectX::GenerateMipMaps(
        image.GetImages(),
        image.GetImageCount(),
        image.GetMetadata(),
        useSrgb ? DirectX::TEX_FILTER_SRGB : DirectX::TEX_FILTER_DEFAULT,
        0,
        mipImages);
    if (FAILED(hr)) {
        return {};
    }

    succeeded = true;
    return mipImages;
}

DirectX::ScratchImage LoadRgbaTextureMemory(
    const uint8_t* rgbaData,
    uint32_t width,
    uint32_t height,
    bool useSrgb,
    bool& succeeded) {
    succeeded = false;
    if (rgbaData == nullptr || width == 0 || height == 0) {
        return {};
    }

    DirectX::ScratchImage image{};
    HRESULT hr = image.Initialize2D(
        useSrgb
            ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
            : DXGI_FORMAT_R8G8B8A8_UNORM,
        width,
        height,
        1,
        1);
    if (FAILED(hr) || image.GetImageCount() == 0) {
        return {};
    }
    const DirectX::Image* destination = image.GetImage(0, 0, 0);
    if (destination == nullptr) {
        return {};
    }
    const size_t sourceRowPitch = static_cast<size_t>(width) * 4;
    for (uint32_t row = 0; row < height; ++row) {
        std::memcpy(
            destination->pixels + destination->rowPitch * row,
            rgbaData + sourceRowPitch * row,
            sourceRowPitch);
    }

    DirectX::ScratchImage mipImages{};
    hr = DirectX::GenerateMipMaps(
        image.GetImages(),
        image.GetImageCount(),
        image.GetMetadata(),
        useSrgb ? DirectX::TEX_FILTER_SRGB : DirectX::TEX_FILTER_DEFAULT,
        0,
        mipImages);
    if (FAILED(hr)) {
        return {};
    }

    succeeded = true;
    return mipImages;
}

} // namespace

void TextureManager::Initialize(ID3D12Device* device) {
    assert(device != nullptr);

    device_ = device;
    // CPU/GPUハンドルを次のSRVへ進める際に必要な間隔を取得する。
    descriptorSize_ = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // ImGui font/Editor Viewport用2枠 + ゲーム用テクスチャを1つのshaderVisibleヒープに確保する。
    srvDescriptorHeap_ = DX12Utility::CreateDescriptorHeap(
        device_,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        static_cast<UINT>(kMaxTextures) + kEngineDescriptorCount,
        true);
}

int TextureManager::LoadTexture(
    const std::string& filePath,
    ID3D12GraphicsCommandList* commandList,
    bool useSrgb) {
    assert(device_ != nullptr);
    assert(commandList != nullptr);

    // 一度読み込んだ画像は、同じ番号を返して再読み込みを避ける。
    // 同じ画像でもColor TextureとNormal Mapでは色空間が異なるため、別SRVとして管理する。
    const std::string cacheKey = filePath + (useSrgb ? "|srgb" : "|linear");
    const auto cacheIt = textureCache_.find(cacheKey);
    if (cacheIt != textureCache_.end()) {
        return cacheIt->second;
    }

    if (textureCount_ >= kMaxTextures) {
        return -1;
    }

    // 1. 画像を読み込み、ミップマップを含むCPU側データを作る。
    bool textureLoaded = false;
    DirectX::ScratchImage mipImages =
        LoadTextureFile(filePath, useSrgb, textureLoaded);
    if (!textureLoaded) {
        return -1;
    }
    return CreateTextureFromImage(
        cacheKey, filePath, mipImages, commandList);
}

int TextureManager::LoadTextureFromMemory(
    const std::string& cacheKey,
    const uint8_t* encodedData,
    size_t encodedSize,
    ID3D12GraphicsCommandList* commandList,
    bool useSrgb) {
    assert(device_ != nullptr);
    assert(commandList != nullptr);

    const std::string typedCacheKey =
        cacheKey + (useSrgb ? "|srgb" : "|linear");
    const auto cacheIt = textureCache_.find(typedCacheKey);
    if (cacheIt != textureCache_.end()) {
        return cacheIt->second;
    }
    if (textureCount_ >= kMaxTextures) {
        return -1;
    }

    bool textureLoaded = false;
    DirectX::ScratchImage mipImages = LoadTextureMemory(
        encodedData, encodedSize, useSrgb, textureLoaded);
    if (!textureLoaded) {
        return -1;
    }
    return CreateTextureFromImage(
        typedCacheKey, cacheKey, mipImages, commandList);
}

int TextureManager::LoadTextureFromRgbaMemory(
    const std::string& cacheKey,
    const uint8_t* rgbaData,
    uint32_t width,
    uint32_t height,
    ID3D12GraphicsCommandList* commandList,
    bool useSrgb) {
    assert(device_ != nullptr);
    assert(commandList != nullptr);

    const std::string typedCacheKey =
        cacheKey + (useSrgb ? "|srgb" : "|linear");
    const auto cacheIt = textureCache_.find(typedCacheKey);
    if (cacheIt != textureCache_.end()) {
        return cacheIt->second;
    }
    if (textureCount_ >= kMaxTextures) {
        return -1;
    }

    bool textureLoaded = false;
    DirectX::ScratchImage mipImages = LoadRgbaTextureMemory(
        rgbaData, width, height, useSrgb, textureLoaded);
    if (!textureLoaded) {
        return -1;
    }
    return CreateTextureFromImage(
        typedCacheKey, cacheKey, mipImages, commandList);
}

int TextureManager::CreateTextureFromImage(
    const std::string& cacheKey,
    const std::string& sourcePath,
    const DirectX::ScratchImage& mipImages,
    ID3D12GraphicsCommandList* commandList) {
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    textureResources_[textureCount_] =
        DX12Utility::CreateTextureResource(device_, metadata);

    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource =
        DX12Utility::UploadTextureData(
            textureResources_[textureCount_].Get(),
            mipImages,
            device_,
            commandList);
    intermediateResources_.push_back(std::move(intermediateResource));

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
    textureCache_[cacheKey] = textureIndex;
    texturePaths_.push_back(sourcePath);
    ++textureCount_;
    return textureIndex;
}

const std::string& TextureManager::GetTexturePath(int textureIndex) const {
    static const std::string kEmptyPath;
    if (textureIndex < 0 ||
        textureIndex >= static_cast<int>(texturePaths_.size())) {
        return kEmptyPath;
    }
    return texturePaths_[static_cast<size_t>(textureIndex)];
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
