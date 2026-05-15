#include "Graphics.h"
#include <cassert>
#include <format>
#include <string>

// 内部だけで使うユーティリティ関数
namespace {
	std::string ConvertString(const std::wstring& str) {
		if (str.empty()) { return std::string(); }
		int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
		std::string result(sizeNeeded, 0);
		WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
		return result;
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
		Log(logStream,ConvertString(std::format(L"Begin CompileShader, path:{}, profile:{}\n", filePath, profile)));
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
		if(shaderError != nullptr && shaderError->GetStringLength() !=0){
			Log(logStream,shaderError->GetStringPointer());
			// 警告・エラーダメゼッタイ
			assert(false);
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
		// バッファの場合はこれらは1にする決まり
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
}

Graphics::~Graphics()
{
	if (fenceEvent_) {
		CloseHandle(fenceEvent_);
	}
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
		//infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

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

	D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc{};
	rtvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvDescriptorHeapDesc.NumDescriptors = 2;
	hr = device_->CreateDescriptorHeap(&rtvDescriptorHeapDesc, IID_PPV_ARGS(&rtvDescriptorHeap_));
	assert(SUCCEEDED(hr));

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

	// dxcCompilerを初期化
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils_));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler_));
	assert(SUCCEEDED(hr));

	// その後
	hr = dxcUtils_->CreateDefaultIncludeHandler(&includeHandler_);
	assert(SUCCEEDED(hr));

	// Vertex Shaderをコンパイルしてバイナリを得る
	vertexShaderBlob_ = CompileShader(L"Object3d.VS.hlsl", L"vs_6_0", dxcUtils_.Get(), dxcCompiler_.Get(), includeHandler_.Get(), logStream);
	assert(vertexShaderBlob_ != nullptr);

	pixelShaderBlob_ = CompileShader(L"Object3d.PS.hlsl", L"ps_6_0", dxcUtils_.Get(), dxcCompiler_.Get(), includeHandler_.Get(), logStream);
	assert(pixelShaderBlob_ != nullptr);

	// RootSignatureの作成
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = 
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// RootParameter作成。複数設定できるので配列。今回は結果1つだけなので長さ1の配列
	D3D12_ROOT_PARAMETER rootParameters[1] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // ConstantBufferView
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // ピクセルシェーダーで使う
	rootParameters[0].Descriptor.ShaderRegister = 0; // レジスタ番号 b0 に紐づけ
	descriptionRootSignature.pParameters = rootParameters; // パラメータをセット
	descriptionRootSignature.NumParameters = _countof(rootParameters);

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

	D3D12_INPUT_ELEMENT_DESC inputElementDescs[1] = {};
	inputElementDescs[0].SemanticName = "POSITION"; // HLSLのセマンティクスと合わせる
	inputElementDescs[0].SemanticIndex = 0; // POSITION の 0 に対応
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT; // float4に対応
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT; // 構造体の先頭からのオフセット

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

	// 裏面(時計回り)を表示しない
	resterizerDesc.CullMode = D3D12_CULL_MODE_BACK;

	// 三角形の中を塗りつぶす
	resterizerDesc.FillMode = D3D12_FILL_MODE_SOLID; 

	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = rootSignature_.Get(); // RootSignature
	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc; // InputLayout
	graphicsPipelineStateDesc.VS = { vertexShaderBlob_->GetBufferPointer(), vertexShaderBlob_->GetBufferSize() }; // VertexShader
	graphicsPipelineStateDesc.PS = { pixelShaderBlob_->GetBufferPointer(), pixelShaderBlob_->GetBufferSize() }; // PixelShader
	graphicsPipelineStateDesc.BlendState = blendDesc; // BlendState
	graphicsPipelineStateDesc.RasterizerState = resterizerDesc; // RasterizerState 

	// 書き込むRTVの情報
	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	// 利用するトポロジ（形状）のタイプ。三角形
	graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	// どのように画面に色を打ち込むかの設定（気にしなくて良い）
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	// 実際に生成
	hr = device_->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState_));
	assert(SUCCEEDED(hr));

	// 実際に頂点リソースを作る
	vertexResource_.Attach(CreateBufferResource(device_.Get(), sizeof(Vector4) * 3));

	// 頂点バッファビューを作成する
	// リソースの先頭のアドレスから使う
	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	// 使用するリソースのサイズは頂点3つ分のサイズ
	vertexBufferView_.SizeInBytes = sizeof(Vector4) * 3;
	// 1頂点あたりのサイズ
	vertexBufferView_.StrideInBytes = sizeof(Vector4);

	// 頂点リソースにデータを書き込む
	Vector4* vertexData = nullptr;
	// 書き込むためのアドレスを取得
	vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

	// 左下
	vertexData[0] = { -0.5f, -0.5f, 0.0f, 1.0f };
	// 上
	vertexData[1] = { 0.0f, 0.5f, 0.0f, 1.0f };
	// 右下
	vertexData[2] = { 0.5f, -0.5f, 0.0f, 1.0f };

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

	// マテリアル用のリソースを作る。今回はcolor1つ分のサイズを用意する
	materialResource_.Attach(CreateBufferResource(device_.Get(), sizeof(Vector4)));

	// マテリアルにデータを書き込む
	Vector4* materialData = nullptr;
	// 書き込むためのアドレスを取得
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
	// 今回は赤を書き込んでみる (Vector4にコンストラクタがないため波括弧で代入)
	*materialData = { 1.0f, 0.0f, 0.0f, 1.0f };
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

	commandList_->OMSetRenderTargets(1, &rtvHandles_[backBufferIndex], false, nullptr);

	float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
	commandList_->ClearRenderTargetView(rtvHandles_[backBufferIndex], clearColor, 0, nullptr);
}

void Graphics::EndDraw()
{
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

	hr = commandAllocator_->Reset();
	assert(SUCCEEDED(hr));
	hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
	assert(SUCCEEDED(hr));
}

void Graphics::Draw(){
	// RootSignatureを設定。これを忘れると描画されない
	commandList_->SetGraphicsRootSignature(rootSignature_.Get());

	commandList_->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());

	// PSOを設定
	commandList_->SetPipelineState(graphicsPipelineState_.Get());

	// ビューポートとシザー矩形を設定
	commandList_->RSSetViewports(1, &viewport_);
	commandList_->RSSetScissorRects(1, &scissorRect_);

	// 頂点バッファを設定
	commandList_->IASetVertexBuffers(0, 1, &vertexBufferView_);

	// 形状を設定。三角形リストとして描画
	commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// 描画！ (3頂点、1インスタンス、0開始インデックス、0開始インスタンス)
	commandList_->DrawInstanced(3, 1, 0, 0);
}

