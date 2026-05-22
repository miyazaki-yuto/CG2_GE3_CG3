#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h> // ComPtr用
#include "Matrix4x4.h"
#include <cstdint>
#include <fstream>

#include <dxcapi.h>
#pragma comment(lib,"dxcompiler.lib")

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

// 一旦ココ後でファイル分けする
struct Vector4 {
	float x;
	float y;
	float z;
	float w;
};

struct TransformData {
	Vector3 scale;
	Vector3 rotate;
	Vector3 translate;
};

struct TransformationMatrix {
	Matrix4x4 WVP;
};

class Graphics
{
public:
	Graphics() = default;
	~Graphics();

	// DirectXの初期化
	void Initialize(HWND hWnd, int32_t width, int32_t height, std::ofstream& logStream);

	// フレーム計算用
	void Update();

	// 描画開始（画面クリアなど）
	void BeginDraw();
	// 描画終了（画面フリップと同期など）
	void EndDraw();

	void Draw();

private:

	void InitializeImGui(HWND hwnd); 
	void ShutdownImGui();            

	// ComPtrを使用して自動解放を行う
	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;
	Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources_[2];
	Microsoft::WRL::ComPtr<ID3D12Fence> fence_;

	HANDLE fenceEvent_ = nullptr;
	uint64_t fenceValue_ = 0;
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles_[2]{};

	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils_;
	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler_;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler_;

	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob_;
	Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob_;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

	D3D12_VIEWPORT viewport_{};
	D3D12_RECT scissorRect_{};

	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;

	TransformationMatrix* wvpData_ = nullptr;
	TransformData transform_;       
	TransformData cameraTransform_;
};