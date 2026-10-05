#pragma once
#include <d3d12.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <wrl.h>
#include <vector>

namespace DirectX {
class ScratchImage;
}

// 画像ファイルをGPUテクスチャへ変換し、SRVディスクリプタの番号で管理するクラス。
class TextureManager {
public:
    // SRVヒープを作成する。最初の1枠はImGui専用に予約する。
    void Initialize(ID3D12Device* device);
    // 同じファイルはキャッシュから返し、重複してGPUメモリを使わない。
    int LoadTexture(
        const std::string& filePath,
        ID3D12GraphicsCommandList* commandList,
        bool useSrgb = true);
    // GLBなどに埋め込まれたPNG／JPEGデータをファイルへ展開せず読み込む。
    int LoadTextureFromMemory(
        const std::string& cacheKey,
        const uint8_t* encodedData,
        size_t encodedSize,
        ID3D12GraphicsCommandList* commandList,
        bool useSrgb = true);
    // Assimpが展開済みRGBAとして返した埋め込み画像用。
    int LoadTextureFromRgbaMemory(
        const std::string& cacheKey,
        const uint8_t* rgbaData,
        uint32_t width,
        uint32_t height,
        ID3D12GraphicsCommandList* commandList,
        bool useSrgb = true);

    // 描画時にDescriptorHeapをセットするため
    ID3D12DescriptorHeap* GetSrvHeap() const { return srvDescriptorHeap_.Get(); }

    // 描画時に使用するGPU上のハンドルを取得する
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU(int textureIndex) const;

    // Scene保存では一時的なハンドル番号ではなく、元画像のパスを記録する。
    // 無効なハンドルの場合は空文字列を返す。
    const std::string& GetTexturePath(int textureIndex) const;

    // 今回送信したUploadリソースへフェンス値を付け、GPU完了済みのものだけ解放する。
    void ReleaseIntermediateResources(
        uint64_t submittedFenceValue,
        uint64_t completedFenceValue);

    // ImGui font + Editor Viewport用の予約領域を除き、ゲーム用テクスチャは128枚まで読み込める。
    static constexpr size_t kMaxTextures = 128;
    static constexpr UINT kEngineDescriptorCount = 6;
    static constexpr UINT kTextureDescriptorOffset = kEngineDescriptorCount;

    D3D12_CPU_DESCRIPTOR_HANDLE GetImGuiSrvHandleCPU() const {
        return srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetImGuiSrvHandleGPU() const {
        return srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetEditorViewportSrvHandleCPU() const {
        D3D12_CPU_DESCRIPTOR_HANDLE handle =
            srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += descriptorSize_;
        return handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetEditorViewportSrvHandleGPU() const {
        D3D12_GPU_DESCRIPTOR_HANDLE handle =
            srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += descriptorSize_;
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetDirectionalShadowSrvHandleCPU() const {
        D3D12_CPU_DESCRIPTOR_HANDLE handle =
            srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += descriptorSize_ * 2;
        return handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetDirectionalShadowSrvHandleGPU() const {
        D3D12_GPU_DESCRIPTOR_HANDLE handle =
            srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += descriptorSize_ * 2;
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetSceneHdrSrvHandleCPU() const {
        D3D12_CPU_DESCRIPTOR_HANDLE handle =
            srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += descriptorSize_ * 3;
        return handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetSceneHdrSrvHandleGPU() const {
        D3D12_GPU_DESCRIPTOR_HANDLE handle =
            srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += descriptorSize_ * 3;
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetPointShadowSrvHandleCPU() const {
        D3D12_CPU_DESCRIPTOR_HANDLE handle =
            srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += descriptorSize_ * 4;
        return handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetPointShadowSrvHandleGPU() const {
        D3D12_GPU_DESCRIPTOR_HANDLE handle =
            srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += descriptorSize_ * 4;
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetSceneDepthSrvHandleCPU() const {
        D3D12_CPU_DESCRIPTOR_HANDLE handle =
            srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += descriptorSize_ * 5;
        return handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetSceneDepthSrvHandleGPU() const {
        D3D12_GPU_DESCRIPTOR_HANDLE handle =
            srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += descriptorSize_ * 5;
        return handle;
    }

private:
    int CreateTextureFromImage(
        const std::string& cacheKey,
        const std::string& sourcePath,
        const DirectX::ScratchImage& mipImages,
        ID3D12GraphicsCommandList* commandList);

    struct IntermediateResourceBatch {
        uint64_t fenceValue = 0;
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> resources;
    };

    // TextureManagerはデバイスを所有せず、Graphics/DirectXCommonが寿命を管理する。
    ID3D12Device* device_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;

    // 重複読み込み防止用のキャッシュ
    std::unordered_map<std::string, int> textureCache_;
    // ハンドルから保存用ファイルパスを逆引きする配列。
    std::vector<std::string> texturePaths_;

    // テクスチャリソースの配列
    Microsoft::WRL::ComPtr<ID3D12Resource> textureResources_[kMaxTextures];
    uint32_t textureCount_ = 0;

    // データ転送用の中間リソース（コマンド実行完了まで保持する必要がある）
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> intermediateResources_;
    std::vector<IntermediateResourceBatch> submittedIntermediateResourceBatches_;

    // ディスクリプタを1個進めるためのバイト数（GPUごとに異なる）。
    UINT descriptorSize_ = 0;
};
