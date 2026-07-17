#include "DirectXCommon.h"
#include "DX12Utility.h"
#include <cassert>
#include"externals/DirectXTex/d3dx12.h"

DirectXCommon::~DirectXCommon() {
    // GPU実行中のコマンドがリソースを参照している可能性があるため、先に完了を待つ。
    WaitForGpu();
    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}

void DirectXCommon::Initialize(HWND hWnd, int32_t width, int32_t height) {
    HRESULT hr = S_FALSE;

#ifdef _DEBUG
    // デバッグレイヤーを有効にすると、D3D12の使い方の誤りを出力ウィンドウで確認できる。
    Microsoft::WRL::ComPtr<ID3D12Debug1> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        debugController->SetEnableGPUBasedValidation(TRUE);
    }
#endif

    hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory_));
    assert(SUCCEEDED(hr));

    // アダプタの選択: ソフトウェアアダプタを避け、高性能GPUを優先する。
    Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter;
    for (UINT i = 0; dxgiFactory_->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC3 adapterDesc{};
        hr = useAdapter->GetDesc3(&adapterDesc);
        assert(SUCCEEDED(hr));
        if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
            break;
        }
        useAdapter.Reset();
    }
    assert(useAdapter != nullptr);

    // デバイス生成: 高いFeature Levelから順に試す。
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0 };
    for (size_t i = 0; i < _countof(featureLevels); ++i) {
        hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&device_));
        if (SUCCEEDED(hr)) break;
    }
    assert(device_ != nullptr);

#ifdef _DEBUG
    // エラー時にブレークする設定
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
    if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
    }
#endif

    // コマンドキュー=GPUへ送る待ち行列、アロケータ=コマンド用メモリ、リスト=記録する命令列。
    D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
    hr = device_->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue_));
    assert(SUCCEEDED(hr));

    hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator_));
    assert(SUCCEEDED(hr));

    hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator_.Get(), nullptr, IID_PPV_ARGS(&commandList_));
    assert(SUCCEEDED(hr));

    // スワップチェーンの生成: 表示用バックバッファを2枚（ダブルバッファリング）持つ。
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> baseSwapChain;
    hr = dxgiFactory_->CreateSwapChainForHwnd(
        commandQueue_.Get(),
        hWnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        baseSwapChain.GetAddressOf());
    assert(SUCCEEDED(hr));
    hr = baseSwapChain.As(&swapChain_);
    assert(SUCCEEDED(hr));

    // RTV用ヒープとリソースの生成: バックバッファを「描画先」として見えるようにする。
    rtvDescriptorHeap_ = DX12Utility::CreateDescriptorHeap(
        device_.Get(),
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        2,
        false);

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = GetBackBufferFormat();
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    UINT rtvIncrementSize = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    rtvHandles_[0] = rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
    rtvHandles_[1] = rtvHandles_[0];
    rtvHandles_[1].ptr += rtvIncrementSize;

    for (int i = 0; i < 2; ++i) {
        hr = swapChain_->GetBuffer(i, IID_PPV_ARGS(&swapChainResources_[i]));
        assert(SUCCEEDED(hr));
        device_->CreateRenderTargetView(swapChainResources_[i].Get(), &rtvDesc, rtvHandles_[i]);
    }

    // 深度バッファ(DSV)の生成: 手前のピクセルだけを描画するための奥行き情報。
    dsvDescriptorHeap_ = DX12Utility::CreateDescriptorHeap(
        device_.Get(),
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
        1,
        false);

    D3D12_RESOURCE_DESC depthResDesc{};
    depthResDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthResDesc.Width = width;
    depthResDesc.Height = height;
    depthResDesc.DepthOrArraySize = 1;
    depthResDesc.MipLevels = 1;
    depthResDesc.Format = GetDepthBufferFormat();
    depthResDesc.SampleDesc.Count = 1;
    depthResDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_HEAP_PROPERTIES depthHeapProps{};
    depthHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE depthClearValue{};
    depthClearValue.DepthStencil.Depth = 1.0f;
    depthClearValue.Format = GetDepthBufferFormat();

    hr = device_->CreateCommittedResource(
        &depthHeapProps, D3D12_HEAP_FLAG_NONE, &depthResDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClearValue, IID_PPV_ARGS(&depthBuffer_)
    );
    assert(SUCCEEDED(hr));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = GetDepthBufferFormat();
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(depthBuffer_.Get(), &dsvDesc, dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart());

    // フェンスの生成: CPUがGPUの完了を待つための同期オブジェクト。
    hr = device_->CreateFence(fenceValue_, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    assert(SUCCEEDED(hr));
    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(fenceEvent_ != nullptr);
}

void DirectXCommon::BeginDraw() {
    UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();

    // リソースバリア: 表示中(PRESENT)のバックバッファを描画可能(RENDER_TARGET)へ遷移する。
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        swapChainResources_[backBufferIndex].Get(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET
    );
    commandList_->ResourceBarrier(1, &barrier);

    // RTV と DSV の指定: カラーバッファと深度バッファを出力先として設定する。
    auto dsvHandle = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
    commandList_->OMSetRenderTargets(1, &rtvHandles_[backBufferIndex], FALSE, &dsvHandle);

    // 画面のクリア
    float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f }; // 適当なクリアカラー
    commandList_->ClearRenderTargetView(rtvHandles_[backBufferIndex], clearColor, 0, nullptr);
    commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

void DirectXCommon::EndDraw() {
    UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();

    // 描画完了後は、画面に表示できるPRESENT状態へ戻す。
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        swapChainResources_[backBufferIndex].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT
    );
    commandList_->ResourceBarrier(1, &barrier);

    // 記録を終えたコマンドリストだけがGPUへ実行できる。
    HRESULT hr = commandList_->Close();
    assert(SUCCEEDED(hr));

    // コマンドの実行
    ID3D12CommandList* commandLists[] = { commandList_.Get() };
    commandQueue_->ExecuteCommandLists(1, commandLists);

    // 画面のフリップ
    swapChain_->Present(1, 0);

    // このサンプルでは毎フレームGPU完了を待つ。完了後ならアロケータとリストを安全に再利用できる。
    WaitForGpu();
    hr = commandAllocator_->Reset();
    assert(SUCCEEDED(hr));
    hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
    assert(SUCCEEDED(hr));
}

void DirectXCommon::WaitForGpu() {
    // 新しいフェンス値をGPUへ送信し、その値に到達するまでCPUを待機させる。
    const uint64_t fenceValueToSignal = fenceValue_ + 1;
    commandQueue_->Signal(fence_.Get(), fenceValueToSignal);

    if (fence_->GetCompletedValue() < fenceValueToSignal) {
        fence_->SetEventOnCompletion(fenceValueToSignal, fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
    fenceValue_ = fenceValueToSignal;
}
