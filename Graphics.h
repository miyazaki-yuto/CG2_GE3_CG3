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
const uint32_t kSpriteMaxCount = 1000;

class Graphics
{
public:
	Graphics() = default;
	~Graphics();

	// DirectXの初期化
	void Initialize(HWND hWnd, int32_t width, int32_t height, std::ofstream& logStream);

	// スプライトの描画に必要な初期化
	void InitializeDrawSprite();

	// フレーム計算用
	void Update();

	// 描画開始
	void BeginDraw();
	// 描画終了
	void EndDraw();

	// --- Triangle用の描画関数 ---
	// mainからデータを渡すための関数たち
	void SetTriangleVertices(int index, const TextureVertexData* vertices);
	void SetTriangleTransform(int index, const TransformData& transform);
	void SetTriangleTexture(int index, int textureIndex);
	void SetColor(const Vector4& color);
	void Draw();

	// --- Sprite用の描画関数 ---
	// Sprite用の頂点データを設定
	void SetSpriteVertices(int index, const TextureVertexData* vertices);
	// Sprite用のTransformを設定
	void SetSpriteTransform(int index, const TransformData& transform);
	// Sprite用のテクスチャを設定
	void SetSpriteTexture(int index, int textureIndex);
	// Spriteの色を設定
	void SetSpriteColor(int index, const Vector4& color);

	// Spriteを描画する
	void DrawSprites();

	// --- Sphere用の描画関数 ---
	// 初期化
	void InitializeDrawSphere();
	// トランスフォームの設定
	void SetSphereTransform(const TransformData& transform); 
	// テクスチャの設定
	void SetSphereTexture(int textureIndex);
	// 色の設定
	void SetSphereColor(const Vector4& color); 
	// 描画
	void DrawSphere();

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

	// --- Sprite用のリソース ---
	Microsoft::WRL::ComPtr<ID3D12PipelineState> spritePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12Resource> spriteVertexResource_;
	D3D12_VERTEX_BUFFER_VIEW spriteVertexBufferView_{};
	TextureVertexData* mappedSpriteVertexData_ = nullptr;

	// Sprite用のWVP行列(正投影)
	Microsoft::WRL::ComPtr<ID3D12Resource> spriteWvpResource_[kSpriteMaxCount];
	TransformationMatrix* spriteWvpData_[kSpriteMaxCount] = { nullptr };
	TransformData spriteTransform_[kSpriteMaxCount];

	// Sprite用のマテリアル(色)
	Microsoft::WRL::ComPtr<ID3D12Resource> spriteMaterialResource_[kSpriteMaxCount];
	Vector4* spriteMaterialData_[kSpriteMaxCount] = { nullptr };

	int spriteSelectedTexture_[kSpriteMaxCount] = { 0 };

	// --- Sphere用のリソース ---
	Microsoft::WRL::ComPtr<ID3D12Resource> sphereVertexResource_;
	D3D12_VERTEX_BUFFER_VIEW sphereVertexBufferView_{};
	uint32_t sphereVertexCount_ = 0;

	Microsoft::WRL::ComPtr<ID3D12Resource> sphereWvpResource_;
	TransformationMatrix* sphereWvpData_ = nullptr;
	TransformData sphereTransform_ = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

	Microsoft::WRL::ComPtr<ID3D12Resource> sphereMaterialResource_;
	Vector4* sphereMaterialData_ = nullptr;

	int sphereSelectedTexture_ = 0;
};