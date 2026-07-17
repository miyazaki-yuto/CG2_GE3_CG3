#include "Graphics.h"

#include "DX12Utility.h"
#include "DebugCamera.h"
#include "DirectXCommon.h"
#include "Model.h"
#include "PrimitiveDrawer.h"
#include "Sprite.h"
#include "TextureManager.h"

#include <cassert>
#include <dxcapi.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "dxgi.lib")

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#endif

Graphics::Graphics() = default;

void Graphics::WaitForGpu() {
    if (dxCommon_ != nullptr) {
        dxCommon_->WaitForGpu();
    }
}

Graphics::~Graphics() {
    // GPUが使用中のリソースを解放しないよう、先に処理完了を待つ。
    WaitForGpu();
    ShutdownImGui();
    // unique_ptrとComPtrのメンバーは宣言と逆順に自動破棄される。
}

void Graphics::Initialize(
    DirectXCommon* dxCommon,
    HWND hWnd,
    int32_t width,
    int32_t height,
    std::ofstream& logStream) {
    assert(dxCommon != nullptr);
    assert(width > 0 && height > 0);

    dxCommon_ = dxCommon;
    windowWidth_ = static_cast<uint32_t>(width);
    windowHeight_ = static_cast<uint32_t>(height);
    DX12Utility::Log(logStream, "Begin Graphics initialization.");

    // 全描画クラスとImGuiが共有するSRVヒープを最初に用意する。
    textureManager_ = std::make_unique<TextureManager>();
    textureManager_->Initialize(dxCommon_->GetDevice());

    // 共有パイプラインを作成してから、利用側の描画クラスを初期化する。
    CreateRootSignature(logStream);
    CreateGraphicsPipelines(logStream);

    // 三角形・球・OBJモデルが同じ視点を共有できるよう、描画クラスより先に作る。
    debugCamera_ = std::make_unique<DebugCamera>();
    debugCamera_->Initialize(windowWidth_, windowHeight_);
    CreateRenderers(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    InitializeImGui(hWnd);

    DX12Utility::Log(logStream, "Complete Graphics initialization.");
}

void Graphics::CreateRootSignature(std::ofstream& logStream) {
    // ルートパラメータの対応:
    // [0]=b0 Material, [1]=b1 TransformationMatrix,
    // [2]=t0 Texture,  [3]=b2 DirectionalLight
    D3D12_ROOT_PARAMETER rootParameters[4]{};

    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[0].Descriptor.ShaderRegister = 0;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[1].Descriptor.ShaderRegister = 1;

    D3D12_DESCRIPTOR_RANGE textureRange{};
    textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureRange.NumDescriptors = 1;
    textureRange.BaseShaderRegister = 0;
    textureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].DescriptorTable.pDescriptorRanges = &textureRange;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;

    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[3].Descriptor.ShaderRegister = 2;

    // HLSLのs0に対応する、テクスチャ用の固定サンプラー。
    D3D12_STATIC_SAMPLER_DESC staticSampler{};
    staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSampler.MaxLOD = D3D12_FLOAT32_MAX;
    staticSampler.ShaderRegister = 0;
    staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumParameters = _countof(rootParameters);
    rootSignatureDesc.pStaticSamplers = &staticSampler;
    rootSignatureDesc.NumStaticSamplers = 1;

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signatureBlob,
        &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob != nullptr) {
            DX12Utility::Log(
                logStream,
                static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        assert(false);
        return;
    }

    hr = dxCommon_->GetDevice()->CreateRootSignature(
        0,
        signatureBlob->GetBufferPointer(),
        signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    assert(SUCCEEDED(hr));
}

void Graphics::CreateGraphicsPipelines(std::ofstream& logStream) {
    // DXC関連オブジェクトはシェーダーコンパイル中だけ必要なので、メンバーに保持しない。
    Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils;
    Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler;
    Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;

    HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
    assert(SUCCEEDED(hr));
    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
    assert(SUCCEEDED(hr));
    hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
    assert(SUCCEEDED(hr));

    Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = DX12Utility::CompileShader(
        L"Object3d.VS.hlsl",
        L"vs_6_0",
        dxcUtils.Get(),
        dxcCompiler.Get(),
        includeHandler.Get(),
        logStream);
    assert(vertexShaderBlob != nullptr);

    Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = DX12Utility::CompileShader(
        L"Object3d.PS.hlsl",
        L"ps_6_0",
        dxcUtils.Get(),
        dxcCompiler.Get(),
        includeHandler.Get(),
        logStream);
    assert(pixelShaderBlob != nullptr);

    // TextureVertexDataとVertexShaderInputの各要素を対応付ける。
    const D3D12_INPUT_ELEMENT_DESC inputElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_INPUT_LAYOUT_DESC inputLayout{};
    inputLayout.pInputElementDescs = inputElements;
    inputLayout.NumElements = _countof(inputElements);

    // 3Dオブジェクトは不透明描画なので、カラーブレンドを無効にする。
    D3D12_BLEND_DESC objectBlendDesc{};
    objectBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_RASTERIZER_DESC objectRasterizerDesc{};
    objectRasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
    objectRasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    objectRasterizerDesc.DepthClipEnable = TRUE;

    D3D12_DEPTH_STENCIL_DESC objectDepthStencilDesc{};
    objectDepthStencilDesc.DepthEnable = true;
    objectDepthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    objectDepthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};
    pipelineDesc.pRootSignature = rootSignature_.Get();
    pipelineDesc.InputLayout = inputLayout;
    pipelineDesc.VS = {
        vertexShaderBlob->GetBufferPointer(),
        vertexShaderBlob->GetBufferSize()
    };
    pipelineDesc.PS = {
        pixelShaderBlob->GetBufferPointer(),
        pixelShaderBlob->GetBufferSize()
    };
    pipelineDesc.BlendState = objectBlendDesc;
    pipelineDesc.RasterizerState = objectRasterizerDesc;
    pipelineDesc.DepthStencilState = objectDepthStencilDesc;
    pipelineDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pipelineDesc.NumRenderTargets = 1;
    pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    pipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineDesc.SampleDesc.Count = 1;
    pipelineDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(
        &pipelineDesc,
        IID_PPV_ARGS(&object3dPipelineState_));
    assert(SUCCEEDED(hr));

    // Spriteは半透明を扱うため、SrcAlphaで通常のアルファブレンドを行う。
    D3D12_BLEND_DESC spriteBlendDesc{};
    D3D12_RENDER_TARGET_BLEND_DESC& spriteBlend = spriteBlendDesc.RenderTarget[0];
    spriteBlend.BlendEnable = TRUE;
    spriteBlend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    spriteBlend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    spriteBlend.BlendOp = D3D12_BLEND_OP_ADD;
    spriteBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    spriteBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
    spriteBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    spriteBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // 2D矩形は両面描画し、3Dの深度バッファには影響させない。
    D3D12_RASTERIZER_DESC spriteRasterizerDesc = objectRasterizerDesc;
    spriteRasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;

    D3D12_DEPTH_STENCIL_DESC spriteDepthStencilDesc = objectDepthStencilDesc;
    spriteDepthStencilDesc.DepthEnable = false;
    spriteDepthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC spritePipelineDesc = pipelineDesc;
    spritePipelineDesc.BlendState = spriteBlendDesc;
    spritePipelineDesc.RasterizerState = spriteRasterizerDesc;
    spritePipelineDesc.DepthStencilState = spriteDepthStencilDesc;

    hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(
        &spritePipelineDesc,
        IID_PPV_ARGS(&spritePipelineState_));
    assert(SUCCEEDED(hr));
}

void Graphics::CreateRenderers(uint32_t width, uint32_t height) {
    // 個別描画クラスへ、Graphicsが所有する共通オブジェクトを貸し出す。
    primitiveDrawer_ = std::make_unique<PrimitiveDrawer>();
    primitiveDrawer_->Initialize(
        dxCommon_,
        debugCamera_.get(),
        textureManager_.get(),
        rootSignature_.Get(),
        object3dPipelineState_.Get(),
        width,
        height);

    sprite_ = std::make_unique<Sprite>();
    sprite_->Initialize(
        dxCommon_,
        textureManager_.get(),
        rootSignature_.Get(),
        spritePipelineState_.Get(),
        width,
        height);
}

void Graphics::BeginDraw() {
    // バックバッファの遷移とクリアはDirectXCommonへ委譲する。
    dxCommon_->BeginDraw();

    // Drawの呼び出し順を内部スロット0から割り当て直す。
    primitiveDrawer_->BeginFrame();
    sprite_->BeginFrame();

#ifdef USE_IMGUI
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
#endif
}

void Graphics::EndDraw() {
#ifdef USE_IMGUI
    ImGui::Render();

    // PrimitiveDrawer/Spriteが別のヒープを設定する可能性があるため、ImGui描画前に再設定する。
    ID3D12DescriptorHeap* descriptorHeaps[] = { textureManager_->GetSrvHeap() };
    dxCommon_->GetCommandList()->SetDescriptorHeaps(
        _countof(descriptorHeaps), descriptorHeaps);
    ImGui_ImplDX12_RenderDrawData(
        ImGui::GetDrawData(), dxCommon_->GetCommandList());
#endif

    // コマンド実行・Present・GPU待機・次フレーム用Resetを行う。
    dxCommon_->EndDraw();

    // GPUコピー完了後なので、テクスチャ転送用中間バッファを解放できる。
    textureManager_->ReleaseIntermediateResources();
}

void Graphics::InitializeImGui(HWND hWnd) {
#ifdef USE_IMGUI
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    const bool win32Initialized = ImGui_ImplWin32_Init(hWnd);
    assert(win32Initialized);

    const bool dx12Initialized = ImGui_ImplDX12_Init(
        dxCommon_->GetDevice(),
        2,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        textureManager_->GetSrvHeap(),
        textureManager_->GetImGuiSrvHandleCPU(),
        textureManager_->GetImGuiSrvHandleGPU());
    assert(dx12Initialized);

    isImGuiInitialized_ = true;
#else
    static_cast<void>(hWnd);
#endif
}

void Graphics::ShutdownImGui() {
#ifdef USE_IMGUI
    if (!isImGuiInitialized_) {
        return;
    }

    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    isImGuiInitialized_ = false;
#endif
}

int Graphics::LoadTexture(const std::string& filePath) {
    assert(textureManager_ != nullptr);
    return textureManager_->LoadTexture(
        filePath, dxCommon_->GetCommandList());
}

std::unique_ptr<Model> Graphics::CreateModel(const std::string& objFilePath) {
    assert(dxCommon_ != nullptr);
    assert(textureManager_ != nullptr);
    assert(rootSignature_ != nullptr);
    assert(object3dPipelineState_ != nullptr);
    assert(debugCamera_ != nullptr);

    auto model = std::make_unique<Model>();
    if (!model->Initialize(
        dxCommon_,
        debugCamera_.get(),
        textureManager_.get(),
        rootSignature_.Get(),
        object3dPipelineState_.Get(),
        windowWidth_,
        windowHeight_,
        objFilePath)) {
        const std::string message = model->GetLastError() + "\n";
        OutputDebugStringA(message.c_str());
        return nullptr;
    }
    return model;
}
