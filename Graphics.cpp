#include "Graphics.h"

#include "DX12Utility.h"
#include "DebugCamera.h"
#include "DirectXCommon.h"
#include "LightingManager.h"
#include "Material.h"
#include "Model.h"
#include "PrimitiveDrawer.h"
#include "ShaderManager.h"
#include "Sprite.h"
#include "TextureManager.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#endif

namespace {

constexpr float kDirectionalShadowBias = 0.0015f;
constexpr float kDirectionalCascadeSplits[4] = {
    10.0f, 25.0f, 60.0f, 120.0f
};
constexpr float kPointShadowNearClip = 0.1f;
constexpr float kPointShadowBias = 0.003f;

void TransitionResource(
    ID3D12GraphicsCommandList* commandList,
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES stateBefore,
    D3D12_RESOURCE_STATES stateAfter) {
    assert(commandList != nullptr);
    assert(resource != nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = stateBefore;
    barrier.Transition.StateAfter = stateAfter;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);
}

Matrix4x4 MakeDirectionalShadowViewProjection(
    Vector3 lightDirection,
    const Vector3& focus,
    float halfExtent) {
    lightDirection.Normalize();
    const float lightDistance = halfExtent * 2.0f;
    const Vector3 eye = focus -
        lightDirection * lightDistance;

    Vector3 upAxis = { 0.0f, 1.0f, 0.0f };
    if (std::fabs(Vector3::Dot(upAxis, lightDirection)) > 0.99f) {
        upAxis = { 0.0f, 0.0f, 1.0f };
    }
    Vector3 right = Cross(upAxis, lightDirection);
    right.Normalize();
    const Vector3 up = Cross(lightDirection, right);

    Matrix4x4 lightView = MakeIdentity4x4();
    lightView.m[0][0] = right.x;
    lightView.m[0][1] = up.x;
    lightView.m[0][2] = lightDirection.x;
    lightView.m[1][0] = right.y;
    lightView.m[1][1] = up.y;
    lightView.m[1][2] = lightDirection.y;
    lightView.m[2][0] = right.z;
    lightView.m[2][1] = up.z;
    lightView.m[2][2] = lightDirection.z;
    lightView.m[3][0] = -Vector3::Dot(right, eye);
    lightView.m[3][1] = -Vector3::Dot(up, eye);
    lightView.m[3][2] = -Vector3::Dot(lightDirection, eye);

    const Matrix4x4 lightProjection = MakeOrthographicMatrix(
        -halfExtent,
        halfExtent,
        halfExtent,
        -halfExtent,
        0.1f,
        lightDistance * 2.0f);
    return Multiply(lightView, lightProjection);
}

Matrix4x4 MakePointShadowViewProjection(
    const Vector3& lightPosition,
    uint32_t cubeFaceIndex,
    float nearClip,
    float farClip) {
    const Vector3 directions[6] = {
        { 1.0f, 0.0f, 0.0f },
        { -1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        { 0.0f, -1.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f },
        { 0.0f, 0.0f, -1.0f }
    };
    const Vector3 upAxes[6] = {
        { 0.0f, 1.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, -1.0f },
        { 0.0f, 0.0f, 1.0f },
        { 0.0f, 1.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }
    };
    const Vector3 forward = directions[cubeFaceIndex];
    Vector3 right = Cross(upAxes[cubeFaceIndex], forward);
    right.Normalize();
    const Vector3 up = Cross(forward, right);

    Matrix4x4 view = MakeIdentity4x4();
    view.m[0][0] = right.x;
    view.m[0][1] = up.x;
    view.m[0][2] = forward.x;
    view.m[1][0] = right.y;
    view.m[1][1] = up.y;
    view.m[1][2] = forward.y;
    view.m[2][0] = right.z;
    view.m[2][1] = up.z;
    view.m[2][2] = forward.z;
    view.m[3][0] = -Vector3::Dot(right, lightPosition);
    view.m[3][1] = -Vector3::Dot(up, lightPosition);
    view.m[3][2] = -Vector3::Dot(forward, lightPosition);

    constexpr float kHalfPi = 1.57079632679489661923f;
    const Matrix4x4 projection =
        MakePerspectiveFovMatrix(kHalfPi, 1.0f, nearClip, farClip);
    return Multiply(view, projection);
}

} // namespace

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
    logStream_ = &logStream;
    windowWidth_ = static_cast<uint32_t>(width);
    windowHeight_ = static_cast<uint32_t>(height);
    DX12Utility::Log(logStream, "Begin Graphics initialization.");

    // 全描画クラスとImGuiが共有するSRVヒープを最初に用意する。
    textureManager_ = std::make_unique<TextureManager>();
    textureManager_->Initialize(dxCommon_->GetDevice());

    shaderManager_ = std::make_unique<ShaderManager>();
    shaderManager_->Initialize(logStream);

    // 共有パイプラインを作成してから、利用側の描画クラスを初期化する。
    CreateRootSignature(logStream);
    CreateGraphicsPipelines(logStream);
    CreateDirectionalShadowMap();
    CreatePointShadowMap();

    // 三角形・球・OBJモデルが同じ視点を共有できるよう、描画クラスより先に作る。
    debugCamera_ = std::make_unique<DebugCamera>();
    debugCamera_->Initialize(windowWidth_, windowHeight_);
    editorCamera_ = std::make_unique<DebugCamera>();
    editorCamera_->Initialize(windowWidth_, windowHeight_);
    lightingManager_ = std::make_unique<LightingManager>();
    lightingManager_->Initialize(dxCommon_);
    CreateRenderers(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    CreateEditorViewportRenderTarget(windowWidth_, windowHeight_);
    InitializeImGui(hWnd);

    DX12Utility::Log(logStream, "Complete Graphics initialization.");
}

void Graphics::CreateRootSignature(std::ofstream& logStream) {
    // ルートパラメータの対応:
    // [0]=b0 Material, [1]=b1 TransformationMatrix,
    // [2]=t0 Base Color, [3]=b2 LightingData, [4]=t1 Shadow Map,
    // [5]=t2 Normal Map, [6]=t3 Environment Map,
    // [7]=t4 Point Light Shadow Cube, [8]=b3 Custom Material Parameters
    D3D12_ROOT_PARAMETER rootParameters[9]{};

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

    D3D12_DESCRIPTOR_RANGE shadowRange{};
    shadowRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    shadowRange.NumDescriptors = 1;
    shadowRange.BaseShaderRegister = 1;
    shadowRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParameters[4].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[4].DescriptorTable.pDescriptorRanges = &shadowRange;
    rootParameters[4].DescriptorTable.NumDescriptorRanges = 1;

    D3D12_DESCRIPTOR_RANGE normalRange{};
    normalRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    normalRange.NumDescriptors = 1;
    normalRange.BaseShaderRegister = 2;
    normalRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    rootParameters[5].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[5].DescriptorTable.pDescriptorRanges = &normalRange;
    rootParameters[5].DescriptorTable.NumDescriptorRanges = 1;

    D3D12_DESCRIPTOR_RANGE environmentRange{};
    environmentRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    environmentRange.NumDescriptors = 1;
    environmentRange.BaseShaderRegister = 3;
    environmentRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    rootParameters[6].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[6].DescriptorTable.pDescriptorRanges = &environmentRange;
    rootParameters[6].DescriptorTable.NumDescriptorRanges = 1;

    D3D12_DESCRIPTOR_RANGE pointShadowRange{};
    pointShadowRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    pointShadowRange.NumDescriptors = 1;
    pointShadowRange.BaseShaderRegister = 4;
    pointShadowRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    rootParameters[7].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[7].DescriptorTable.pDescriptorRanges =
        &pointShadowRange;
    rootParameters[7].DescriptorTable.NumDescriptorRanges = 1;

    rootParameters[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[8].Descriptor.ShaderRegister = 3;

    // HLSLのs0に対応する、テクスチャ用の固定サンプラー。
    D3D12_STATIC_SAMPLER_DESC staticSamplers[2]{};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    staticSamplers[1].Filter =
        D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    staticSamplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    staticSamplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[1].ShaderRegister = 1;
    staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumParameters = _countof(rootParameters);
    rootSignatureDesc.pStaticSamplers = staticSamplers;
    rootSignatureDesc.NumStaticSamplers = _countof(staticSamplers);

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

    // Post ProcessはHDR ColorとScene Depthを画面全体のShaderへ渡す。
    D3D12_DESCRIPTOR_RANGE toneTextureRange{};
    toneTextureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    toneTextureRange.NumDescriptors = 1;
    toneTextureRange.BaseShaderRegister = 0;
    toneTextureRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_DESCRIPTOR_RANGE toneDepthRange{};
    toneDepthRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    toneDepthRange.NumDescriptors = 1;
    toneDepthRange.BaseShaderRegister = 1;
    toneDepthRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_ROOT_PARAMETER toneRootParameters[3]{};
    toneRootParameters[0].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    toneRootParameters[0].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_PIXEL;
    toneRootParameters[0].DescriptorTable.pDescriptorRanges =
        &toneTextureRange;
    toneRootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
    toneRootParameters[1].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    toneRootParameters[1].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_PIXEL;
    toneRootParameters[1].DescriptorTable.pDescriptorRanges =
        &toneDepthRange;
    toneRootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    toneRootParameters[2].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    toneRootParameters[2].ShaderVisibility =
        D3D12_SHADER_VISIBILITY_PIXEL;
    toneRootParameters[2].Constants.ShaderRegister = 0;
    toneRootParameters[2].Constants.Num32BitValues = 16;

    D3D12_STATIC_SAMPLER_DESC toneSampler{};
    toneSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    toneSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    toneSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    toneSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    toneSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    toneSampler.MaxLOD = D3D12_FLOAT32_MAX;
    toneSampler.ShaderRegister = 0;
    toneSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC toneRootSignatureDesc{};
    toneRootSignatureDesc.pParameters = toneRootParameters;
    toneRootSignatureDesc.NumParameters = _countof(toneRootParameters);
    toneRootSignatureDesc.pStaticSamplers = &toneSampler;
    toneRootSignatureDesc.NumStaticSamplers = 1;
    signatureBlob.Reset();
    errorBlob.Reset();
    hr = D3D12SerializeRootSignature(
        &toneRootSignatureDesc,
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
        IID_PPV_ARGS(&toneMappingRootSignature_));
    assert(SUCCEEDED(hr));

    D3D12_ROOT_PARAMETER shadowRootParameter{};
    shadowRootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    shadowRootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    shadowRootParameter.Descriptor.ShaderRegister = 0;

    D3D12_ROOT_SIGNATURE_DESC shadowRootSignatureDesc{};
    shadowRootSignatureDesc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    shadowRootSignatureDesc.pParameters = &shadowRootParameter;
    shadowRootSignatureDesc.NumParameters = 1;

    signatureBlob.Reset();
    errorBlob.Reset();
    hr = D3D12SerializeRootSignature(
        &shadowRootSignatureDesc,
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
        IID_PPV_ARGS(&shadowRootSignature_));
    assert(SUCCEEDED(hr));
}

void Graphics::CreateGraphicsPipelines(std::ofstream& logStream) {
    // 全描画パスのHLSLはShaderManager経由で取得する。
    assert(shaderManager_ != nullptr);

    Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob =
        shaderManager_->LoadShader(
        L"Object3d.VS.hlsl",
        L"vs_6_0",
        logStream);

    Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob =
        shaderManager_->LoadShader(
        L"Object3d.PS.hlsl",
        L"ps_6_0",
        logStream);

    Microsoft::WRL::ComPtr<IDxcBlob> shadowVertexShaderBlob =
        shaderManager_->LoadShader(
            L"ShadowMap.VS.hlsl",
            L"vs_6_0",
            logStream);

    Microsoft::WRL::ComPtr<IDxcBlob> toneMappingVertexShaderBlob =
        shaderManager_->LoadShader(
            L"ToneMapping.VS.hlsl",
            L"vs_6_0",
            logStream);

    Microsoft::WRL::ComPtr<IDxcBlob> toneMappingPixelShaderBlob =
        shaderManager_->LoadShader(
            L"ToneMapping.PS.hlsl",
            L"ps_6_0",
            logStream);

    if (vertexShaderBlob == nullptr ||
        pixelShaderBlob == nullptr ||
        shadowVertexShaderBlob == nullptr ||
        toneMappingVertexShaderBlob == nullptr ||
        toneMappingPixelShaderBlob == nullptr) {
        throw std::runtime_error(
            "Failed to compile one or more required graphics shaders.");
    }

    HRESULT hr = S_OK;

    // TextureVertexDataとVertexShaderInputの各要素を対応付ける。
    const D3D12_INPUT_ELEMENT_DESC inputElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_INPUT_LAYOUT_DESC inputLayout{};
    inputLayout.pInputElementDescs = inputElements;
    inputLayout.NumElements = _countof(inputElements);

    // MTLのd（不透明度）を反映できるよう、3Dにも通常のアルファブレンドを許可する。
    // alpha=1の不透明モデルは従来と同じ結果になる。
    D3D12_BLEND_DESC objectBlendDesc{};
    D3D12_RENDER_TARGET_BLEND_DESC& objectBlend = objectBlendDesc.RenderTarget[0];
    objectBlend.BlendEnable = TRUE;
    objectBlend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    objectBlend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    objectBlend.BlendOp = D3D12_BLEND_OP_ADD;
    objectBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    objectBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
    objectBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    objectBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

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
    pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineDesc.SampleDesc.Count = 1;
    pipelineDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(
        &pipelineDesc,
        IID_PPV_ARGS(&object3dPipelineState_));
    assert(SUCCEEDED(hr));

    shaderManager_->SetMaterialPipelineTemplate(
        dxCommon_->GetDevice(),
        rootSignature_.Get(),
        pipelineDesc);
    shaderManager_->LoadShaderDefinitions(
        "Resources/Shaders",
        logStream);

    // 天球はカメラが球の内側にいる状態で見るため、表裏のどちらも描画可能にする。
    // また、天球が先に深度を書いてしまうと後から描く3Dモデルを隠すので、
    // 深度テストは行いつつ深度バッファへの書き込みだけを止める。
    D3D12_RASTERIZER_DESC skySphereRasterizerDesc = objectRasterizerDesc;
    skySphereRasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;

    D3D12_DEPTH_STENCIL_DESC skySphereDepthStencilDesc = objectDepthStencilDesc;
    skySphereDepthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC skySpherePipelineDesc = pipelineDesc;
    skySpherePipelineDesc.RasterizerState = skySphereRasterizerDesc;
    skySpherePipelineDesc.DepthStencilState = skySphereDepthStencilDesc;

    hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(
        &skySpherePipelineDesc,
        IID_PPV_ARGS(&skySpherePipelineState_));
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

    // HDR Scene Textureを表示用のsRGB Render Targetへ変換するFullscreen Pass。
    D3D12_RASTERIZER_DESC toneRasterizerDesc{};
    toneRasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    toneRasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
    toneRasterizerDesc.DepthClipEnable = TRUE;
    D3D12_BLEND_DESC toneBlendDesc{};
    toneBlendDesc.RenderTarget[0].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
    D3D12_DEPTH_STENCIL_DESC toneDepthStencilDesc{};
    toneDepthStencilDesc.DepthEnable = FALSE;
    toneDepthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC tonePipelineDesc{};
    tonePipelineDesc.pRootSignature = toneMappingRootSignature_.Get();
    tonePipelineDesc.VS = {
        toneMappingVertexShaderBlob->GetBufferPointer(),
        toneMappingVertexShaderBlob->GetBufferSize()
    };
    tonePipelineDesc.PS = {
        toneMappingPixelShaderBlob->GetBufferPointer(),
        toneMappingPixelShaderBlob->GetBufferSize()
    };
    tonePipelineDesc.BlendState = toneBlendDesc;
    tonePipelineDesc.RasterizerState = toneRasterizerDesc;
    tonePipelineDesc.DepthStencilState = toneDepthStencilDesc;
    tonePipelineDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    tonePipelineDesc.NumRenderTargets = 1;
    tonePipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    tonePipelineDesc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    tonePipelineDesc.SampleDesc.Count = 1;
    tonePipelineDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(
        &tonePipelineDesc,
        IID_PPV_ARGS(&toneMappingPipelineState_));
    assert(SUCCEEDED(hr));

    D3D12_RASTERIZER_DESC shadowRasterizerDesc{};
    shadowRasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    shadowRasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
    shadowRasterizerDesc.DepthBias = 1000;
    shadowRasterizerDesc.DepthBiasClamp = 0.0f;
    shadowRasterizerDesc.SlopeScaledDepthBias = 1.5f;
    shadowRasterizerDesc.DepthClipEnable = TRUE;

    D3D12_DEPTH_STENCIL_DESC shadowDepthStencilDesc{};
    shadowDepthStencilDesc.DepthEnable = TRUE;
    shadowDepthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    shadowDepthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPipelineDesc{};
    shadowPipelineDesc.pRootSignature = shadowRootSignature_.Get();
    shadowPipelineDesc.InputLayout = inputLayout;
    shadowPipelineDesc.VS = {
        shadowVertexShaderBlob->GetBufferPointer(),
        shadowVertexShaderBlob->GetBufferSize()
    };
    shadowPipelineDesc.RasterizerState = shadowRasterizerDesc;
    shadowPipelineDesc.DepthStencilState = shadowDepthStencilDesc;
    shadowPipelineDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    shadowPipelineDesc.NumRenderTargets = 0;
    shadowPipelineDesc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    shadowPipelineDesc.SampleDesc.Count = 1;
    shadowPipelineDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(
        &shadowPipelineDesc,
        IID_PPV_ARGS(&shadowPipelineState_));
    assert(SUCCEEDED(hr));
}

void Graphics::CreateRenderers(uint32_t width, uint32_t height) {
    // 個別描画クラスへ、Graphicsが所有する共通オブジェクトを貸し出す。
    primitiveDrawer_ = std::make_unique<PrimitiveDrawer>();
    primitiveDrawer_->Initialize(
        dxCommon_,
        debugCamera_.get(),
        lightingManager_.get(),
        textureManager_.get(),
        rootSignature_.Get(),
        object3dPipelineState_.Get(),
        shadowRootSignature_.Get(),
        shadowPipelineState_.Get());

    sprite_ = std::make_unique<Sprite>();
    sprite_->Initialize(
        dxCommon_,
        lightingManager_.get(),
        textureManager_.get(),
        rootSignature_.Get(),
        spritePipelineState_.Get(),
        width,
        height);
}

void Graphics::BeginDraw() {
    assert(!isEditorViewportDrawing_);
    if (shaderManager_ != nullptr && logStream_ != nullptr) {
        shaderManager_->UpdateHotReload(*logStream_);
    }
    // バックバッファの遷移とクリアはDirectXCommonへ委譲する。
    dxCommon_->BeginDraw();

    // ライトのGPUアドレスを無効化し、この後のImGui編集を最初のDrawへ反映できるようにする。
    lightingManager_->BeginFrame();

    // Drawの呼び出し順を内部スロット0から割り当て直す。
    primitiveDrawer_->BeginFrame();
    sprite_->BeginFrame();

#ifdef USE_IMGUI
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
#endif
}

void Graphics::CreateEditorViewportRenderTarget(
    uint32_t width,
    uint32_t height) {
    assert(dxCommon_ != nullptr);
    assert(textureManager_ != nullptr);
    assert(width > 0 && height > 0);

    ID3D12Device* device = dxCommon_->GetDevice();
    if (editorViewportRtvHeap_ == nullptr) {
        editorViewportRtvHeap_ = DX12Utility::CreateDescriptorHeap(
            device,
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
            1,
            false);
    }
    if (sceneHdrRtvHeap_ == nullptr) {
        sceneHdrRtvHeap_ = DX12Utility::CreateDescriptorHeap(
            device,
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
            1,
            false);
    }

    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = width;
    resourceDesc.Height = height;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    clearValue.Color[0] = 0.035f;
    clearValue.Color[1] = 0.04f;
    clearValue.Color[2] = 0.05f;
    clearValue.Color[3] = 1.0f;

    editorViewportTexture_.Reset();
    const HRESULT hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(editorViewportTexture_.GetAddressOf()));
    if (FAILED(hr)) {
        throw std::runtime_error(
            "Failed to create the Editor Viewport render target.");
    }

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(
        editorViewportTexture_.Get(),
        &rtvDesc,
        editorViewportRtvHeap_->GetCPUDescriptorHandleForHeapStart());

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(
        editorViewportTexture_.Get(),
        &srvDesc,
        textureManager_->GetEditorViewportSrvHandleCPU());

    // 3D Sceneは16-bit floatへ描き、1.0を超える光量をTone Mappingまで保持する。
    D3D12_RESOURCE_DESC hdrResourceDesc = resourceDesc;
    hdrResourceDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    D3D12_CLEAR_VALUE hdrClearValue{};
    hdrClearValue.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    hdrClearValue.Color[0] = 0.035f;
    hdrClearValue.Color[1] = 0.04f;
    hdrClearValue.Color[2] = 0.05f;
    hdrClearValue.Color[3] = 1.0f;
    sceneHdrTexture_.Reset();
    const HRESULT hdrHr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &hdrResourceDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &hdrClearValue,
        IID_PPV_ARGS(sceneHdrTexture_.GetAddressOf()));
    if (FAILED(hdrHr)) {
        throw std::runtime_error(
            "Failed to create the HDR Scene render target.");
    }

    D3D12_RENDER_TARGET_VIEW_DESC hdrRtvDesc{};
    hdrRtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    hdrRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(
        sceneHdrTexture_.Get(),
        &hdrRtvDesc,
        sceneHdrRtvHeap_->GetCPUDescriptorHandleForHeapStart());

    D3D12_SHADER_RESOURCE_VIEW_DESC hdrSrvDesc{};
    hdrSrvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    hdrSrvDesc.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    hdrSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    hdrSrvDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(
        sceneHdrTexture_.Get(),
        &hdrSrvDesc,
        textureManager_->GetSceneHdrSrvHandleCPU());

    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc{};
    depthSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    depthSrvDesc.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(
        dxCommon_->GetDepthBuffer(),
        &depthSrvDesc,
        textureManager_->GetSceneDepthSrvHandleCPU());

    editorViewportTextureWidth_ = width;
    editorViewportTextureHeight_ = height;
}

void Graphics::CreateDirectionalShadowMap() {
    assert(dxCommon_ != nullptr);
    assert(textureManager_ != nullptr);

    ID3D12Device* device = dxCommon_->GetDevice();
    directionalShadowDsvHeap_ = DX12Utility::CreateDescriptorHeap(
        device,
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
        kDirectionalCascadeCount,
        false);
    directionalShadowDsvDescriptorSize_ =
        device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = kDirectionalShadowMapSize;
    resourceDesc.Height = kDirectionalShadowMapSize;
    resourceDesc.DepthOrArraySize = kDirectionalCascadeCount;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    const HRESULT hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(directionalShadowMap_.GetAddressOf()));
    if (FAILED(hr)) {
        throw std::runtime_error(
            "Failed to create the Directional Light shadow map.");
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
        directionalShadowDsvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t cascadeIndex = 0;
        cascadeIndex < kDirectionalCascadeCount;
        ++cascadeIndex) {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Texture2DArray.FirstArraySlice = cascadeIndex;
        dsvDesc.Texture2DArray.ArraySize = 1;
        device->CreateDepthStencilView(
            directionalShadowMap_.Get(), &dsvDesc, dsvHandle);
        dsvHandle.ptr += directionalShadowDsvDescriptorSize_;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.ArraySize = kDirectionalCascadeCount;
    device->CreateShaderResourceView(
        directionalShadowMap_.Get(),
        &srvDesc,
        textureManager_->GetDirectionalShadowSrvHandleCPU());
}

void Graphics::CreatePointShadowMap() {
    assert(dxCommon_ != nullptr);
    assert(textureManager_ != nullptr);

    ID3D12Device* device = dxCommon_->GetDevice();
    pointShadowDsvHeap_ = DX12Utility::CreateDescriptorHeap(
        device,
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
        6,
        false);
    pointShadowDsvDescriptorSize_ =
        device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = kPointShadowMapSize;
    resourceDesc.Height = kPointShadowMapSize;
    resourceDesc.DepthOrArraySize = 6;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;
    const HRESULT hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(pointShadowMap_.GetAddressOf()));
    if (FAILED(hr)) {
        throw std::runtime_error(
            "Failed to create the Point Light shadow cube map.");
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
        pointShadowDsvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (uint32_t cubeFaceIndex = 0;
        cubeFaceIndex < 6;
        ++cubeFaceIndex) {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Texture2DArray.MipSlice = 0;
        dsvDesc.Texture2DArray.FirstArraySlice = cubeFaceIndex;
        dsvDesc.Texture2DArray.ArraySize = 1;
        device->CreateDepthStencilView(
            pointShadowMap_.Get(), &dsvDesc, dsvHandle);
        dsvHandle.ptr += pointShadowDsvDescriptorSize_;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.MipLevels = 1;
    srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
    device->CreateShaderResourceView(
        pointShadowMap_.Get(),
        &srvDesc,
        textureManager_->GetPointShadowSrvHandleCPU());
}

bool Graphics::BeginDirectionalShadowDraw(uint32_t cascadeIndex) {
    if (cascadeIndex >= kDirectionalCascadeCount) {
        throw std::out_of_range("Directional cascade index is invalid.");
    }
    if (isDirectionalShadowDrawing_ || isEditorViewportDrawing_) {
        throw std::logic_error("Invalid Directional Shadow draw begin.");
    }

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    if (cascadeIndex == 0) {
        DirectionalLight light{};
        uint32_t lightIndex = 0;
        if (!lightingManager_->GetFirstEnabledDirectionalLight(
                light, lightIndex)) {
            isDirectionalShadowPrepared_ = false;
            lightingManager_->SetDirectionalShadow(
                nullptr,
                { kDirectionalCascadeSplits[0], kDirectionalCascadeSplits[1],
                  kDirectionalCascadeSplits[2], kDirectionalCascadeSplits[3] },
                false, 0,
                { 1.0f / static_cast<float>(kDirectionalShadowMapSize),
                  1.0f / static_cast<float>(kDirectionalShadowMapSize) },
                kDirectionalShadowBias);
            return false;
        }

        Vector3 cameraForward =
            debugCamera_->GetTarget() - debugCamera_->GetPosition();
        cameraForward.Normalize();
        float cascadeNear = debugCamera_->GetNearClip();
        for (uint32_t index = 0;
            index < kDirectionalCascadeCount;
            ++index) {
            const float cascadeFar = kDirectionalCascadeSplits[index];
            const Vector3 focus = debugCamera_->GetPosition() +
                cameraForward * ((cascadeNear + cascadeFar) * 0.5f);
            directionalShadowViewProjections_[index] =
                MakeDirectionalShadowViewProjection(
                    light.direction, focus, cascadeFar * 0.75f);
            cascadeNear = cascadeFar;
        }
        const float inverseSize =
            1.0f / static_cast<float>(kDirectionalShadowMapSize);
        lightingManager_->SetDirectionalShadow(
            directionalShadowViewProjections_,
            { kDirectionalCascadeSplits[0], kDirectionalCascadeSplits[1],
              kDirectionalCascadeSplits[2], kDirectionalCascadeSplits[3] },
            true, lightIndex, { inverseSize, inverseSize },
            kDirectionalShadowBias);
        TransitionResource(
            commandList, directionalShadowMap_.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_DEPTH_WRITE);
        isDirectionalShadowPrepared_ = true;
    } else if (!isDirectionalShadowPrepared_) {
        return false;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
        directionalShadowDsvHeap_->GetCPUDescriptorHandleForHeapStart();
    dsvHandle.ptr +=
        static_cast<SIZE_T>(directionalShadowDsvDescriptorSize_) *
        cascadeIndex;
    commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsvHandle);
    commandList->ClearDepthStencilView(
        dsvHandle,
        D3D12_CLEAR_FLAG_DEPTH,
        1.0f,
        0,
        0,
        nullptr);
    dxCommon_->SetViewportAndScissor(
        kDirectionalShadowMapSize, kDirectionalShadowMapSize);
    isDirectionalShadowDrawing_ = true;
    return true;
}

void Graphics::EndDirectionalShadowDraw(uint32_t cascadeIndex) {
    if (!isDirectionalShadowDrawing_) {
        throw std::logic_error("Directional Shadow draw was not started.");
    }
    isDirectionalShadowDrawing_ = false;
    if (cascadeIndex + 1 < kDirectionalCascadeCount) {
        return;
    }

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    TransitionResource(
        commandList,
        directionalShadowMap_.Get(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    dxCommon_->BindSwapChainRenderTarget();
    dxCommon_->SetViewportAndScissor(windowWidth_, windowHeight_);
    isDirectionalShadowPrepared_ = false;
}

bool Graphics::BeginPointShadowDraw(uint32_t cubeFaceIndex) {
    if (cubeFaceIndex >= 6 || isPointShadowDrawing_ ||
        isDirectionalShadowDrawing_ || isEditorViewportDrawing_) {
        throw std::logic_error("Invalid Point Shadow draw begin.");
    }

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    if (cubeFaceIndex == 0) {
        PointLight light{};
        uint32_t lightIndex = 0;
        if (!lightingManager_->GetFirstEnabledPointLight(
                light, lightIndex)) {
            isPointShadowPrepared_ = false;
            lightingManager_->SetPointShadow(
                { 0.0f, 0.0f, 0.0f },
                false,
                0,
                kPointShadowNearClip,
                1.0f,
                kPointShadowBias,
                1.0f / static_cast<float>(kPointShadowMapSize));
            return false;
        }

        const float farClip = (std::max)(
            light.radius, kPointShadowNearClip + 0.1f);
        for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex) {
            pointShadowViewProjections_[faceIndex] =
                MakePointShadowViewProjection(
                    light.position,
                    faceIndex,
                    kPointShadowNearClip,
                    farClip);
        }
        lightingManager_->SetPointShadow(
            light.position,
            true,
            lightIndex,
            kPointShadowNearClip,
            farClip,
            kPointShadowBias,
            1.0f / static_cast<float>(kPointShadowMapSize));
        TransitionResource(
            commandList,
            pointShadowMap_.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_DEPTH_WRITE);
        isPointShadowPrepared_ = true;
    } else if (!isPointShadowPrepared_) {
        return false;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
        pointShadowDsvHeap_->GetCPUDescriptorHandleForHeapStart();
    dsvHandle.ptr +=
        static_cast<SIZE_T>(pointShadowDsvDescriptorSize_) *
        cubeFaceIndex;
    commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsvHandle);
    commandList->ClearDepthStencilView(
        dsvHandle,
        D3D12_CLEAR_FLAG_DEPTH,
        1.0f,
        0,
        0,
        nullptr);
    dxCommon_->SetViewportAndScissor(
        kPointShadowMapSize, kPointShadowMapSize);
    isPointShadowDrawing_ = true;
    return true;
}

void Graphics::EndPointShadowDraw(uint32_t cubeFaceIndex) {
    if (!isPointShadowDrawing_ || cubeFaceIndex >= 6) {
        throw std::logic_error("Point Shadow draw was not started.");
    }
    isPointShadowDrawing_ = false;
    if (cubeFaceIndex < 5) {
        return;
    }

    TransitionResource(
        dxCommon_->GetCommandList(),
        pointShadowMap_.Get(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    isPointShadowPrepared_ = false;
    dxCommon_->BindSwapChainRenderTarget();
    dxCommon_->SetViewportAndScissor(windowWidth_, windowHeight_);
}

void Graphics::BeginEditorViewportDraw(
    uint32_t width,
    uint32_t height) {
    if (isEditorViewportDrawing_ || editorViewportTexture_ == nullptr ||
        sceneHdrTexture_ == nullptr) {
        throw std::logic_error("Invalid Editor Viewport draw begin.");
    }

    const uint32_t renderWidth = (std::clamp)(
        width, 1u, editorViewportTextureWidth_);
    const uint32_t renderHeight = (std::clamp)(
        height, 1u, editorViewportTextureHeight_);
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    TransitionResource(
        commandList,
        sceneHdrTexture_.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        sceneHdrRtvHeap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dxCommon_->GetDepthStencilView();
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
    const float clearColor[] = { 0.035f, 0.04f, 0.05f, 1.0f };
    commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    commandList->ClearDepthStencilView(
        dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    dxCommon_->SetViewportAndScissor(renderWidth, renderHeight);

    debugCamera_->Resize(renderWidth, renderHeight);
    editorCamera_->Resize(renderWidth, renderHeight);
    sprite_->Resize(renderWidth, renderHeight);
    activeSceneRenderWidth_ = renderWidth;
    activeSceneRenderHeight_ = renderHeight;
    isEditorViewportDrawing_ = true;
}

void Graphics::EndEditorViewportDraw() {
    if (!isEditorViewportDrawing_) {
        throw std::logic_error("Editor Viewport draw was not started.");
    }

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    TransitionResource(
        commandList,
        sceneHdrTexture_.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    TransitionResource(
        commandList,
        dxCommon_->GetDepthBuffer(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

#ifdef USE_IMGUI
    // EditorではTone Mapping済みのLDR TextureをImGui::Imageへ渡す。
    TransitionResource(
        commandList,
        editorViewportTexture_.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_CPU_DESCRIPTOR_HANDLE editorRtvHandle =
        editorViewportRtvHeap_->GetCPUDescriptorHandleForHeapStart();
    commandList->OMSetRenderTargets(
        1, &editorRtvHandle, FALSE, nullptr);
    DrawToneMapping();
    TransitionResource(
        commandList,
        editorViewportTexture_.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
#else
    // Releaseでは同じTone Mapping PassをSwapChainへ直接出力する。
    dxCommon_->BindSwapChainRenderTarget();
    DrawToneMapping();
#endif

    TransitionResource(
        commandList,
        dxCommon_->GetDepthBuffer(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);

    dxCommon_->BindSwapChainRenderTarget();
    dxCommon_->SetViewportAndScissor(windowWidth_, windowHeight_);
    isEditorViewportDrawing_ = false;
}

void Graphics::DrawToneMapping() {
    assert(sceneHdrTexture_ != nullptr);
    assert(toneMappingRootSignature_ != nullptr);
    assert(toneMappingPipelineState_ != nullptr);

    struct ToneMappingConstants {
        float exposure;
        int32_t mode;
        float sourceUvScale[2];
        int32_t antiAliasingMode;
        int32_t ssaoEnabled;
        float ssaoStrength;
        int32_t bloomEnabled;
        float bloomIntensity;
        float bloomThreshold;
        float inverseSourceSize[2];
        uint32_t frameIndex;
        float padding[3];
    };
    const ToneMappingConstants constants = {
        exposure_,
        static_cast<int32_t>(toneMappingMode_),
        {
            static_cast<float>(activeSceneRenderWidth_) /
                static_cast<float>(editorViewportTextureWidth_),
            static_cast<float>(activeSceneRenderHeight_) /
                static_cast<float>(editorViewportTextureHeight_)
        },
        static_cast<int32_t>(antiAliasingMode_),
        ssaoEnabled_ ? 1 : 0,
        ssaoStrength_,
        bloomEnabled_ ? 1 : 0,
        bloomIntensity_,
        bloomThreshold_,
        {
            1.0f / static_cast<float>(editorViewportTextureWidth_),
            1.0f / static_cast<float>(editorViewportTextureHeight_)
        },
        postProcessFrameIndex_++,
        { 0.0f, 0.0f, 0.0f }
    };

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap* descriptorHeaps[] = {
        textureManager_->GetSrvHeap()
    };
    commandList->SetDescriptorHeaps(
        _countof(descriptorHeaps), descriptorHeaps);
    commandList->SetGraphicsRootSignature(
        toneMappingRootSignature_.Get());
    commandList->SetPipelineState(toneMappingPipelineState_.Get());
    commandList->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->SetGraphicsRootDescriptorTable(
        0, textureManager_->GetSceneHdrSrvHandleGPU());
    commandList->SetGraphicsRootDescriptorTable(
        1, textureManager_->GetSceneDepthSrvHandleGPU());
    commandList->SetGraphicsRoot32BitConstants(
        2, 16, &constants, 0);
    dxCommon_->SetViewportAndScissor(
        activeSceneRenderWidth_, activeSceneRenderHeight_);
    commandList->DrawInstanced(3, 1, 0, 0);
}

void Graphics::SetToneMappingMode(ToneMappingMode mode) {
    switch (mode) {
    case ToneMappingMode::None:
    case ToneMappingMode::Reinhard:
    case ToneMappingMode::ACES:
        toneMappingMode_ = mode;
        break;
    default:
        toneMappingMode_ = ToneMappingMode::ACES;
        break;
    }
}

void Graphics::SetExposure(float exposure) {
    exposure_ = (std::clamp)(exposure, -10.0f, 10.0f);
}

void Graphics::SetSsaoStrength(float strength) {
    ssaoStrength_ = (std::clamp)(strength, 0.0f, 2.0f);
}

void Graphics::SetBloomIntensity(float intensity) {
    bloomIntensity_ = (std::clamp)(intensity, 0.0f, 3.0f);
}

void Graphics::SetBloomThreshold(float threshold) {
    bloomThreshold_ = (std::clamp)(threshold, 0.0f, 10.0f);
}

uint64_t Graphics::GetEditorViewportTextureId() const {
    if (textureManager_ == nullptr || editorViewportTexture_ == nullptr) {
        return 0;
    }
    return static_cast<uint64_t>(
        textureManager_->GetEditorViewportSrvHandleGPU().ptr);
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

    // コマンドを実行し、次に再利用するフレーム領域だけ必要に応じて待つ。
    dxCommon_->EndDraw();

    // テクスチャ転送用中間バッファは、対応するGPUフェンスが完了したものだけ解放する。
    textureManager_->ReleaseIntermediateResources(
        dxCommon_->GetLastSubmittedFenceValue(),
        dxCommon_->GetCompletedFenceValue());
}

void Graphics::Resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 ||
        (width == windowWidth_ && height == windowHeight_)) {
        return;
    }

    windowWidth_ = width;
    windowHeight_ = height;
    debugCamera_->Resize(width, height);
    editorCamera_->Resize(width, height);
    sprite_->Resize(width, height);
    CreateEditorViewportRenderTarget(width, height);
}

void Graphics::InitializeImGui(HWND hWnd) {
#ifdef USE_IMGUI
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigDockingWithShift = false;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
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

int Graphics::LoadTexture(const std::string& filePath, bool useSrgb) {
    assert(textureManager_ != nullptr);
    return textureManager_->LoadTexture(
        filePath, dxCommon_->GetCommandList(), useSrgb);
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
        lightingManager_.get(),
        textureManager_.get(),
        rootSignature_.Get(),
        object3dPipelineState_.Get(),
        shadowRootSignature_.Get(),
        shadowPipelineState_.Get(),
        objFilePath)) {
        const std::string message = model->GetLastError() + "\n";
        OutputDebugStringA(message.c_str());
        return nullptr;
    }
    model->SetSkySphere(false);
    return model;
}

void Graphics::FlushGpu() {
    WaitForGpu();
}

std::unique_ptr<Model> Graphics::CreateSkySphereModel(
    const std::string& objFilePath) {
    assert(dxCommon_ != nullptr);
    assert(textureManager_ != nullptr);
    assert(rootSignature_ != nullptr);
    assert(skySpherePipelineState_ != nullptr);
    assert(debugCamera_ != nullptr);

    // OBJの読み込み処理は通常Modelと共通で、描画時のPSOだけを天球専用にする。
    auto model = std::make_unique<Model>();
    if (!model->Initialize(
        dxCommon_,
        debugCamera_.get(),
        lightingManager_.get(),
        textureManager_.get(),
        rootSignature_.Get(),
        skySpherePipelineState_.Get(),
        shadowRootSignature_.Get(),
        shadowPipelineState_.Get(),
        objFilePath)) {
        const std::string message = model->GetLastError() + "\n";
        OutputDebugStringA(message.c_str());
        return nullptr;
    }
    model->SetSkySphere(true);
    return model;
}

std::shared_ptr<Material> Graphics::CreateMaterial(
    const std::string& shaderName) const {
    if (shaderManager_ == nullptr ||
        shaderManager_->GetMaterialPipelineState(shaderName) == nullptr) {
        return {};
    }
    return std::make_shared<Material>(
        shaderManager_.get(), shaderName);
}
