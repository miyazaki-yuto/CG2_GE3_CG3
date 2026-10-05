#include "ParticleDrawer.h"

#include "DX12Utility.h"
#include "DebugCamera.h"
#include "DirectXCommon.h"
#include "ShaderManager.h"
#include "TextureManager.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdexcept>

void ParticleDrawer::Initialize(
    DirectXCommon* dxCommon,
    DebugCamera* camera,
    TextureManager* textureManager,
    ShaderManager* shaderManager,
    std::ostream& logStream) {
    assert(dxCommon != nullptr);
    assert(camera != nullptr);
    assert(textureManager != nullptr);
    assert(shaderManager != nullptr);

    dxCommon_ = dxCommon;
    camera_ = camera;
    textureManager_ = textureManager;
    CreateRootSignature(logStream);
    CreatePipelineStates(*shaderManager, logStream);
    CreateQuadResources();
}

void ParticleDrawer::CreateRootSignature(std::ostream& logStream) {
    D3D12_DESCRIPTOR_RANGE textureRange{};
    textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureRange.NumDescriptors = 1;
    textureRange.BaseShaderRegister = 0;
    textureRange.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[2]{};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[1].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[1].DescriptorTable.pDescriptorRanges = &textureRange;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.pParameters = rootParameters;
    desc.NumParameters = _countof(rootParameters);
    desc.pStaticSamplers = &sampler;
    desc.NumStaticSamplers = 1;

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT result = D3D12SerializeRootSignature(
        &desc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signatureBlob,
        &errorBlob);
    if (FAILED(result)) {
        if (errorBlob != nullptr) {
            DX12Utility::Log(
                logStream,
                static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        throw std::runtime_error("Failed to create the particle root signature.");
    }
    result = dxCommon_->GetDevice()->CreateRootSignature(
        0,
        signatureBlob->GetBufferPointer(),
        signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    if (FAILED(result)) {
        throw std::runtime_error("Failed to create the particle root signature.");
    }
}

void ParticleDrawer::CreatePipelineStates(
    ShaderManager& shaderManager,
    std::ostream& logStream) {
    Microsoft::WRL::ComPtr<IDxcBlob> vertexShader = shaderManager.LoadShader(
        L"Particle.VS.hlsl", L"vs_6_0", logStream);
    Microsoft::WRL::ComPtr<IDxcBlob> pixelShader = shaderManager.LoadShader(
        L"Particle.PS.hlsl", L"ps_6_0", logStream);
    if (vertexShader == nullptr || pixelShader == nullptr) {
        throw std::runtime_error("Failed to compile particle shaders.");
    }

    const D3D12_INPUT_ELEMENT_DESC inputElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "POSITION", 1, DXGI_FORMAT_R32G32B32_FLOAT, 1, 0,
          D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 1, 12,
          D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT, 1, 20,
          D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 24,
          D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    };

    D3D12_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizer.CullMode = D3D12_CULL_MODE_NONE;
    rasterizer.DepthClipEnable = TRUE;

    D3D12_DEPTH_STENCIL_DESC depthStencil{};
    depthStencil.DepthEnable = TRUE;
    depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};
    pipelineDesc.pRootSignature = rootSignature_.Get();
    pipelineDesc.InputLayout = { inputElements, _countof(inputElements) };
    pipelineDesc.VS = {
        vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()
    };
    pipelineDesc.PS = {
        pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()
    };
    pipelineDesc.RasterizerState = rasterizer;
    pipelineDesc.DepthStencilState = depthStencil;
    pipelineDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pipelineDesc.NumRenderTargets = 1;
    pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pipelineDesc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineDesc.SampleDesc.Count = 1;
    pipelineDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    for (size_t index = 0; index < kBlendModeCount; ++index) {
        pipelineDesc.BlendState = DX12Utility::CreateBlendDesc(
            static_cast<BlendMode>(index));
        const HRESULT result =
            dxCommon_->GetDevice()->CreateGraphicsPipelineState(
                &pipelineDesc,
                IID_PPV_ARGS(&pipelineStates_[index]));
        if (FAILED(result)) {
            throw std::runtime_error("Failed to create a particle pipeline state.");
        }
    }
}

void ParticleDrawer::CreateQuadResources() {
    constexpr ParticleVertex vertices[4] = {
        { { -0.5f,  0.5f }, { 0.0f, 0.0f } },
        { {  0.5f,  0.5f }, { 1.0f, 0.0f } },
        { { -0.5f, -0.5f }, { 0.0f, 1.0f } },
        { {  0.5f, -0.5f }, { 1.0f, 1.0f } },
    };
    constexpr uint32_t indices[6] = { 0, 1, 2, 1, 3, 2 };

    vertexResource_ = dxCommon_->CreateStaticBufferResource(
        vertices, sizeof(vertices), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(vertices);
    vertexBufferView_.StrideInBytes = sizeof(ParticleVertex);

    indexResource_ = dxCommon_->CreateStaticBufferResource(
        indices, sizeof(indices), D3D12_RESOURCE_STATE_INDEX_BUFFER);
    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = sizeof(indices);
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
}

void ParticleDrawer::Draw(
    const std::vector<ParticleRenderData>& particles,
    int textureHandle,
    BlendMode blendMode) {
    if (particles.empty() || textureHandle < 0) {
        return;
    }
    assert(dxCommon_ != nullptr);
    assert(camera_ != nullptr);
    assert(textureManager_ != nullptr);

    const uint32_t instanceCount = static_cast<uint32_t>((std::min)(
        particles.size(), static_cast<size_t>(kMaxParticleCount)));
    const size_t instanceBytes =
        sizeof(ParticleRenderData) * static_cast<size_t>(instanceCount);
    const DynamicBufferAllocation instanceAllocation =
        dxCommon_->AllocateDynamicBuffer(instanceBytes, 16);
    std::memcpy(instanceAllocation.cpuAddress, particles.data(), instanceBytes);

    D3D12_VERTEX_BUFFER_VIEW instanceBufferView{};
    instanceBufferView.BufferLocation = instanceAllocation.gpuAddress;
    instanceBufferView.SizeInBytes = static_cast<UINT>(instanceBytes);
    instanceBufferView.StrideInBytes = sizeof(ParticleRenderData);

    const Matrix4x4 inverseView = Inverse(camera_->GetViewMatrix());
    const DynamicBufferAllocation cameraAllocation =
        dxCommon_->AllocateDynamicBuffer(
            sizeof(CameraConstants),
            D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    auto* cameraConstants =
        static_cast<CameraConstants*>(cameraAllocation.cpuAddress);
    cameraConstants->viewProjection = camera_->GetViewProjectionMatrix();
    cameraConstants->cameraRight = {
        inverseView.m[0][0], inverseView.m[0][1], inverseView.m[0][2]
    };
    cameraConstants->cameraUp = {
        inverseView.m[1][0], inverseView.m[1][1], inverseView.m[1][2]
    };

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap* descriptorHeaps[] = {
        textureManager_->GetSrvHeap()
    };
    commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(
        pipelineStates_[GetBlendModeIndex(blendMode)].Get());
    const D3D12_VERTEX_BUFFER_VIEW vertexBuffers[] = {
        vertexBufferView_, instanceBufferView
    };
    commandList->IASetVertexBuffers(0, _countof(vertexBuffers), vertexBuffers);
    commandList->IASetIndexBuffer(&indexBufferView_);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->SetGraphicsRootConstantBufferView(
        0, cameraAllocation.gpuAddress);
    commandList->SetGraphicsRootDescriptorTable(
        1, textureManager_->GetSrvHandleGPU(textureHandle));
    commandList->DrawIndexedInstanced(6, instanceCount, 0, 0, 0);
}
