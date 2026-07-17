#include "DX12Utility.h"

#include <cassert>
#include <format>
#include <ostream>
#include <stdexcept>
#include <vector>
#include <windows.h>
#include "externals/DirectXTex/d3dx12.h"

namespace DX12Utility {

void Log(std::ostream& os, const std::string& message) {
    // ファイルへ残すだけでなく、Visual Studioの出力ウィンドウでも確認できるようにする。
    os << message << std::endl;
    OutputDebugStringA((message + '\n').c_str());
}

std::wstring ConvertString(const std::string& str) {
    if (str.empty()) {
        return {};
    }

    // 先に必要な文字数を取得してから、UTF-16用の領域を確保する。
    const int sizeNeeded = MultiByteToWideChar(
        CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    std::wstring result(sizeNeeded, L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded);
    return result;
}

std::string ConvertString(const std::wstring& str) {
    if (str.empty()) {
        return {};
    }

    // こちらも必要なUTF-8バイト数を先に求めてから変換する。
    const int sizeNeeded = WideCharToMultiByte(
        CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0, nullptr, nullptr);
    std::string result(sizeNeeded, '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, nullptr, nullptr);
    return result;
}

Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(
    ID3D12Device* device,
    size_t sizeInBytes) {
    assert(device != nullptr);

    // UploadヒープはCPUからMapして書ける。定数バッファや動的頂点に向く。
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = sizeInBytes;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    const HRESULT hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(resource.GetAddressOf()));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create an upload buffer resource.");
    }
    assert(SUCCEEDED(hr));
    return resource;
}

Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultBufferResource(
    ID3D12Device* device,
    size_t sizeInBytes) {
    assert(device != nullptr);
    if (sizeInBytes == 0) {
        throw std::invalid_argument("Default buffer size must be greater than zero.");
    }

    // DEFAULTヒープはCPUからMapできない代わりに、GPUが効率よく読み出せる。
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = sizeInBytes;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    const HRESULT hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(resource.GetAddressOf()));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create a default buffer resource.");
    }
    return resource;
}

Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(
    ID3D12Device* device,
    const DirectX::TexMetadata& metadata) {
    assert(device != nullptr);

    // テクスチャの幅・高さ・ミップ数・形式は、読み込んだ画像のメタデータを使う。
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Width = static_cast<UINT>(metadata.width);
    resourceDesc.Height = static_cast<UINT>(metadata.height);
    resourceDesc.MipLevels = static_cast<UINT16>(metadata.mipLevels);
    resourceDesc.DepthOrArraySize = static_cast<UINT16>(metadata.arraySize);
    resourceDesc.Format = metadata.format;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(metadata.dimension);
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES heapProperties{};
    // DefaultヒープはGPU専用。COPY_DEST状態で作り、後でUploadTextureDataで内容をコピーする。
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    const HRESULT hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(resource.GetAddressOf()));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create a texture resource.");
    }
    assert(SUCCEEDED(hr));
    return resource;
}

Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(
    ID3D12Resource* texture,
    const DirectX::ScratchImage& mipImages,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList) {
    assert(texture != nullptr);
    assert(device != nullptr);
    assert(commandList != nullptr);

    // ミップマップを含む各画像を、D3D12がコピーできるサブリソース配列へ変換する。
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    DirectX::PrepareUpload(
        device,
        mipImages.GetImages(),
        mipImages.GetImageCount(),
        mipImages.GetMetadata(),
        subresources);

    // GPUテクスチャへ直接CPU書き込みはできないため、Uploadヒープの中間バッファを用意する。
    const uint64_t intermediateSize =
        GetRequiredIntermediateSize(texture, 0, static_cast<UINT>(subresources.size()));
    const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(intermediateSize);

    Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource;
    const HRESULT hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(intermediateResource.GetAddressOf()));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create a texture upload resource.");
    }
    assert(SUCCEEDED(hr));

    // コピーコマンドをコマンドリストへ積む（この時点ではまだGPUは実行していない）。
    UpdateSubresources(
        commandList,
        texture,
        intermediateResource.Get(),
        0,
        0,
        static_cast<UINT>(subresources.size()),
        subresources.data());

    // コピー完了後、ピクセルシェーダーから読める状態へ遷移させる。
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        texture,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList->ResourceBarrier(1, &barrier);
    return intermediateResource;
}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(
    ID3D12Device* device,
    D3D12_DESCRIPTOR_HEAP_TYPE heapType,
    UINT numDescriptors,
    bool shaderVisible) {
    assert(device != nullptr);

    // shaderVisible=trueのヒープだけが、SetGraphicsRootDescriptorTableでシェーダーへ渡せる。
    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
    descriptorHeapDesc.Type = heapType;
    descriptorHeapDesc.NumDescriptors = numDescriptors;
    descriptorHeapDesc.Flags = shaderVisible
        ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
        : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    const HRESULT hr = device->CreateDescriptorHeap(
        &descriptorHeapDesc,
        IID_PPV_ARGS(descriptorHeap.GetAddressOf()));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create a descriptor heap.");
    }
    assert(SUCCEEDED(hr));
    return descriptorHeap;
}

Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(
    const std::wstring& filePath,
    const wchar_t* profile,
    IDxcUtils* dxcUtils,
    IDxcCompiler3* dxcCompiler,
    IDxcIncludeHandler* includeHandler,
    std::ostream& logStream) {
    assert(dxcUtils != nullptr);
    assert(dxcCompiler != nullptr);
    assert(includeHandler != nullptr);

    // コンパイル対象とプロファイルをログに出して、HLSLエラーの調査をしやすくする。
    Log(logStream, ConvertString(std::format(
        L"Begin CompileShader, path:{}, profile:{}", filePath, profile)));

    Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource;
    HRESULT hr = dxcUtils->LoadFile(
        filePath.c_str(), nullptr, shaderSource.GetAddressOf());
    assert(SUCCEEDED(hr));

    // 読み込んだHLSLのメモリ範囲をDXCへ渡す。
    DxcBuffer shaderSourceBuffer{};
    shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
    shaderSourceBuffer.Size = shaderSource->GetBufferSize();
    shaderSourceBuffer.Encoding = DXC_CP_UTF8;

    // -Zpr: 行優先行列。C++側のMatrix4x4を転置せずにHLSLへ渡せるようにする。
    LPCWSTR arguments[] = {
        filePath.c_str(),
        L"-E", L"main",
        L"-T", profile,
        L"-Zi", L"-Qembed_debug",
        L"-Od",
        L"-Zpr",
    };

    Microsoft::WRL::ComPtr<IDxcResult> shaderResult;
    hr = dxcCompiler->Compile(
        &shaderSourceBuffer,
        arguments,
        _countof(arguments),
        includeHandler,
        IID_PPV_ARGS(shaderResult.GetAddressOf()));
    assert(SUCCEEDED(hr));

    // DXCは成功時でも警告を返すことがある。文字列があればログへ出す。
    Microsoft::WRL::ComPtr<IDxcBlobUtf8> shaderError;
    shaderResult->GetOutput(
        DXC_OUT_ERRORS,
        IID_PPV_ARGS(shaderError.GetAddressOf()),
        nullptr);
    if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
        Log(logStream, shaderError->GetStringPointer());
        assert(false);
        return {};
    }

    Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob;
    hr = shaderResult->GetOutput(
        DXC_OUT_OBJECT,
        IID_PPV_ARGS(shaderBlob.GetAddressOf()),
        nullptr);
    assert(SUCCEEDED(hr));
    Log(logStream, ConvertString(std::format(
        L"Compile Succeeded, path:{}, profile:{}", filePath, profile)));

    return shaderBlob;
}

} // namespace DX12Utility
