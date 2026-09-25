#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>

#include "Matrix4x4.h"
#include "CommonTypes.h"

class DirectXCommon;
class DebugCamera;
class LightingManager;
class Material;
class Model;
class PrimitiveDrawer;
class ShaderManager;
class Sprite;
class TextureManager;

// 描画機能全体の窓口。
// 共通パイプラインと各描画クラスの生成順・破棄順だけを管理する。
class Graphics {
public:
    // unique_ptrの対象を前方宣言にするため、コンストラクタ／デストラクタは.cppで定義する。
    Graphics();
    ~Graphics();

    Graphics(const Graphics&) = delete;
    Graphics& operator=(const Graphics&) = delete;

    // DirectXCommonが用意したDevice/CommandListを使って描画機能を初期化する。
    void Initialize(
        DirectXCommon* dxCommon,
        HWND hWnd,
        int32_t width,
        int32_t height,
        std::ofstream& logStream);

    // 1フレームの描画開始・終了。
    void BeginDraw();
    void EndDraw();

    // ImGui Scene/Game Viewへ表示するoff-screen targetの描画開始・終了。
    void BeginEditorViewportDraw(uint32_t width, uint32_t height);
    void EndEditorViewportDraw();

    // Renders depth from the first enabled Directional Light. When no such
    // light exists, false is returned and no shadow pass is started.
    bool BeginDirectionalShadowDraw(uint32_t cascadeIndex);
    void EndDirectionalShadowDraw(uint32_t cascadeIndex);
    const Matrix4x4& GetDirectionalShadowViewProjection(
        uint32_t cascadeIndex) const {
        return directionalShadowViewProjections_[cascadeIndex];
    }
    bool BeginPointShadowDraw(uint32_t cubeFaceIndex);
    void EndPointShadowDraw(uint32_t cubeFaceIndex);
    const Matrix4x4& GetPointShadowViewProjection(
        uint32_t cubeFaceIndex) const {
        return pointShadowViewProjections_[cubeFaceIndex];
    }

    // SwapChain変更後の画面サイズを、カメラと2D投影へ反映する。
    void Resize(uint32_t width, uint32_t height);

    // Scene差し替えなど、GPUリソースをまとめて破棄する直前に使用する。
    void FlushGpu();

    // 読み込んだテクスチャを指定するための番号を返す。
    int LoadTexture(const std::string& filePath, bool useSrgb = true);

    // OBJを読み込み、そのモデルのGPUリソースを所有するModelを返す。
    // 読み込みに失敗した場合はnullptrを返す。
    std::unique_ptr<Model> CreateModel(const std::string& objFilePath);

    // 天球用の描画設定でOBJを生成する。
    // 通常モデルと違い、球の内側を描き、深度バッファには書き込まない。
    std::unique_ptr<Model> CreateSkySphereModel(const std::string& objFilePath);
    std::shared_ptr<Material> CreateMaterial(
        const std::string& shaderName) const;

    // 所有権はGraphicsにある。呼び出し側はポインタをdeleteしない。
    TextureManager* GetTextureManager() const { return textureManager_.get(); }
    ShaderManager* GetShaderManager() const { return shaderManager_.get(); }
    Sprite* GetSprite() const { return sprite_.get(); }
    PrimitiveDrawer* GetPrimitiveDrawer() const { return primitiveDrawer_.get(); }
    DebugCamera* GetDebugCamera() const { return debugCamera_.get(); }
    DebugCamera* GetEditorCamera() const { return editorCamera_.get(); }
    LightingManager* GetLightingManager() const { return lightingManager_.get(); }
    uint64_t GetEditorViewportTextureId() const;
    uint32_t GetEditorViewportTextureWidth() const {
        return editorViewportTextureWidth_;
    }
    uint32_t GetEditorViewportTextureHeight() const {
        return editorViewportTextureHeight_;
    }
    void SetToneMappingMode(ToneMappingMode mode);
    ToneMappingMode GetToneMappingMode() const {
        return toneMappingMode_;
    }
    void SetExposure(float exposure);
    float GetExposure() const { return exposure_; }
    void SetAntiAliasingMode(AntiAliasingMode mode) { antiAliasingMode_ = mode; }
    AntiAliasingMode GetAntiAliasingMode() const { return antiAliasingMode_; }
    void SetSsaoEnabled(bool enabled) { ssaoEnabled_ = enabled; }
    bool IsSsaoEnabled() const { return ssaoEnabled_; }
    void SetSsaoStrength(float strength);
    float GetSsaoStrength() const { return ssaoStrength_; }
    void SetBloomEnabled(bool enabled) { bloomEnabled_ = enabled; }
    bool IsBloomEnabled() const { return bloomEnabled_; }
    void SetBloomIntensity(float intensity);
    float GetBloomIntensity() const { return bloomIntensity_; }
    void SetBloomThreshold(float threshold);
    float GetBloomThreshold() const { return bloomThreshold_; }

private:
    // Initializeを処理の目的ごとに分け、初期化順を読みやすくする。
    void CreateRootSignature(std::ofstream& logStream);
    void CreateGraphicsPipelines(std::ofstream& logStream);
    void CreateRenderers(uint32_t width, uint32_t height);
    void CreateEditorViewportRenderTarget(uint32_t width, uint32_t height);
    void CreateDirectionalShadowMap();
    void CreatePointShadowMap();
    void DrawToneMapping();

    // ImGuiはTextureManagerのSRVヒープ先頭1枠を使用する。
    void InitializeImGui(HWND hWnd);
    void ShutdownImGui();
    void WaitForGpu();

    // 所有しない参照。EngineがGraphicsより長く生存させる。
    DirectXCommon* dxCommon_ = nullptr;

    // ルートシグネチャは共有するが、深度・カリング・ブレンド設定は描画用途別に分ける。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> object3dPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> skySpherePipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> spritePipelineState_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> shadowRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> shadowPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> toneMappingRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> toneMappingPipelineState_;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> editorViewportRtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> editorViewportTexture_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> sceneHdrRtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> sceneHdrTexture_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> directionalShadowDsvHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> directionalShadowMap_;
    Matrix4x4 directionalShadowViewProjections_[4] = {
        MakeIdentity4x4(), MakeIdentity4x4(),
        MakeIdentity4x4(), MakeIdentity4x4()
    };
    UINT directionalShadowDsvDescriptorSize_ = 0;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> pointShadowDsvHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> pointShadowMap_;
    Matrix4x4 pointShadowViewProjections_[6] = {
        MakeIdentity4x4(), MakeIdentity4x4(), MakeIdentity4x4(),
        MakeIdentity4x4(), MakeIdentity4x4(), MakeIdentity4x4()
    };
    UINT pointShadowDsvDescriptorSize_ = 0;

    // 生成順・破棄順をGraphicsが管理する描画関連クラス。
    std::unique_ptr<TextureManager> textureManager_;
    std::unique_ptr<ShaderManager> shaderManager_;
    // 3D描画クラスより先に生成し、後に破棄する共有カメラ。
    std::unique_ptr<DebugCamera> debugCamera_;
    // Scene View camera. It is never modified by Play Mode.
    std::unique_ptr<DebugCamera> editorCamera_;
    // 全ての描画クラスが同じ平行光源を参照する。
    std::unique_ptr<LightingManager> lightingManager_;
    std::unique_ptr<PrimitiveDrawer> primitiveDrawer_;
    std::unique_ptr<Sprite> sprite_;
    std::ostream* logStream_ = nullptr;

    bool isImGuiInitialized_ = false;
    uint32_t windowWidth_ = 0;
    uint32_t windowHeight_ = 0;
    uint32_t editorViewportTextureWidth_ = 0;
    uint32_t editorViewportTextureHeight_ = 0;
    uint32_t activeSceneRenderWidth_ = 0;
    uint32_t activeSceneRenderHeight_ = 0;
    ToneMappingMode toneMappingMode_ = ToneMappingMode::ACES;
    float exposure_ = 0.0f;
    AntiAliasingMode antiAliasingMode_ = AntiAliasingMode::FXAA;
    bool ssaoEnabled_ = true;
    float ssaoStrength_ = 0.7f;
    bool bloomEnabled_ = true;
    float bloomIntensity_ = 0.18f;
    float bloomThreshold_ = 1.0f;
    uint32_t postProcessFrameIndex_ = 0;
    bool isEditorViewportDrawing_ = false;
    bool isDirectionalShadowDrawing_ = false;
    bool isDirectionalShadowPrepared_ = false;
    bool isPointShadowDrawing_ = false;
    bool isPointShadowPrepared_ = false;
    static constexpr uint32_t kDirectionalShadowMapSize = 2048;
    static constexpr uint32_t kDirectionalCascadeCount = 4;
    static constexpr uint32_t kPointShadowMapSize = 1024;
};
