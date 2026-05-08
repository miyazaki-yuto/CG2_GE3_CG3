#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h> // ComPtr用
#include <cstdint>
#include <fstream>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

class Graphics
{
public:
	Graphics() = default;
	~Graphics();

	// DirectXの初期化
	void Initialize(HWND hWnd, int32_t width, int32_t height, std::ofstream& logStream);

	// 描画開始（画面クリアなど）
	void BeginDraw();
	// 描画終了（画面フリップと同期など）
	void EndDraw();

private:
	// ComPtrを使用して自動解放を行う
	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_;
	Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources_[2];
	Microsoft::WRL::ComPtr<ID3D12Fence> fence_;

	HANDLE fenceEvent_ = nullptr;
	uint64_t fenceValue_ = 0;
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles_[2]{};
};