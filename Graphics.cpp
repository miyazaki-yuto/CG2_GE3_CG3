#include "Graphics.h"
#include <cassert>
#include <format>
#include <string>
#include <vector>
#include <cmath>
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_win32.h"
#include "externals/imgui/imgui_impl_dx12.h"
#endif
#include "externals/DirectXTex/DirectXTex.h"
#include"externals/DirectXTex/d3dx12.h"

// 内部だけで使うユーティリティ関数
namespace {
	std::string ConvertString(const std::wstring& str) {
		if (str.empty()) { return std::string(); }
		int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
		std::string result(sizeNeeded, 0);
		WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
		return result;
	}

	// stringをwstringに変換する関数
	std::wstring ConvertString(const std::string& str) {
		if (str.empty()) { return std::wstring(); }
		int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
		std::wstring result(sizeNeeded, 0);
		MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
		return result;
	}

	// テクスチャデータを読み込む関数
	DirectX::ScratchImage LoadTexture(const std::string& filePath)
	{
		// テクスチャファイルを読んでプログラムで扱えるようにする
		DirectX::ScratchImage image{};
		std::wstring filePathW = ConvertString(filePath);
		HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
		assert(SUCCEEDED(hr));

		// ミップマップの作成
		DirectX::ScratchImage mipImages{};
		hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);
		assert(SUCCEEDED(hr));

		// ミップマップ付きのデータを返す
		return mipImages;
	}

	// テクスチャ用のリソースを作成する便利関数
	ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata)
	{
		// 1. metadataを基にResourceの設定
		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Width = UINT(metadata.width);                               // 幅
		resourceDesc.Height = UINT(metadata.height);                             // 高さ
		resourceDesc.MipLevels = UINT16(metadata.mipLevels);                     // ミップマップの数
		resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);              // 奥行き or 配列サイズ
		resourceDesc.Format = metadata.format;                                   // フォーマット
		resourceDesc.SampleDesc.Count = 1;                                       // サンプリングカウント
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension);   // 1D, 2D, 3Dのどれか

		// 2. 利用するHeapの設定 
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		// 3. Resourceを生成する
		ID3D12Resource* resource = nullptr;
		HRESULT hr = device->CreateCommittedResource(
			&heapProperties,                            // ヒープの設定
			D3D12_HEAP_FLAG_NONE,                       // 特にフラグなし
			&resourceDesc,                              // リソースの設定
			D3D12_RESOURCE_STATE_COPY_DEST,             // データ転送される設定
			nullptr,                                    // クリア値
			IID_PPV_ARGS(&resource)                     // 生成されたリソースを受け取る
		);
		assert(SUCCEEDED(hr));

		return resource;
	}

	[[nodiscard]]
	ID3D12Resource* UploadTextureData(
		ID3D12Resource* texture,
		const DirectX::ScratchImage& mipImages,
		ID3D12Device* device,
		ID3D12GraphicsCommandList* commandList)
	{
		// 1. サブリソースデータの準備
		std::vector<D3D12_SUBRESOURCE_DATA> subresources;
		DirectX::PrepareUpload(device, mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);

		// 2. 中間リソース（Upload Heap）に必要なサイズを取得
		uint64_t intermediateSize = GetRequiredIntermediateSize(texture, 0, UINT(subresources.size()));

		// 3. 中間リソース（Upload Heap）の作成
		ID3D12Resource* intermediateResource = nullptr;
		auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(intermediateSize);

		HRESULT hr = device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&intermediateResource)
		);
		assert(SUCCEEDED(hr));

		// 4. データ転送コマンドの積む
		UpdateSubresources(
			commandList,
			texture,
			intermediateResource,
			0, 0,
			UINT(subresources.size()),
			subresources.data()
		);

		// 5. テクスチャの状態を COPY_DEST から PIXEL_SHADER_RESOURCE へ変更
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			texture,
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		);
		commandList->ResourceBarrier(1, &barrier);

		// 中間リソースはコマンド実行完了まで保持する必要があるため返す
		return intermediateResource;
	}

	void Log(std::ostream& os, const std::string& message) {
		os << message << std::endl;
		std::string formatted = std::format("{}\n", message);
		OutputDebugStringA(formatted.c_str());
	}

	// シェーダーコンパイル用便利関数
	IDxcBlob* CompileShader(
		const std::wstring& filePath,
		const wchar_t* profile,
		IDxcUtils* dxcUtils,
		IDxcCompiler3* dxcCompiler,
		IDxcIncludeHandler* includeHandler,
		std::ostream& logStream
	)
	{
		// ココの中身をこの後書いていく
		// 1. ファイルを読む

		// これからシェーダーをコンパイルする旨をログに出す
		Log(logStream, ConvertString(std::format(L"Begin CompileShader, path:{}, profile:{}\n", filePath, profile)));
		// HLSLファイルを読む
		IDxcBlobEncoding* shaderSource = nullptr;
		HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
		// 読めなかったら止める
		assert(SUCCEEDED(hr));
		// 読み込んだファイルの内容を設定する
		DxcBuffer shaderSourceBuffer;
		shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
		shaderSourceBuffer.Size = shaderSource->GetBufferSize();
		shaderSourceBuffer.Encoding = DXC_CP_UTF8; //UTF8文字コードであることを通知

		// 2. Compileする

		LPCWSTR arguments[] = {
			filePath.c_str(), // コンパイル対象のHLSLファイル名
			L"-E", L"main", // エントリーポイントの指定。基本main以外にはしない
			L"-T",profile, // ShaderProfileの設定
			L"-Zi",L"-Qembed_debug", // デバッグ用の情報を埋め込む
			L"-Od", // 最適化を外しておく
			L"-Zpr", // メモリレイアウトは行優先
		};

		// 実際にShaderをコンパイルする
		IDxcResult* shaderResult = nullptr;
		hr = dxcCompiler->Compile(
			&shaderSourceBuffer, // 読み込んだファイル
			arguments, // コンパイルオプション
			_countof(arguments), // コンパイルオプション数
			includeHandler, // includeが含まれた諸々
			IID_PPV_ARGS(&shaderResult) // コンパイル結果
		);
		// コンパイルエラーではなくdxcが起動出来ないなど致命的な状況
		assert(SUCCEEDED(hr));

		// 3. 警告・エラーが出ていないか確認する

		// 警告・エラーが出たらログに出して止める
		IDxcBlobUtf8* shaderError = nullptr;
		shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
		if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
			Log(logStream, shaderError->GetStringPointer());
			// 警告・エラーダメゼッタイ
			assert(false);
		}
		if (shaderError != nullptr) {
			shaderError->Release();
		}

		// 4. Compile結果を受け取って返す

		// コンパイル結果空実行用バイナリ部分を取得
		IDxcBlob* shaderBlob = nullptr;
		hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
		assert(SUCCEEDED(hr));
		// 成功ログを出す
		Log(logStream, ConvertString(std::format(L"Compile Succceeded, path:{}, profile:{}\n", filePath, profile)));

		// 使わなくなったリソースを解放
		shaderSource->Release();
		shaderResult->Release();

		// 実行用バイナリを返却
		return shaderBlob;
	}

	// リソース作成の便利関数
	ID3D12Resource* CreateBufferResource(ID3D12Device* device, size_t sizeInBytes) {
		D3D12_HEAP_PROPERTIES uploadHeapProperties{};
		uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD; // UploadHeapを使う

		D3D12_RESOURCE_DESC resourceDesc{};
		// バッファリソースの設定
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Width = sizeInBytes;
		// バッファの場合はこ���らは1にする決まり
		resourceDesc.Height = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.SampleDesc.Count = 1;
		// バッファの場合はこれにする決まり
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		ID3D12Resource* resource = nullptr;
		HRESULT hr = device->CreateCommittedResource(
			&uploadHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&resource)
		);
		assert(SUCCEEDED(hr));
		return resource;
	}

	// DescriptorHeap作成の便利関数
	ID3D12DescriptorHeap* CreateDescriptorHeap(
		ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible)
	{
		ID3D12DescriptorHeap* descriptorHeap = nullptr;
		D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
		descriptorHeapDesc.Type = heapType;
		descriptorHeapDesc.NumDescriptors = numDescriptors;
		descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		HRESULT hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
		assert(SUCCEEDED(hr));
		return descriptorHeap;
	}

}

void Graphics::WaitForGpu()
{
	// コマンドキューにシグナルを送る
	const uint64_t fenceValueToSignal = fenceValue_ + 1;
	commandQueue_->Signal(fence_.Get(), fenceValueToSignal);

	// GPUがそのシグナルに到達するまで待機する
	if (fence_->GetCompletedValue() < fenceValueToSignal)
	{
		HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		fence_->SetEventOnCompletion(fenceValueToSignal, eventHandle);
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
	// 次のフェンス値に更新
	fenceValue_ = fenceValueToSignal;
}


Graphics::~Graphics()
{
	WaitForGpu();

	ShutdownImGui();

	CleanupResources();

	if (fenceEvent_) {
		CloseHandle(fenceEvent_);
		fenceEvent_ = nullptr;
	}
}

void Graphics::CleanupResources()
{
	// Unmap CPU マッピングされたリソースを解放してから Reset する
	if (mappedVertexData_ != nullptr && vertexResource_ != nullptr) {
		vertexResource_->Unmap(0, nullptr);
		mappedVertexData_ = nullptr;
	}
	if (mappedSpriteVertexData_ != nullptr && spriteVertexResource_ != nullptr) {
		// InitializeDrawSprite の実装上、ここで Unmap されている場合もあるが
		// 安全のためマップポインタが残っていれば Unmap しておく
		spriteVertexResource_->Unmap(0, nullptr);
		mappedSpriteVertexData_ = nullptr;
	}
	if (materialData_ != nullptr && materialResource_ != nullptr) {
		materialResource_->Unmap(0, nullptr);
		materialData_ = nullptr;
	}
	if (directionalLightData_ != nullptr && directionalLightResource_ != nullptr) {
		directionalLightResource_->Unmap(0, nullptr);
		directionalLightData_ = nullptr;
	}
	// 各要素配列の Unmap
	for (int i = 0; i < kTriangleMaxCount; ++i) {
		if (wvpData_[i] != nullptr && wvpResource_[i] != nullptr) {
			wvpResource_[i]->Unmap(0, nullptr);
			wvpData_[i] = nullptr;
		}
	}
	for (int i = 0; i < kSpriteMaxCount; ++i) {
		if (spriteWvpData_[i] != nullptr && spriteWvpResource_[i] != nullptr) {
			spriteWvpResource_[i]->Unmap(0, nullptr);
			spriteWvpData_[i] = nullptr;
		}
		if (spriteMaterialData_[i] != nullptr && spriteMaterialResource_[i] != nullptr) {
			spriteMaterialResource_[i]->Unmap(0, nullptr);
			spriteMaterialData_[i] = nullptr;
		}
	}
	if (sphereWvpData_ != nullptr && sphereWvpResource_ != nullptr) {
		sphereWvpResource_->Unmap(0, nullptr);
		sphereWvpData_ = nullptr;
	}
	if (sphereMaterialData_ != nullptr && sphereMaterialResource_ != nullptr) {
		sphereMaterialResource_->Unmap(0, nullptr);
		sphereMaterialData_ = nullptr;
	}

	// テクスチャリソースの解放
	for (size_t i = 0; i < kMaxTextures; ++i) {
		textureResources_[i].Reset();
	}
	textureCache_.clear();

	// Triangle リソースの解放
	for (uint32_t i = 0; i < kTriangleMaxCount; ++i) {
		if (wvpResource_[i]) {
			wvpResource_[i].Reset();
		}
	}

	// Sprite リソースの解放
	for (uint32_t i = 0; i < kSpriteMaxCount; ++i) {
		if (spriteWvpResource_[i]) {
			spriteWvpResource_[i].Reset();
		}
		if (spriteMaterialResource_[i]) {
			spriteMaterialResource_[i].Reset();
		}
	}

	// Sphere リソースの解放
	sphereVertexResource_.Reset();
	sphereWvpResource_.Reset();
	sphereMaterialResource_.Reset();

	// その他の共通リソース
	vertexResource_.Reset();
	materialResource_.Reset();
	depthBuffer_.Reset();
	directionalLightResource_.Reset();

	// パイプラインステート
	graphicsPipelineState_.Reset();
	spritePipelineState_.Reset();
	rootSignature_.Reset();

	// Heap
	rtvDescriptorHeap_.Reset();
	srvDescriptorHeap_.Reset();
	dsvDescriptorHeap_.Reset();

	// コマンド関連
	commandList_.Reset();
	commandAllocator_.Reset();
	commandQueue_.Reset();

	// デバイス関連
	swapChain_.Reset();
	for (int i = 0; i < 2; ++i) {
		swapChainResources_[i].Reset();
	}
	fence_.Reset();

	// Compiler 関連
	includeHandler_.Reset();
	dxcCompiler_.Reset();
	dxcUtils_.Reset();

	// Factory
	dxgiFactory_.Reset();

	// デバイスは最後に解放
	device_.Reset();
}

void Graphics::Initialize(HWND hWnd, int32_t width, int32_t height, std::ofstream& logStream)
{
#ifdef _DEBUG
	Microsoft::WRL::ComPtr<ID3D12Debug1> debugController;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		debugController->EnableDebugLayer();
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif
	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory_));
	assert(SUCCEEDED(hr));

	Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter;
	for (UINT i = 0; dxgiFactory_->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = useAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
			Log(logStream, std::format("Use Adapter: {}", ConvertString(adapterDesc.Description)));
			break;
		}
		useAdapter.Reset();
	}
	assert(useAdapter != nullptr);

	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0 };
	const char* featureLevelStrings[] = { "12.2", "12.1", "12.0" };
	for (size_t i = 0; i < _countof(featureLevels); ++i) {
		hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&device_));
		if (SUCCEEDED(hr)) {
			Log(logStream, std::format("FeatureLevel : {}", featureLevelStrings[i]));
			break;
		}
	}
	assert(device_ != nullptr);
	Log(logStream, "Complete create D3D12Device!!!");

#ifdef _DEBUG
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
	if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);

		D3D12_MESSAGE_ID denyIds[] = { D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE };
		D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;
		infoQueue->PushStorageFilter(&filter);
	}
#endif

	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device_->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue_));
	assert(SUCCEEDED(hr));

	hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator_));
	assert(SUCCEEDED(hr));

	hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator_.Get(), nullptr, IID_PPV_ARGS(&commandList_));
	assert(SUCCEEDED(hr));

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = width;
	swapChainDesc.Height = height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	hr = dxgiFactory_->CreateSwapChainForHwnd(commandQueue_.Get(), hWnd, &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(swapChain_.GetAddressOf()));
	assert(SUCCEEDED(hr));

	rtvDescriptorHeap_.Attach(CreateDescriptorHeap(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false));

	srvDescriptorHeap_.Attach(CreateDescriptorHeap(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true));

	hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&swapChainResources_[0]));
	assert(SUCCEEDED(hr));
	hr = swapChain_->GetBuffer(1, IID_PPV_ARGS(&swapChainResources_[1]));
	assert(SUCCEEDED(hr));

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();

	rtvHandles_[0] = rtvStartHandle;
	device_->CreateRenderTargetView(swapChainResources_[0].Get(), &rtvDesc, rtvHandles_[0]);

	rtvHandles_[1].ptr = rtvHandles_[0].ptr + device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	device_->CreateRenderTargetView(swapChainResources_[1].Get(), &rtvDesc, rtvHandles_[1]);

	hr = device_->CreateFence(fenceValue_, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
	assert(SUCCEEDED(hr));

	fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	assert(fenceEvent_ != nullptr);

	// 1. DSV用ディスクリプタヒープの作成
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	hr = device_->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvDescriptorHeap_));
	assert(SUCCEEDED(hr));

	// 2. 深度バッファリソースの設定
	D3D12_RESOURCE_DESC depthResDesc{};
	depthResDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthResDesc.Width = width; // Initializeの引数を使用
	depthResDesc.Height = height; // Initializeの引数を使用
	depthResDesc.DepthOrArraySize = 1;
	depthResDesc.MipLevels = 1;
	depthResDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // 深度24bit, ステンシル8bit
	depthResDesc.SampleDesc.Count = 1;
	depthResDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	// 3. ヒーププロパティとクリア値の設定
	D3D12_HEAP_PROPERTIES depthHeapProps{};
	depthHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT; // VRAM上に作成

	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.DepthStencil.Depth = 1.0f; // 最大値でクリア
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

	// 4. リソースの生成
	hr = device_->CreateCommittedResource(
		&depthHeapProps,
		D3D12_HEAP_FLAG_NONE,
		&depthResDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClearValue,
		IID_PPV_ARGS(&depthBuffer_)
	);
	assert(SUCCEEDED(hr));

	// 5. DSV（Depth Stencil View）の作成
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	device_->CreateDepthStencilView(depthBuffer_.Get(), &dsvDesc, dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart());

	// dxcCompilerを初期化
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils_));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler_));
	assert(SUCCEEDED(hr));

	// その後
	hr = dxcUtils_->CreateDefaultIncludeHandler(&includeHandler_);
	assert(SUCCEEDED(hr));

	// Vertex Shaderをコンパイルしてバイナリを得る
	vertexShaderBlob_.Attach(CompileShader(L"Object3d.VS.hlsl", L"vs_6_0", dxcUtils_.Get(), dxcCompiler_.Get(), includeHandler_.Get(), logStream));
	assert(vertexShaderBlob_ != nullptr);

	pixelShaderBlob_.Attach(CompileShader(L"Object3d.PS.hlsl", L"ps_6_0", dxcUtils_.Get(), dxcCompiler_.Get(), includeHandler_.Get(), logStream));
	assert(pixelShaderBlob_ != nullptr);
	// RootSignatureの作成
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// 配列のサイズを 2 から 3 に変更
	D3D12_ROOT_PARAMETER rootParameters[4] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].Descriptor.ShaderRegister = 0;

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[1].Descriptor.ShaderRegister = 1;
	// b2 : 平行光源
	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;    // 定数バッファビュー
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // ピクセルシェーダーからアクセスする
	rootParameters[3].Descriptor.ShaderRegister = 2;                    // レジスタ番号 b2

	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0; // t0に対応
	descriptorRange[0].NumDescriptors = 1;
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);


	descriptionRootSignature.pParameters = rootParameters; // ルートパラメータ配列へのポインタ
	descriptionRootSignature.NumParameters = _countof(rootParameters); // 配列の長さ


	D3D12_STATIC_SAMPLER_DESC staticSampler[1] = {};
	staticSampler[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; // バイリニアフィルタ
	staticSampler[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; // 繰り返し
	staticSampler[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSampler[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSampler[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSampler[0].MaxLOD = D3D12_FLOAT32_MAX;
	staticSampler[0].ShaderRegister = 0; // s0に対応
	staticSampler[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	descriptionRootSignature.pStaticSamplers = staticSampler;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSampler);


	// シリアライズしてバイナリにする
	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
	hr = D3D12SerializeRootSignature(&descriptionRootSignature,
		D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob
	);

	if (FAILED(hr)) {
		Log(logStream, reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}

	// バイナリを元に生成
	hr = device_->CreateRootSignature(0,
		signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature_)
	);
	assert(SUCCEEDED(hr));

	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};


	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

	// BlendStateの設定
	D3D12_BLEND_DESC blendDesc{};
	// すべての色要素を書き込む
	blendDesc.RenderTarget[0].RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_ALL;

	// RasterizerStateの設定
	D3D12_RASTERIZER_DESC resterizerDesc{};

	// 裏面を表示しない
	resterizerDesc.CullMode = D3D12_CULL_MODE_BACK;

	// 三角形の中を塗りつぶす
	resterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = true; // 深度テストを有効化
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL; // 深度バッファへの書き込みを有効化
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL; // 深度値が小さいものを描画

	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = rootSignature_.Get();
	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;
	graphicsPipelineStateDesc.VS = { vertexShaderBlob_->GetBufferPointer(), vertexShaderBlob_->GetBufferSize() };
	graphicsPipelineStateDesc.PS = { pixelShaderBlob_->GetBufferPointer(), pixelShaderBlob_->GetBufferSize() };
	graphicsPipelineStateDesc.BlendState = blendDesc;
	graphicsPipelineStateDesc.RasterizerState = resterizerDesc;

	// ここに深度テストの設定を適用
	graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	// 実際に生成
	hr = device_->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState_));
	assert(SUCCEEDED(hr));

	// 実際に頂点リソースを作る
	vertexResource_.Attach(CreateBufferResource(device_.Get(), sizeof(TextureVertexData) * 3 * kTriangleMaxCount));
	// 頂点バッファビューを作成する
	// リソースの先頭のアドレスから使う
	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	// 使用するリソースのサイズは頂点 3 * 三角の最大数分のサイズを確保
	vertexBufferView_.SizeInBytes = sizeof(TextureVertexData) * 3 * kTriangleMaxCount;
	// 1頂点あたりのサイズ 
	vertexBufferView_.StrideInBytes = sizeof(TextureVertexData);

	// 書き込むためのアドレスを取得
	vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&mappedVertexData_));


	// ビューポートの設定
	viewport_.Width = static_cast<float>(width);
	viewport_.Height = static_cast<float>(height);
	viewport_.TopLeftX = 0;
	viewport_.TopLeftY = 0;
	viewport_.MinDepth = 0.0f;
	viewport_.MaxDepth = 1.0f;

	// シザー矩形の設定
	scissorRect_.left = 0;
	scissorRect_.right = width;
	scissorRect_.top = 0;
	scissorRect_.bottom = height;

	// Material構造体のサイズで確保
	UINT materialBufferSize = (sizeof(Material) + 255) & ~255;  // 256バイト境界に丸める
	materialResource_.Attach(CreateBufferResource(device_.Get(), materialBufferSize));
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
	// Material構造体として使用できるようにキャスト
	Material* matData = reinterpret_cast<Material*>(materialData_);
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
	*materialData_ = color_ = { 1.0f, 1.0f, 1.0f, 1.0f };
	matData->enableLighting = 1;

	// WVP用リソースを作る
	for (int i = 0; i < kTriangleMaxCount; ++i) {
		// ✅ sizeof(TransformationMatrix) に修正
		wvpResource_[i].Attach(CreateBufferResource(device_.Get(), sizeof(TransformationMatrix)));
		wvpResource_[i]->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_[i]));
		wvpData_[i]->WVP = MakeIdentity4x4();
		// ✅ World行列も初期化
		wvpData_[i]->World = MakeIdentity4x4();
		transform_[i] = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
	}

	cameraTransform_ = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -5.0f} };

	// テクスチャ読み込み
	LoadTexture("Resources/uvChecker.png");
	LoadTexture("Resources/monsterBall.png");

	InitializeImGui(hWnd);
}

void Graphics::InitializeDrawSprite() {
	// 1. Sprite用パイプラインステートの作成
	spritePipelineState_ = graphicsPipelineState_;

	// 2. Sprite用頂点バッファの作成 (1スプライトにつき 三角形2つ = 6頂点)
	size_t spriteVertexBufferSize = sizeof(TextureVertexData) * 6 * kSpriteMaxCount;
	spriteVertexResource_.Attach(CreateBufferResource(device_.Get(), spriteVertexBufferSize));

	spriteVertexBufferView_.BufferLocation = spriteVertexResource_->GetGPUVirtualAddress();
	spriteVertexBufferView_.SizeInBytes = static_cast<UINT>(spriteVertexBufferSize);
	spriteVertexBufferView_.StrideInBytes = sizeof(TextureVertexData);

	spriteVertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&mappedSpriteVertexData_));

	// 3. Sprite用リソースの初期化 (WVPとMaterial)
	for (int i = 0; i < kSpriteMaxCount; ++i) {
		// WVP
		spriteWvpResource_[i].Attach(CreateBufferResource(device_.Get(), sizeof(TransformationMatrix)));
		spriteWvpResource_[i]->Map(0, nullptr, reinterpret_cast<void**>(&spriteWvpData_[i]));
		spriteWvpData_[i]->WVP = MakeIdentity4x4();
		spriteTransform_[i] = { {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

		// Material (Color)
		UINT materialBufferSize = (sizeof(Material) + 255) & ~255;
		spriteMaterialResource_[i].Attach(CreateBufferResource(device_.Get(), materialBufferSize));
		spriteMaterialResource_[i]->Map(0, nullptr, reinterpret_cast<void**>(&spriteMaterialData_[i]));

		spriteMaterialData_[i]->color = { 1.0f, 1.0f, 1.0f, 1.0f }; // 基本色（白）
		spriteMaterialData_[i]->enableLighting = 0;                 // ★ 修正: 0に変更（ライティング無効）
	}

	// スプライトの基本形状（四角形=三角形2つ）を設定
	// 既にマップされている場合はそのポインタを使い、未マップなら一時的に Map して Unmap する
	if (mappedSpriteVertexData_ != nullptr) {
		TextureVertexData* spriteVertexData = mappedSpriteVertexData_;
		// 1つ目の三角形
		spriteVertexData[0] = { { 0.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } }; // 左下
		spriteVertexData[1] = { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } }; // 左上
		spriteVertexData[2] = { { 1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } }; // 右下
		// 2つ目の三角形
		spriteVertexData[3] = { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } }; // 左上
		spriteVertexData[4] = { { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } }; // 右上
		spriteVertexData[5] = { { 1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } }; // 右下
	}
	else {
		TextureVertexData* spriteVertexData = nullptr;
		HRESULT hr = spriteVertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&spriteVertexData));
		if (SUCCEEDED(hr)) {
			// 1つ目の三角形
			spriteVertexData[0] = { { 0.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } }; // 左下
			spriteVertexData[1] = { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } }; // 左上
			spriteVertexData[2] = { { 1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } }; // 右下
			// 2つ目の三角形
			spriteVertexData[3] = { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } }; // 左上
			spriteVertexData[4] = { { 1.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } }; // 右上
			spriteVertexData[5] = { { 1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } }; // 右下

			// 一時的に Map したのでアンマップする
			spriteVertexResource_->Unmap(0, nullptr);
		}
	}
}

void Graphics::Update() {
	Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform_.scale, cameraTransform_.rotate, cameraTransform_.translate);
	Matrix4x4 viewMatrix = Inverse(cameraMatrix);

	float aspectRatio = 1280.0f / 720.0f;
	Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, aspectRatio, 0.1f, 100.0f);
	Matrix4x4 viewProjectionMatrix = Multiply(viewMatrix, projectionMatrix);

	for (uint32_t i = 0; i < kTriangleMaxCount; ++i) {
		Matrix4x4 worldMatrix = MakeAffineMatrix(transform_[i].scale, transform_[i].rotate, transform_[i].translate);
		wvpData_[i]->WVP = Multiply(worldMatrix, viewProjectionMatrix);
	}

	// Sprite用の正投影行列 (画面サイズに合わせる。ピクセル座標で指定できるようになります)
	float windowWidth = 1280.0f; // kWindowWidth_ 等を使用してください
	float windowHeight = 720.0f;
	Matrix4x4 orthoMatrix = MakeOrthographicMatrix(0.0f, 0.0f, windowWidth, windowHeight, 0.0f, 100.0f);

	for (uint32_t i = 0; i < kSpriteMaxCount; ++i) {
		if (spriteWvpData_[i] == nullptr) continue;
		// Spriteは通常View行列(カメラ)の影響を受けないため、World行列と正投影行列のみを掛けます
		Matrix4x4 worldMatrix = MakeAffineMatrix(spriteTransform_[i].scale, spriteTransform_[i].rotate, spriteTransform_[i].translate);
		spriteWvpData_[i]->WVP = Multiply(worldMatrix, orthoMatrix);
	}


}

void Graphics::BeginDraw()
{
	UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = swapChainResources_[backBufferIndex].Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList_->ResourceBarrier(1, &barrier);

	// DSVのハンドルを取得
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();

	// OMSetRenderTargetsにDSVを渡すように
	commandList_->OMSetRenderTargets(1, &rtvHandles_[backBufferIndex], false, &dsvHandle);

	float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
	commandList_->ClearRenderTargetView(rtvHandles_[backBufferIndex], clearColor, 0, nullptr);

	// 深度バッファのクリア
	commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

#ifdef USE_IMGUI
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
#endif
}

void Graphics::EndDraw()
{
#ifdef USE_IMGUI
	ImGui::Render();
	ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap_.Get() };
	commandList_->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList_.Get());

#endif
	UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = swapChainResources_[backBufferIndex].Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList_->ResourceBarrier(1, &barrier);

	HRESULT hr = commandList_->Close();
	assert(SUCCEEDED(hr));

	ID3D12CommandList* commandLists[] = { commandList_.Get() };
	commandQueue_->ExecuteCommandLists(1, commandLists);

	swapChain_->Present(1, 0);

	fenceValue_++;
	commandQueue_->Signal(fence_.Get(), fenceValue_);

	if (fence_->GetCompletedValue() < fenceValue_) {
		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		WaitForSingleObject(fenceEvent_, INFINITE);
	}

	// GPU側の転送処理が完全に終わったので、安全に中間リソースをすべて解放する
	//intermediateResources_.clear();

	hr = commandAllocator_->Reset();
	assert(SUCCEEDED(hr));
	hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
	assert(SUCCEEDED(hr));
}

void Graphics::Draw() {
	commandList_->SetGraphicsRootSignature(rootSignature_.Get());
	commandList_->SetPipelineState(graphicsPipelineState_.Get());

	ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap_.Get() };
	commandList_->SetDescriptorHeaps(1, descriptorHeaps);

	commandList_->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());

	// ビューポートとシザー矩形を設定
	commandList_->RSSetViewports(1, &viewport_);
	commandList_->RSSetScissorRects(1, &scissorRect_);

	// 頂点バッファを設定
	commandList_->IASetVertexBuffers(0, 1, &vertexBufferView_);
	commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	commandList_->SetGraphicsRootConstantBufferView(3, directionalLightResource_->GetGPUVirtualAddress());

	// デスクリプタのサイズと開始ハンドルを取得
	UINT descriptorSize = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPUStart = srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();

	for (uint32_t i = 0; i < kTriangleMaxCount; ++i) {
		// 各三角形のWVP行列をセット
		commandList_->SetGraphicsRootConstantBufferView(1, wvpResource_[i]->GetGPUVirtualAddress());

		// 各三角形のテクスチャをバインド
		D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU = srvHandleGPUStart;
		srvHandleGPU.ptr += descriptorSize * (1 + selectedTexture_[i]);
		commandList_->SetGraphicsRootDescriptorTable(2, srvHandleGPU);

		// 描画 
		commandList_->DrawInstanced(3, 1, i * 3, 0);
	}

	commandList_->DrawInstanced(3, 1, 3, 0);

}


void Graphics::InitializeImGui(HWND hwnd)
{
#ifdef USE_IMGUI
	// 1. ImGuiのコンテキスト作成
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	// 2. Win32用プラットフォームバックエンドの初期化
	ImGui_ImplWin32_Init(hwnd);

	// 3. DirectX12用レンダラーバックエンドの初期化
	ImGui_ImplDX12_Init(
		device_.Get(),
		2, // バックバッファ数
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
		srvDescriptorHeap_.Get(),
		srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(),
		srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart()
	);
#endif
}
void Graphics::ShutdownImGui()
{
#ifdef USE_IMGUI
	// ImGuiの終了処理
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
#endif
}

void Graphics::SetTriangleVertices(int index, const TextureVertexData* vertices) {
	// 指定されたインデックスが正常で、バッファがマップされていれば書き込む
	if (index >= 0 && index < kTriangleMaxCount && mappedVertexData_ != nullptr) {
		// 1つの三角形につき3頂点だからindex * 3 の位置から上書きする
		for (int i = 0; i < 3; ++i) {
			mappedVertexData_[index * 3 + i] = vertices[i];
		}
	}
}

void Graphics::SetTriangleTransform(int index, const TransformData& transform) {
	if (index >= 0 && index < kTriangleMaxCount) {
		transform_[index] = transform;
	}
}

void Graphics::SetTriangleTexture(int index, int textureIndex) {
	if (index >= 0 && index < kTriangleMaxCount) {
		selectedTexture_[index] = textureIndex;
	}
}

void Graphics::SetColor(const Vector4& color) {
	color_ = color;
	// GPU側のデータも即座に更新する
	if (materialData_ != nullptr) {
		*materialData_ = color_;
	}
}


int Graphics::LoadTexture(const std::string& filePath)
{
	// 1. キャッシュの確認：既に同じファイルが読み込まれている場合はそのインデックスを返す
	auto it = textureCache_.find(filePath);
	if (it != textureCache_.end()) {
		return it->second;
	}

	// 上限を超えていたらエラーを出して中断
	if (textureCount_ >= kMaxTextures) {
		assert(false && "テクスチャの読み込み上限を超えました");
		return -1;
	}

	// 2. 画像読み込み
	DirectX::ScratchImage mipImages = ::LoadTexture(filePath);
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();

	// 3. GPU用のテクスチャリソースを作成して配列に保存
	textureResources_[textureCount_].Attach(CreateTextureResource(device_.Get(), metadata));

	// 4. データをVRAMに転送し、生ポインタで中間リソースを受け取る
	ID3D12Resource* rawIntermediateResource = UploadTextureData(
		textureResources_[textureCount_].Get(), mipImages, device_.Get(), commandList_.Get());

	// 5. SRVの作成
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU = srvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	UINT descriptorSize = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	srvHandleCPU.ptr += descriptorSize * (1 + textureCount_);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

	device_->CreateShaderResourceView(textureResources_[textureCount_].Get(), &srvDesc, srvHandleCPU);

	// 6. コマンドリストを閉じて実行し、転送完了を待つ (同期処理)
	HRESULT hr = commandList_->Close();
	assert(SUCCEEDED(hr));
	ID3D12CommandList* commandLists[] = { commandList_.Get() };
	commandQueue_->ExecuteCommandLists(1, commandLists);

	fenceValue_++;
	commandQueue_->Signal(fence_.Get(), fenceValue_);
	if (fence_->GetCompletedValue() < fenceValue_) {
		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		WaitForSingleObject(fenceEvent_, INFINITE);
	}

	hr = commandAllocator_->Reset();
	assert(SUCCEEDED(hr));
	hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
	assert(SUCCEEDED(hr));

	// 7. GPUでの転送が終わったので、中間リソースをその場で解放する
	if (rawIntermediateResource) {
		rawIntermediateResource->Release();
	}

	// 8. キャッシュに登録して、ロード数をインクリメント
	textureCache_[filePath] = textureCount_;
	int returnIndex = textureCount_;
	textureCount_++;

	return returnIndex;
}

// --- Sprite用のセッター関数 ---
void Graphics::SetSpriteVertices(int index, const TextureVertexData* vertices) {
	if (index >= 0 && index < kSpriteMaxCount && mappedSpriteVertexData_ != nullptr) {
		// 1スプライトにつき6頂点をコピー
		for (int i = 0; i < 6; ++i) {
			mappedSpriteVertexData_[index * 6 + i] = vertices[i];
		}
	}
}

void Graphics::SetSpriteTransform(int index, const TransformData& transform) {
	if (index >= 0 && index < kSpriteMaxCount) {
		spriteTransform_[index] = transform;
	}
}

void Graphics::SetSpriteTexture(int index, int textureIndex) {
	if (index >= 0 && index < kSpriteMaxCount) {
		spriteSelectedTexture_[index] = textureIndex;
	}
}

void Graphics::SetSpriteColor(int index, const Vector4& color) {
	if (spriteMaterialData_[index] != nullptr) {
		spriteMaterialData_[index]->color = { color.x, color.y, color.z, color.w };
	}
}


// --- Sprite描画処理 ---
void Graphics::DrawSprites() {
	// スプライト用のパイプラインステートをセット
	commandList_->SetPipelineState(spritePipelineState_.Get());

	commandList_->IASetVertexBuffers(0, 1, &spriteVertexBufferView_);
	commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	UINT descriptorSize = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPUStart = srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();

	for (uint32_t i = 0; i < kSpriteMaxCount; ++i) {
		// 各Spriteのマテリアル(色)をセット
		commandList_->SetGraphicsRootConstantBufferView(0, spriteMaterialResource_[i]->GetGPUVirtualAddress());

		// 各SpriteのWVP行列をセット
		commandList_->SetGraphicsRootConstantBufferView(1, spriteWvpResource_[i]->GetGPUVirtualAddress());

		// 各Spriteのテクスチャをバインド
		D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU = srvHandleGPUStart;
		srvHandleGPU.ptr += descriptorSize * (1 + spriteSelectedTexture_[i]);
		commandList_->SetGraphicsRootDescriptorTable(2, srvHandleGPU);

		// 描画 (1スプライト = 6頂点)
		commandList_->DrawInstanced(6, 1, i * 6, 0);
	}
}

void Graphics::InitializeDrawSphere() {
	// 球の分割数
	const uint32_t kSubdivision = 16;
	sphereVertexCount_ = kSubdivision * kSubdivision * 6;

	// 頂点バッファの作成
	size_t vertexBufferSize = sizeof(TextureVertexData) * sphereVertexCount_;
	sphereVertexResource_.Attach(CreateBufferResource(device_.Get(), vertexBufferSize));
	sphereVertexBufferView_.BufferLocation = sphereVertexResource_->GetGPUVirtualAddress();
	sphereVertexBufferView_.SizeInBytes = static_cast<UINT>(vertexBufferSize);
	sphereVertexBufferView_.StrideInBytes = sizeof(TextureVertexData);

	TextureVertexData* mappedData = nullptr;
	sphereVertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));

	const float pi = 3.1415926535f;
	uint32_t index = 0;
	// 球の頂点を緯度・経度で計算して生成
	for (uint32_t latIndex = 0; latIndex < kSubdivision; ++latIndex) {
		float lat = -pi / 2.0f + pi * latIndex / kSubdivision;
		float nextLat = -pi / 2.0f + pi * (latIndex + 1.0f) / kSubdivision;

		for (uint32_t lonIndex = 0; lonIndex < kSubdivision; ++lonIndex) {
			float lon = 2.0f * pi * lonIndex / kSubdivision;
			float nextLon = 2.0f * pi * (lonIndex + 1.0f) / kSubdivision;

			// 球の1頂点を計算するラムダ式
			auto calcVertex = [pi](float u, float v) -> TextureVertexData {
				TextureVertexData vtx;
				vtx.position.x = cos(v) * cos(u);
				vtx.position.y = sin(v);
				vtx.position.z = cos(v) * sin(u);
				vtx.position.w = 1.0f;

				// 法線を設定（単位球なので位置ベクトルがそのまま法線）
				vtx.normal.x = cos(v) * cos(u);
				vtx.normal.y = sin(v);
				vtx.normal.z = cos(v) * sin(u);

				// UV座標の計算
				vtx.texcoord.x = u / (2.0f * pi);
				vtx.texcoord.y = 1.0f - (v + pi / 2.0f) / pi;
				return vtx;
				};

			TextureVertexData a = calcVertex(lon, lat);
			TextureVertexData b = calcVertex(lon, nextLat);
			TextureVertexData c = calcVertex(nextLon, lat);
			TextureVertexData d = calcVertex(nextLon, nextLat);

			// 1つ目の三角形
			mappedData[index++] = a;
			mappedData[index++] = b;
			mappedData[index++] = c;
			// 2つ目の三角形
			mappedData[index++] = c;
			mappedData[index++] = b;
			mappedData[index++] = d;
		}
	}
	sphereVertexResource_->Unmap(0, nullptr);

	// WVPの作成
	sphereWvpResource_.Attach(CreateBufferResource(device_.Get(), sizeof(TransformationMatrix)));
	sphereWvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&sphereWvpData_));

	// DirectionalLight構造体は32バイトなので、256バイトに切り上げます
	UINT directionalLightBufferSize = (sizeof(DirectionalLight) + 255) & ~255;

	// 1. バッファを作成する
	directionalLightResource_ = CreateBufferResource(device_.Get(), directionalLightBufferSize);

	// 2. CPUから書き込めるようにポインタをマッピングする
	directionalLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData_));

	// --- この後に代入を行う ---
	directionalLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	directionalLightData_->direction = { 0.0f, -1.0f, 1.0f };       // 斜め下奥に向かう光
	directionalLightData_->direction.Normalize();                    // Vector3.hの関数でベクトルを正規化(長さを1に)
	directionalLightData_->intensity = 1.0f;                        // 光の強さ（1.0で標準）

	// マテリアルの作成
	UINT materialBufferSize = (sizeof(Material) + 255) & ~255;
	sphereMaterialResource_ = CreateBufferResource(device_.Get(), materialBufferSize);
	sphereMaterialResource_->Map(0, nullptr, reinterpret_cast<void**>(&sphereMaterialData_));
	*sphereMaterialData_ = { 1.0f, 1.0f, 1.0f, 1.0f }; // 初期色は白
	sphereMaterialData_->enableLighting = 1;
}

void Graphics::SetSphereTransform(const TransformData& transform) {
	sphereTransform_ = transform;
}

void Graphics::SetSphereTexture(int textureIndex) {
	sphereSelectedTexture_ = textureIndex;
}

void Graphics::SetSphereColor(const Vector4& color) {
	if (sphereMaterialData_ != nullptr) {
		sphereMaterialData_->color = { color.x, color.y, color.z, color.w };
	}
}

void Graphics::DrawSphere() {
	// WVPの計算
	Matrix4x4 worldMatrix = MakeAffineMatrix(sphereTransform_.scale, sphereTransform_.rotate, sphereTransform_.translate);
	Matrix4x4 cameraMatrix = MakeAffineMatrix({ 1.0f, 1.0f, 1.0f }, cameraTransform_.rotate, cameraTransform_.translate);
	Matrix4x4 viewMatrix = Inverse(cameraMatrix);
	Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, 1280.0f / 720.0f, 0.1f, 100.0f);
	sphereWvpData_->WVP = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));
	sphereWvpData_->World = worldMatrix;

	// 描画コマンドの設定
	commandList_->SetPipelineState(graphicsPipelineState_.Get());
	commandList_->IASetVertexBuffers(0, 1, &sphereVertexBufferView_);
	commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// b0 : マテリアル
	commandList_->SetGraphicsRootConstantBufferView(0, sphereMaterialResource_->GetGPUVirtualAddress());
	// b1 : WVP行列
	commandList_->SetGraphicsRootConstantBufferView(1, sphereWvpResource_->GetGPUVirtualAddress());
	// b2 : ライト情報
	commandList_->SetGraphicsRootConstantBufferView(3, directionalLightResource_->GetGPUVirtualAddress());

	// t0 : テクスチャ
	UINT descriptorSize = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPUStart = srvDescriptorHeap_->GetGPUDescriptorHandleForHeapStart();
	srvHandleGPUStart.ptr += descriptorSize * sphereSelectedTexture_;
	commandList_->SetGraphicsRootDescriptorTable(2, srvHandleGPUStart);

	// ドローコール
	commandList_->DrawInstanced(sphereVertexCount_, 1, 0, 0);
}
