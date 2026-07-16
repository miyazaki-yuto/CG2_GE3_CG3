#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <cstdint>
#include <stdexcept>

// DirectX 12の土台となるデバイス、コマンドキュー、スワップチェーン、同期をまとめるクラス。
class DirectXCommon {
public:
    DirectXCommon() = default;
    ~DirectXCommon();

    // 初期化
    void Initialize(HWND hWnd, int32_t width, int32_t height);

    // BeginDraw: バックバッファを描画可能状態にしてクリアする。
    // EndDraw  : GPUへコマンドを送信し、画面表示後に次フレーム用へリセットする。
    void BeginDraw();
    void EndDraw();

    // GPUの完了待ち
    void WaitForGpu();

    // ゲッター (Graphicsクラスなどで使用するため)
    ID3D12Device* GetDevice() const { return device_.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() const { return commandList_.Get(); }
    IDXGISwapChain4* GetSwapChain() const { return swapChain_.Get(); }
    DXGI_FORMAT GetBackBufferFormat() const { return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; }
    DXGI_FORMAT GetDepthBufferFormat() const { return DXGI_FORMAT_D24_UNORM_S8_UINT; }
    ID3D12CommandQueue* GetCommandQueue() const { return commandQueue_.Get(); }
    ID3D12CommandAllocator* GetCommandAllocator() const { return commandAllocator_.Get(); }

private:
    // DirectX 12の中心となるデバイス。リソースやビューの作成に使う。
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;

    // RTV (Render Target View) と DSV (Depth Stencil View)
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources_[2];
    Microsoft::WRL::ComPtr<ID3D12Resource> depthBuffer_;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles_[2]{};

    // フェンス値で「GPUがどこまでコマンドを完了したか」を確認する。
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    HANDLE fenceEvent_ = nullptr;
    uint64_t fenceValue_ = 0;
};
