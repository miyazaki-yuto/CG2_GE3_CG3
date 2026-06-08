#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h> // ComPtr用
#include "Matrix4x4.h"
#include "CommonTypes.h"
#include <cstdint>
#include <fstream>
#include <unordered_map>
#include <vector>

#include <dxcapi.h>
#pragma comment(lib,"dxcompiler.lib")

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

const uint32_t kTriangleMaxCount = 1000;

class Graphics
{
public:
	Graphics() = default;
	~Graphics();

	// DirectXの初期化
	void Initialize(HWND hWnd, int32_t width, int32_t height, std::ofstream& logStream);

	// フレーム計算用
	void Update();

	// 描画開始
	void BeginDraw();
	// 描画終了
	void EndDraw();

	// mainからデータを渡すための関数たち
	void SetTriangleVertices(int index, const TextureVertexData* vertices);
	void SetTriangleTransform(int index, const TransformData& transform);
	void SetTriangleTexture(int index, int textureIndex);
	void SetColor(const Vector4& color);
	void Draw();

	// 画像読み込み
	int LoadTexture(const std::string& filePath);

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

	//Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
	//Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;

	//TransformationMatrix* wvpData_ = nullptr;
	//TransformData transform_;       

	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_[kTriangleMaxCount];

	TransformationMatrix* wvpData_[kTriangleMaxCount] = { nullptr, nullptr };
	TransformData transform_[kTriangleMaxCount];
	TransformData cameraTransform_;

	Vector4 color_ = { 1.0f, 1.0f, 1.0f, 1.0f }; // ImGuiで変更する色を保持する変数
	Vector4* materialData_ = nullptr;            // GPUに書き込むためのポインタ

	int selectedTexture_[kTriangleMaxCount] = { 0, 0 };

	// zバッファ(深度バッファ)用
	Microsoft::WRL::ComPtr<ID3D12Resource> depthBuffer_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_;

	// マップ（紐付け）されたGPUの頂点バッファのアドレスを保持しておくためのポインタ
	TextureVertexData* mappedVertexData_ = nullptr;


	// テクスチャを複数保持するための配列とカウン
	static const size_t kMaxTextures = 10; // 最大10枚まで読み込めるようにする
	Microsoft::WRL::ComPtr<ID3D12Resource> textureResources_[kMaxTextures];
	uint32_t textureCount_ = 0;            // 現在ロード済みのテクスチャ数

	// テクスチャの重複読み込みを防ぐキャッシュ
	std::unordered_map<std::string, int> textureCache_;

	//  転送完了待ちの中間リソースを保持するリスト
	std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> intermediateResources_;
};