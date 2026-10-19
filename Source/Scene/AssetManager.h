#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

class Graphics;
class Model;

// SceneやComponentがAssetを参照するときに使用する永続ID。
// ファイルパスとは独立しているため、Assetを移動しても値は変わらない。
using AssetGuid = std::string;

enum class AssetType {
    Model,
    Texture,
    Prefab
};

struct AssetInfo {
    AssetGuid guid;
    AssetType type = AssetType::Texture;
    std::string filePath;
};

// モデルとテクスチャをGUIDで一元管理するクラス。
//
// Assetの隣に「画像.png.meta」のようなファイルを作り、GUIDを保存する。
// Assetと.metaを一緒に移動すれば、Scene内のGUID参照はそのまま利用できる。
// Assetだけを移動した場合も、サイズと内容ハッシュが一致すれば.metaを追従させる。
class AssetManager {
public:
    AssetManager() = default;
    ~AssetManager();

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // assetRootDirectory以下にある.metaを走査してGUIDデータベースを構築する。
    bool Initialize(
        Graphics* graphics,
        const std::string& assetRootDirectory = "Resources");
    void RefreshAssets();

    // 初めて選択されたファイルには.metaを作成する。
    // 既に.metaがある場合は、そこに保存済みのGUIDを再利用する。
    AssetGuid ImportModel(
        const std::string& filePath,
        std::string* errorMessage = nullptr);
    AssetGuid ImportTexture(
        const std::string& filePath,
        std::string* errorMessage = nullptr);
    AssetGuid ImportPrefab(
        const std::string& filePath,
        std::string* errorMessage = nullptr);

    // 同じGUIDを何度Loadしても、同じGPUリソースを返す。
    std::shared_ptr<Model> LoadModel(
        const AssetGuid& guid,
        bool skySphere = false,
        std::string* errorMessage = nullptr);
    int LoadTexture(
        const AssetGuid& guid,
        std::string* errorMessage = nullptr);
    // Normal Mapなど、ガンマ補正せず数値として読むTexture用。
    int LoadLinearTexture(
        const AssetGuid& guid,
        std::string* errorMessage = nullptr);

    const AssetInfo* FindAsset(const AssetGuid& guid) const;
    AssetGuid FindGuidByPath(const std::string& filePath) const;
    AssetGuid FindTextureGuidByHandle(int textureHandle) const;
    std::string GetAssetPath(const AssetGuid& guid) const;
    std::size_t GetAssetCount() const { return assetsByGuid_.size(); }
    const std::string& GetLastError() const { return lastError_; }

private:
    int LoadTextureInternal(
        const AssetGuid& guid,
        bool useSrgb,
        std::string* errorMessage);
    AssetGuid ImportAsset(
        const std::string& filePath,
        AssetType expectedType,
        std::string* errorMessage);
    bool ResolveRecordPath(AssetInfo& asset);
    bool IsInsideAssetRoot(const std::string& filePath) const;
    void SetError(
        const std::string& message,
        std::string* errorMessage);

    Graphics* graphics_ = nullptr;
    std::string assetRootDirectory_;
    std::unordered_map<AssetGuid, AssetInfo> assetsByGuid_;
    std::unordered_map<std::string, AssetGuid> guidByNormalizedPath_;

    // Modelは頂点・インデックス等を共有し、Drawごとの行列は動的バッファへ書く。
    // そのため複数GameObjectが同じshared_ptr<Model>を安全に利用できる。
    std::unordered_map<std::string, std::shared_ptr<Model>> modelCache_;
    std::unordered_map<AssetGuid, int> textureCache_;
    std::unordered_map<int, AssetGuid> textureGuidByHandle_;
    std::string lastError_;
};
