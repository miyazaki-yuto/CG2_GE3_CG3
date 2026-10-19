#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <array>
#include <cstdint>
#include <iosfwd>
#include <vector>

#include "BlendMode.h"
#include "CommonTypes.h"

class DebugCamera;
class DirectXCommon;
class ShaderManager;
class TextureManager;

// GPUへ1インスタンスとして渡す、板ポリ1枚分の描画データ。
struct ParticleRenderData {
    Vector3 position{};
    Vector2 size{ 1.0f, 1.0f };
    float rotation = 0.0f;
    Color4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
};

// 同じTexture・BlendModeの板ポリを、1回のDrawIndexedInstancedでまとめて描く。
class ParticleDrawer {
public:
    static constexpr uint32_t kMaxParticleCount = 10000;

    ParticleDrawer() = default;
    ~ParticleDrawer() = default;

    ParticleDrawer(const ParticleDrawer&) = delete;
    ParticleDrawer& operator=(const ParticleDrawer&) = delete;

    void Initialize(
        DirectXCommon* dxCommon,
        DebugCamera* camera,
        TextureManager* textureManager,
        ShaderManager* shaderManager,
        std::ostream& logStream);

    void Draw(
        const std::vector<ParticleRenderData>& particles,
        int textureHandle,
        BlendMode blendMode);

private:
    struct ParticleVertex {
        Vector2 position{};
        Vector2 texcoord{};
    };

    struct CameraConstants {
        Matrix4x4 viewProjection = MakeIdentity4x4();
        Vector3 cameraRight{};
        float rightPadding = 0.0f;
        Vector3 cameraUp{};
        float upPadding = 0.0f;
    };

    void CreateRootSignature(std::ostream& logStream);
    void CreatePipelineStates(
        ShaderManager& shaderManager,
        std::ostream& logStream);
    void CreateQuadResources();

    DirectXCommon* dxCommon_ = nullptr;
    DebugCamera* camera_ = nullptr;
    TextureManager* textureManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kBlendModeCount>
        pipelineStates_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
};
