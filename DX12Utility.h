#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#include "externals/DirectXTex/DirectXTex.h"
#include <cstddef>
#include <iosfwd>
#include <string>

namespace DX12Utility {

// ログファイルとVisual Studioの出力ウィンドウへ同じメッセージを出力する。
void Log(std::ostream& os, const std::string& message);

// Win32 API・DXCで使うUTF-8文字列とUTF-16文字列を相互変換する。
std::wstring ConvertString(const std::string& str);
std::string ConvertString(const std::wstring& str);

// CPUからMapして書き込めるUploadヒープ上のバッファを作成する。
Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(
    ID3D12Device* device,
    size_t sizeInBytes);

// 頂点・インデックスなど、作成後にCPUから変更しないバッファをGPU専用領域へ作る。
// データのコピーはDirectXCommonの共有Upload領域から行うため、ここでは本体だけを作成する。
Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultBufferResource(
    ID3D12Device* device,
    size_t sizeInBytes);

// GPU専用のテクスチャ本体をCOPY_DEST状態で作成する。
Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(
    ID3D12Device* device,
    const DirectX::TexMetadata& metadata);

// テクスチャデータを中間バッファ経由でGPUへコピーする。
// 戻り値の中間リソースは、GPUがコピーを完了するまで保持しなければならない。
Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(
    ID3D12Resource* texture,
    const DirectX::ScratchImage& mipImages,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList);

// CBV/SRV/UAV、RTV、DSVなどのビューを置くディスクリプタヒープを作成する。
Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(
    ID3D12Device* device,
    D3D12_DESCRIPTOR_HEAP_TYPE heapType,
    UINT numDescriptors,
    bool shaderVisible);

// HLSLファイルをDXCでコンパイルし、描画PSOに設定できるバイトコードを返す。
Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(
    const std::wstring& filePath,
    const wchar_t* profile,
    IDxcUtils* dxcUtils,
    IDxcCompiler3* dxcCompiler,
    IDxcIncludeHandler* includeHandler,
    std::ostream& logStream);

} // namespace DX12Utility
