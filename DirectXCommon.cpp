#include "DirectXCommon.h"
#include "DX12Utility.h"
#include <cassert>
#include"externals/DirectXTex/d3dx12.h"
#include <cstring>
#include <stdexcept>

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
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("DirectXCommon dimensions must be greater than zero.");
    }
    width_ = static_cast<uint32_t>(width);
    height_ = static_cast<uint32_t>(height);

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

    // コマンドキューは、記録済みの命令をGPUへ順番に送る待ち行列。
    D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
    hr = device_->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue_));
    assert(SUCCEEDED(hr));

    // スワップチェーンの生成: 表示用バックバッファを2枚（ダブルバッファリング）持つ。
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = kFrameCount;
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

    // バックバッファごとに、コマンド記録領域と動的な頂点・定数データ領域を持つ。
    // GPUがframe[0]を使用中でも、CPUはframe[1]へ安全に書き込める。
    currentFrameIndex_ = swapChain_->GetCurrentBackBufferIndex();
    for (uint32_t frameIndex = 0; frameIndex < kFrameCount; ++frameIndex) {
        FrameResource& frame = frameResources_[frameIndex];
        hr = device_->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&frame.commandAllocator));
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create a frame command allocator.");
        }

        frame.dynamicBuffer = DX12Utility::CreateBufferResource(
            device_.Get(), kDynamicBufferSizePerFrame);
        hr = frame.dynamicBuffer->Map(
            0, nullptr, reinterpret_cast<void**>(&frame.mappedDynamicBuffer));
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to map a frame dynamic buffer.");
        }
    }

    // 初期化直後もコマンドリストを開いておき、ゲームループ前のテクスチャ転送を記録できるようにする。
    hr = device_->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        frameResources_[currentFrameIndex_].commandAllocator.Get(),
        nullptr,
        IID_PPV_ARGS(&commandList_));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create a graphics command list.");
    }

    // RTV用ヒープとリソースの生成: バックバッファを「描画先」として見えるようにする。
    rtvDescriptorHeap_ = DX12Utility::CreateDescriptorHeap(
        device_.Get(),
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        kFrameCount,
        false);
    CreateRenderTargetViews();

    // 深度バッファ(DSV)の生成: 手前のピクセルだけを描画するための奥行き情報。
    dsvDescriptorHeap_ = DX12Utility::CreateDescriptorHeap(
        device_.Get(),
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
        1,
        false);
    CreateDepthStencilResource();

    // フェンスの生成: CPUがGPUの完了を待つための同期オブジェクト。
    hr = device_->CreateFence(fenceValue_, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    assert(SUCCEEDED(hr));
    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(fenceEvent_ != nullptr);
}

void DirectXCommon::CreateRenderTargetViews() {
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = GetBackBufferFormat();
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    const UINT descriptorSize = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    const D3D12_CPU_DESCRIPTOR_HANDLE heapStart =
        rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();

    for (uint32_t frameIndex = 0; frameIndex < kFrameCount; ++frameIndex) {
        rtvHandles_[frameIndex] = heapStart;
        rtvHandles_[frameIndex].ptr +=
            static_cast<SIZE_T>(descriptorSize) * frameIndex;

        const HRESULT hr = swapChain_->GetBuffer(
            frameIndex,
            IID_PPV_ARGS(swapChainResources_[frameIndex].GetAddressOf()));
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to get a SwapChain back buffer.");
        }
        device_->CreateRenderTargetView(
            swapChainResources_[frameIndex].Get(),
            &rtvDesc,
            rtvHandles_[frameIndex]);
    }
}

void DirectXCommon::CreateDepthStencilResource() {
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = width_;
    resourceDesc.Height = height_;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = GetDepthBufferFormat();
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.Format = GetDepthBufferFormat();

    const HRESULT hr = device_->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(depthBuffer_.GetAddressOf()));
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create the depth stencil resource.");
    }

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = GetDepthBufferFormat();
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(
        depthBuffer_.Get(),
        &dsvDesc,
        dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart());
}

void DirectXCommon::Resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 ||
        (width == width_ && height == height_)) {
        return;
    }
    if (isDrawing_) {
        throw std::logic_error("SwapChain cannot be resized during drawing.");
    }

    // この設計では次フレーム用コマンドリストが常に開いている。
    // 先に閉じて送信することで、リサイズ直前に読み込んだGPUデータも失わない。
    HRESULT hr = commandList_->Close();
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to close the command list before resizing.");
    }
    ID3D12CommandList* commandLists[] = { commandList_.Get() };
    commandQueue_->ExecuteCommandLists(1, commandLists);
    WaitForGpu();

    for (Microsoft::WRL::ComPtr<ID3D12Resource>& backBuffer :
        swapChainResources_) {
        backBuffer.Reset();
    }
    depthBuffer_.Reset();

    DXGI_SWAP_CHAIN_DESC swapChainDesc{};
    hr = swapChain_->GetDesc(&swapChainDesc);
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to get the SwapChain description.");
    }
    hr = swapChain_->ResizeBuffers(
        kFrameCount,
        width,
        height,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        swapChainDesc.Flags);
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to resize the SwapChain buffers.");
    }

    width_ = width;
    height_ = height;
    CreateRenderTargetViews();
    CreateDepthStencilResource();

    // Resize前のGPU処理は完了済みなので、全フレーム領域を先頭から再利用できる。
    currentFrameIndex_ = swapChain_->GetCurrentBackBufferIndex();
    for (FrameResource& frame : frameResources_) {
        frame.fenceValue = 0;
        frame.dynamicBufferOffset = 0;
        hr = frame.commandAllocator->Reset();
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to reset a frame allocator after resizing.");
        }
    }
    hr = commandList_->Reset(
        frameResources_[currentFrameIndex_].commandAllocator.Get(), nullptr);
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to reset the command list after resizing.");
    }
}

void DirectXCommon::BeginDraw() {
    assert(!isDrawing_);
    const UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();
    assert(backBufferIndex == currentFrameIndex_);

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

    // ViewportとScissorは全描画クラス共通なので、フレーム開始時に1回だけ設定する。
    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width_);
    viewport.Height = static_cast<float>(height_);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    const D3D12_RECT scissorRect{
        0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_) };
    commandList_->RSSetViewports(1, &viewport);
    commandList_->RSSetScissorRects(1, &scissorRect);
    isDrawing_ = true;
}

void DirectXCommon::EndDraw() {
    assert(isDrawing_);
    const UINT backBufferIndex = currentFrameIndex_;

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

    // 画面をフリップした後、このフレームの最後にフェンス値を書き込む。
    hr = swapChain_->Present(1, 0);
    if (FAILED(hr)) {
        throw std::runtime_error("SwapChain Present failed.");
    }

    const uint64_t submittedFenceValue = ++fenceValue_;
    hr = commandQueue_->Signal(fence_.Get(), submittedFenceValue);
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to signal the frame fence.");
    }
    frameResources_[currentFrameIndex_].fenceValue = submittedFenceValue;
    lastSubmittedFenceValue_ = submittedFenceValue;

    // Present後のバックバッファに対応するフレーム領域を次の記録先にする。
    // その領域をGPUがまだ使用中の場合だけ待つため、CPUとGPUを並行動作させられる。
    currentFrameIndex_ = swapChain_->GetCurrentBackBufferIndex();
    WaitForFrame(currentFrameIndex_);

    FrameResource& nextFrame = frameResources_[currentFrameIndex_];
    hr = nextFrame.commandAllocator->Reset();
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to reset a frame command allocator.");
    }
    hr = commandList_->Reset(nextFrame.commandAllocator.Get(), nullptr);
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to reset the graphics command list.");
    }
    // GPU完了を確認した領域なので、先頭から再利用しても安全。
    nextFrame.dynamicBufferOffset = 0;
    isDrawing_ = false;
}

void DirectXCommon::WaitForGpu() {
    if (commandQueue_ == nullptr || fence_ == nullptr || fenceEvent_ == nullptr) {
        return;
    }

    // 終了時など、明示的に全GPU処理の完了が必要な場合だけ使用する。
    const uint64_t fenceValueToSignal = ++fenceValue_;
    const HRESULT signalResult =
        commandQueue_->Signal(fence_.Get(), fenceValueToSignal);
    if (FAILED(signalResult)) {
        assert(false && "Failed to signal the GPU completion fence.");
        return;
    }

    if (fence_->GetCompletedValue() < fenceValueToSignal) {
        const HRESULT eventResult =
            fence_->SetEventOnCompletion(fenceValueToSignal, fenceEvent_);
        if (FAILED(eventResult)) {
            assert(false && "Failed to set the GPU completion event.");
            return;
        }
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}

void DirectXCommon::WaitForFrame(uint32_t frameIndex) {
    assert(frameIndex < kFrameCount);
    const uint64_t requiredFenceValue = frameResources_[frameIndex].fenceValue;
    if (requiredFenceValue == 0 ||
        fence_->GetCompletedValue() >= requiredFenceValue) {
        return;
    }

    const HRESULT hr =
        fence_->SetEventOnCompletion(requiredFenceValue, fenceEvent_);
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to set a frame fence event.");
    }
    WaitForSingleObject(fenceEvent_, INFINITE);
}

DynamicBufferAllocation DirectXCommon::AllocateDynamicBuffer(
    size_t sizeInBytes,
    size_t alignment) {
    if (sizeInBytes == 0 || alignment == 0 ||
        (alignment & (alignment - 1)) != 0) {
        throw std::invalid_argument(
            "Dynamic buffer size must be non-zero and alignment must be a power of two.");
    }

    FrameResource& frame = frameResources_[currentFrameIndex_];
    const size_t alignedOffset =
        (frame.dynamicBufferOffset + alignment - 1) & ~(alignment - 1);
    if (alignedOffset > kDynamicBufferSizePerFrame ||
        sizeInBytes > kDynamicBufferSizePerFrame - alignedOffset) {
        throw std::overflow_error(
            "Per-frame dynamic buffer capacity was exceeded.");
    }

    DynamicBufferAllocation allocation{};
    allocation.cpuAddress = frame.mappedDynamicBuffer + alignedOffset;
    allocation.gpuAddress =
        frame.dynamicBuffer->GetGPUVirtualAddress() + alignedOffset;
    allocation.resource = frame.dynamicBuffer.Get();
    allocation.offset = alignedOffset;
    allocation.sizeInBytes = sizeInBytes;
    frame.dynamicBufferOffset = alignedOffset + sizeInBytes;
    return allocation;
}

Microsoft::WRL::ComPtr<ID3D12Resource>
DirectXCommon::CreateStaticBufferResource(
    const void* data,
    size_t sizeInBytes,
    D3D12_RESOURCE_STATES finalState) {
    if (data == nullptr || sizeInBytes == 0) {
        throw std::invalid_argument("Static buffer data must not be empty.");
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> defaultBuffer =
        DX12Utility::CreateDefaultBufferResource(device_.Get(), sizeInBytes);

    // フレームごとに1つだけ作ってあるUploadヒープの一部へデータを書く。
    // バッファごとのUploadリソース生成とMap/Unmapが不要になる。
    const DynamicBufferAllocation uploadAllocation =
        AllocateDynamicBuffer(sizeInBytes, 16);
    std::memcpy(uploadAllocation.cpuAddress, data, sizeInBytes);
    commandList_->CopyBufferRegion(
        defaultBuffer.Get(),
        0,
        uploadAllocation.resource,
        uploadAllocation.offset,
        sizeInBytes);

    // コピー後は、呼び出し側が指定した頂点／インデックス用途へ状態を遷移する。
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        defaultBuffer.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        finalState);
    commandList_->ResourceBarrier(1, &barrier);
    return defaultBuffer;
}
