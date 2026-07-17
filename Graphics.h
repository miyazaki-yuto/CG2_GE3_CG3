#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <wrl.h>

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>

class DirectXCommon;
class Model;
class PrimitiveDrawer;
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

    // 読み込んだテクスチャを指定するための番号を返す。
    int LoadTexture(const std::string& filePath);

    // OBJを読み込み、そのモデルのGPUリソースを所有するModelを返す。
    // 読み込みに失敗した場合はnullptrを返す。
    std::unique_ptr<Model> CreateModel(const std::string& objFilePath);

    // 所有権はGraphicsにある。呼び出し側はポインタをdeleteしない。
    TextureManager* GetTextureManager() const { return textureManager_.get(); }
    Sprite* GetSprite() const { return sprite_.get(); }
    PrimitiveDrawer* GetPrimitiveDrawer() const { return primitiveDrawer_.get(); }

private:
    // Initializeを処理の目的ごとに分け、初期化順を読みやすくする。
    void CreateRootSignature(std::ofstream& logStream);
    void CreateGraphicsPipelines(std::ofstream& logStream);
    void CreateRenderers(uint32_t width, uint32_t height);

    // ImGuiはTextureManagerのSRVヒープ先頭1枠を使用する。
    void InitializeImGui(HWND hWnd);
    void ShutdownImGui();
    void WaitForGpu();

    // 所有しない参照。EngineがGraphicsより長く生存させる。
    DirectXCommon* dxCommon_ = nullptr;

    // ルートシグネチャは共有するが、深度・カリング・ブレンド設定は描画用途別に分ける。
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> object3dPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> spritePipelineState_;

    // 生成順・破棄順をGraphicsが管理する描画関連クラス。
    std::unique_ptr<TextureManager> textureManager_;
    std::unique_ptr<PrimitiveDrawer> primitiveDrawer_;
    std::unique_ptr<Sprite> sprite_;

    bool isImGuiInitialized_ = false;
    uint32_t windowWidth_ = 0;
    uint32_t windowHeight_ = 0;
};
