#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

// 1フレームだけ使用する頂点・定数データを、Uploadヒープから切り出した結果。
// cpuAddressへ値を書き、gpuAddressをVBVやルートCBVへ設定する。
struct DynamicBufferAllocation {
    void* cpuAddress = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
    ID3D12Resource* resource = nullptr;
    size_t offset = 0;
    size_t sizeInBytes = 0;
};

// DirectX 12の土台となるデバイス、コマンドキュー、スワップチェーン、同期をまとめるクラス。
class DirectXCommon {
public:
    // SwapChainのバックバッファ数と、CPUから書き込むフレームリソース数を一致させる。
    static constexpr uint32_t kFrameCount = 2;

    DirectXCommon() = default;
    ~DirectXCommon();

    // 初期化
    void Initialize(HWND hWnd, int32_t width, int32_t height);

    // BeginDraw: バックバッファを描画可能状態にしてクリアする。
    // EndDraw  : GPUへコマンドを送信し、画面表示後に次フレーム用へリセットする。
    void BeginDraw();
    void EndDraw();

    // Off-screen rendering returns to these SwapChain bindings before ImGui.
    void BindSwapChainRenderTarget();
    void SetViewportAndScissor(uint32_t width, uint32_t height);

    // ウィンドウのクライアント領域に合わせて、SwapChainと深度バッファを作り直す。
    void Resize(uint32_t width, uint32_t height);

    // GPUの完了待ち
    void WaitForGpu();

    // 現在記録中のフレーム専用Uploadヒープから領域を確保する。
    // 定数バッファはalignment=256、頂点データはalignment=16を指定する。
    DynamicBufferAllocation AllocateDynamicBuffer(
        size_t sizeInBytes,
        size_t alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

    // 変更しない頂点・インデックスデータをGPU専用のDEFAULTヒープへ転送する。
    // 一時Uploadリソースを毎回作らず、現在フレームの共有Upload領域をコピー元に使う。
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateStaticBufferResource(
        const void* data,
        size_t sizeInBytes,
        D3D12_RESOURCE_STATES finalState);

    // ゲッター (Graphicsクラスなどで使用するため)
    ID3D12Device* GetDevice() const { return device_.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() const { return commandList_.Get(); }
    IDXGISwapChain4* GetSwapChain() const { return swapChain_.Get(); }
    DXGI_FORMAT GetBackBufferFormat() const { return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; }
    DXGI_FORMAT GetDepthBufferFormat() const { return DXGI_FORMAT_D24_UNORM_S8_UINT; }
    ID3D12Resource* GetDepthBuffer() const { return depthBuffer_.Get(); }
    ID3D12CommandQueue* GetCommandQueue() const { return commandQueue_.Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView() const {
        return dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
    }
    ID3D12CommandAllocator* GetCommandAllocator() const {
        return frameResources_[currentFrameIndex_].commandAllocator.Get();
    }
    uint32_t GetCurrentFrameIndex() const { return currentFrameIndex_; }
    uint64_t GetLastSubmittedFenceValue() const { return lastSubmittedFenceValue_; }
    uint64_t GetCompletedFenceValue() const {
        return fence_ != nullptr ? fence_->GetCompletedValue() : 0;
    }

private:
    void CreateRenderTargetViews();
    void CreateDepthStencilResource();

    // CPUが次に書き込むフレーム領域をGPUが使い終えるまで待つ。
    // 毎フレーム全GPUを待つのではなく、同じ領域を再利用するときだけ待機する。
    void WaitForFrame(uint32_t frameIndex);

    static constexpr size_t kDynamicBufferSizePerFrame = 16u * 1024u * 1024u;

    struct FrameResource {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
        Microsoft::WRL::ComPtr<ID3D12Resource> dynamicBuffer;
        uint8_t* mappedDynamicBuffer = nullptr;
        size_t dynamicBufferOffset = 0;
        uint64_t fenceValue = 0;
    };

    // DirectX 12の中心となるデバイス。リソースやビューの作成に使う。
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
    FrameResource frameResources_[kFrameCount];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;

    // RTV (Render Target View) と DSV (Depth Stencil View)
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources_[kFrameCount];
    Microsoft::WRL::ComPtr<ID3D12Resource> depthBuffer_;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles_[kFrameCount]{};

    // フェンス値で「GPUがどこまでコマンドを完了したか」を確認する。
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    HANDLE fenceEvent_ = nullptr;
    uint64_t fenceValue_ = 0;
    uint64_t lastSubmittedFenceValue_ = 0;
    uint32_t currentFrameIndex_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool isDrawing_ = false;
};
