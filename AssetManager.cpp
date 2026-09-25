#include "AssetManager.h"

#include "Graphics.h"
#include "Model.h"

#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

namespace {

std::filesystem::path MakePathFromUtf8(const std::string& text) {
    const auto* first = reinterpret_cast<const char8_t*>(text.data());
    const std::u8string utf8(first, first + text.size());
    return std::filesystem::path(utf8);
}

std::string MakeUtf8FromPath(const std::filesystem::path& path) {
    const std::u8string utf8 = path.generic_u8string();
    return std::string(
        reinterpret_cast<const char*>(utf8.data()),
        utf8.size());
}

std::string ToLowerAscii(std::string text) {
    std::transform(
        text.begin(), text.end(), text.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
    return text;
}

std::filesystem::path MakeAbsolutePath(const std::filesystem::path& path) {
    std::error_code errorCode;
    std::filesystem::path absolutePath = std::filesystem::absolute(path, errorCode);
    if (errorCode) {
        return path.lexically_normal();
    }
    return absolutePath.lexically_normal();
}

std::string MakePathKey(const std::filesystem::path& path) {
    // Windowsのパスは大文字小文字を区別しないので、キャッシュキーも小文字にする。
    return ToLowerAscii(MakeUtf8FromPath(MakeAbsolutePath(path)));
}

std::filesystem::path MakeMetaPath(const std::filesystem::path& assetPath) {
    std::filesystem::path metaPath = assetPath;
    metaPath += L".meta";
    return metaPath;
}

const char* ToString(AssetType type) {
    switch (type) {
    case AssetType::Model: return "Model";
    case AssetType::Texture: return "Texture";
    case AssetType::Prefab: return "Prefab";
    }
    return "Unknown";
}

bool TryParseAssetType(const std::string& text, AssetType& type) {
    if (text == "Model") {
        type = AssetType::Model;
        return true;
    }
    if (text == "Texture") {
        type = AssetType::Texture;
        return true;
    }
    if (text == "Prefab") {
        type = AssetType::Prefab;
        return true;
    }
    return false;
}

bool TryGetAssetTypeFromExtension(
    const std::filesystem::path& path,
    AssetType& type) {
    const std::string extension = ToLowerAscii(MakeUtf8FromPath(path.extension()));
    if (extension == ".obj") {
        type = AssetType::Model;
        return true;
    }
    if (extension == ".prefab") {
        type = AssetType::Prefab;
        return true;
    }
    if (extension == ".png" || extension == ".jpg" ||
        extension == ".jpeg" || extension == ".bmp" ||
        extension == ".tif" || extension == ".tiff" ||
        extension == ".dds") {
        type = AssetType::Texture;
        return true;
    }
    return false;
}

std::string ExtractJsonString(
    const std::string& json,
    const std::string& key) {
    const std::string quotedKey = "\"" + key + "\"";
    const size_t keyPosition = json.find(quotedKey);
    if (keyPosition == std::string::npos) {
        return {};
    }
    const size_t colonPosition = json.find(':', keyPosition + quotedKey.size());
    const size_t valueBegin = colonPosition == std::string::npos
        ? std::string::npos
        : json.find('"', colonPosition + 1);
    if (valueBegin == std::string::npos) {
        return {};
    }
    const size_t valueEnd = json.find('"', valueBegin + 1);
    if (valueEnd == std::string::npos) {
        return {};
    }
    return json.substr(valueBegin + 1, valueEnd - valueBegin - 1);
}

bool ExtractJsonUnsigned(
    const std::string& json,
    const std::string& key,
    std::uintmax_t& value) {
    const std::string quotedKey = "\"" + key + "\"";
    const size_t keyPosition = json.find(quotedKey);
    if (keyPosition == std::string::npos) {
        return false;
    }
    const size_t colonPosition = json.find(':', keyPosition + quotedKey.size());
    if (colonPosition == std::string::npos) {
        return false;
    }
    const size_t valueBegin = json.find_first_of("0123456789", colonPosition + 1);
    if (valueBegin == std::string::npos) {
        return false;
    }
    const size_t valueEnd = json.find_first_not_of("0123456789", valueBegin);
    try {
        value = static_cast<std::uintmax_t>(std::stoull(
            json.substr(valueBegin, valueEnd - valueBegin)));
    } catch (...) {
        return false;
    }
    return true;
}

struct LocalFingerprint {
    std::uintmax_t fileSize = 0;
    std::string contentHash;
};

bool CalculateFingerprint(
    const std::filesystem::path& path,
    LocalFingerprint& fingerprint) {
    std::error_code errorCode;
    fingerprint.fileSize = std::filesystem::file_size(path, errorCode);
    if (errorCode) {
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }

    // FNV-1a 64bit。ファイル移動の検出用であり、暗号用途ではない。
    std::uint64_t hash = 14695981039346656037ull;
    std::array<char, 64 * 1024> buffer{};
    while (input.good()) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize readSize = input.gcount();
        for (std::streamsize index = 0; index < readSize; ++index) {
            hash ^= static_cast<unsigned char>(buffer[static_cast<size_t>(index)]);
            hash *= 1099511628211ull;
        }
    }

    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << hash;
    fingerprint.contentHash = output.str();
    return true;
}

bool ReadMetaFile(
    const std::filesystem::path& metaPath,
    AssetGuid& guid,
    AssetType& type,
    LocalFingerprint& fingerprint) {
    std::ifstream input(metaPath, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }
    const std::string json(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    guid = ExtractJsonString(json, "guid");
    const std::string typeText = ExtractJsonString(json, "type");
    fingerprint.contentHash = ExtractJsonString(json, "contentHash");
    ExtractJsonUnsigned(json, "fileSize", fingerprint.fileSize);
    return !guid.empty() && TryParseAssetType(typeText, type);
}

bool WriteMetaFile(
    const std::filesystem::path& metaPath,
    const AssetGuid& guid,
    AssetType type,
    const LocalFingerprint& fingerprint) {
    std::ofstream output(metaPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }
    output
        << "{\n"
        << "  \"guid\": \"" << guid << "\",\n"
        << "  \"type\": \"" << ToString(type) << "\",\n"
        << "  \"fileSize\": " << fingerprint.fileSize << ",\n"
        << "  \"contentHash\": \"" << fingerprint.contentHash << "\"\n"
        << "}\n";
    return output.good();
}

AssetGuid GenerateGuid() {
    GUID value{};
    if (FAILED(CoCreateGuid(&value))) {
        return {};
    }

    char buffer[37]{};
    const int length = std::snprintf(
        buffer,
        sizeof(buffer),
        "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        static_cast<unsigned int>(value.Data1),
        static_cast<unsigned int>(value.Data2),
        static_cast<unsigned int>(value.Data3),
        static_cast<unsigned int>(value.Data4[0]),
        static_cast<unsigned int>(value.Data4[1]),
        static_cast<unsigned int>(value.Data4[2]),
        static_cast<unsigned int>(value.Data4[3]),
        static_cast<unsigned int>(value.Data4[4]),
        static_cast<unsigned int>(value.Data4[5]),
        static_cast<unsigned int>(value.Data4[6]),
        static_cast<unsigned int>(value.Data4[7]));
    return length == 36 ? AssetGuid(buffer) : AssetGuid{};
}

} // namespace

AssetManager::~AssetManager() = default;

bool AssetManager::Initialize(
    Graphics* graphics,
    const std::string& assetRootDirectory) {
    graphics_ = graphics;
    assetRootDirectory_ = MakeUtf8FromPath(
        MakeAbsolutePath(MakePathFromUtf8(assetRootDirectory)));
    lastError_.clear();

    std::error_code errorCode;
    std::filesystem::create_directories(
        MakePathFromUtf8(assetRootDirectory_), errorCode);
    if (graphics_ == nullptr || errorCode) {
        lastError_ = graphics_ == nullptr
            ? "AssetManager requires Graphics."
            : "Asset root directory could not be created: " +
                assetRootDirectory_;
        return false;
    }

    RefreshAssets();
    return true;
}

void AssetManager::RefreshAssets() {
    assetsByGuid_.clear();
    guidByNormalizedPath_.clear();
    lastError_.clear();

    const std::filesystem::path root = MakePathFromUtf8(assetRootDirectory_);
    std::error_code errorCode;
    if (!std::filesystem::is_directory(root, errorCode)) {
        return;
    }

    struct OrphanMeta {
        std::filesystem::path metaPath;
        AssetGuid guid;
        AssetType type = AssetType::Texture;
        LocalFingerprint fingerprint;
    };
    std::vector<OrphanMeta> orphans;

    // まず既存.metaを読み、現在のAssetパスとGUIDを対応付ける。
    std::filesystem::recursive_directory_iterator iterator(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        errorCode);
    const std::filesystem::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(errorCode)) {
        if (errorCode) {
            errorCode.clear();
            continue;
        }
        if (!iterator->is_regular_file(errorCode) ||
            ToLowerAscii(MakeUtf8FromPath(iterator->path().extension())) != ".meta") {
            continue;
        }

        AssetGuid guid;
        AssetType type = AssetType::Texture;
        LocalFingerprint fingerprint;
        if (!ReadMetaFile(iterator->path(), guid, type, fingerprint)) {
            continue;
        }

        std::filesystem::path assetPath = iterator->path();
        assetPath.replace_extension();
        if (!std::filesystem::is_regular_file(assetPath, errorCode)) {
            errorCode.clear();
            orphans.push_back({ iterator->path(), guid, type, fingerprint });
            continue;
        }

        AssetType extensionType = AssetType::Texture;
        if (!TryGetAssetTypeFromExtension(assetPath, extensionType) ||
            extensionType != type || assetsByGuid_.contains(guid)) {
            continue;
        }

        // Assetの中身を編集してもGUIDは変えず、移動検出用ハッシュだけを更新する。
        LocalFingerprint currentFingerprint;
        if (CalculateFingerprint(assetPath, currentFingerprint) &&
            (currentFingerprint.fileSize != fingerprint.fileSize ||
                currentFingerprint.contentHash != fingerprint.contentHash)) {
            WriteMetaFile(
                iterator->path(), guid, type, currentFingerprint);
        }

        AssetInfo info{ guid, type, MakeUtf8FromPath(MakeAbsolutePath(assetPath)) };
        guidByNormalizedPath_[MakePathKey(assetPath)] = guid;
        assetsByGuid_[guid] = std::move(info);
    }

    if (orphans.empty()) {
        return;
    }

    // Assetだけが移動されて旧.metaが残った場合を内容ハッシュで復旧する。
    iterator = std::filesystem::recursive_directory_iterator(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        errorCode);
    for (; iterator != end; iterator.increment(errorCode)) {
        if (errorCode) {
            errorCode.clear();
            continue;
        }
        if (!iterator->is_regular_file(errorCode)) {
            continue;
        }

        AssetType candidateType = AssetType::Texture;
        const std::filesystem::path candidatePath = iterator->path();
        if (!TryGetAssetTypeFromExtension(candidatePath, candidateType) ||
            std::filesystem::exists(MakeMetaPath(candidatePath), errorCode)) {
            errorCode.clear();
            continue;
        }

        LocalFingerprint candidateFingerprint;
        if (!CalculateFingerprint(candidatePath, candidateFingerprint)) {
            continue;
        }

        auto match = std::find_if(
            orphans.begin(), orphans.end(),
            [&](const OrphanMeta& orphan) {
                return orphan.type == candidateType &&
                    orphan.fingerprint.fileSize == candidateFingerprint.fileSize &&
                    !orphan.fingerprint.contentHash.empty() &&
                    orphan.fingerprint.contentHash == candidateFingerprint.contentHash;
            });
        if (match == orphans.end()) {
            continue;
        }

        const std::filesystem::path newMetaPath = MakeMetaPath(candidatePath);
        std::filesystem::rename(match->metaPath, newMetaPath, errorCode);
        if (errorCode) {
            errorCode.clear();
            continue;
        }

        AssetInfo info{
            match->guid,
            match->type,
            MakeUtf8FromPath(MakeAbsolutePath(candidatePath))
        };
        guidByNormalizedPath_[MakePathKey(candidatePath)] = match->guid;
        assetsByGuid_[match->guid] = std::move(info);
        orphans.erase(match);
        if (orphans.empty()) {
            break;
        }
    }
}

AssetGuid AssetManager::ImportModel(
    const std::string& filePath,
    std::string* errorMessage) {
    return ImportAsset(filePath, AssetType::Model, errorMessage);
}

AssetGuid AssetManager::ImportTexture(
    const std::string& filePath,
    std::string* errorMessage) {
    return ImportAsset(filePath, AssetType::Texture, errorMessage);
}

AssetGuid AssetManager::ImportPrefab(
    const std::string& filePath,
    std::string* errorMessage) {
    return ImportAsset(filePath, AssetType::Prefab, errorMessage);
}

AssetGuid AssetManager::ImportAsset(
    const std::string& filePath,
    AssetType expectedType,
    std::string* errorMessage) {
    lastError_.clear();
    if (filePath.empty()) {
        SetError("Asset file path is empty.", errorMessage);
        return {};
    }

    const std::filesystem::path path = MakeAbsolutePath(MakePathFromUtf8(filePath));
    std::error_code errorCode;
    if (!std::filesystem::is_regular_file(path, errorCode)) {
        SetError("Asset file was not found: " + filePath, errorMessage);
        return {};
    }
    if (!IsInsideAssetRoot(MakeUtf8FromPath(path))) {
        SetError(
            "Asset must be inside the Resources directory: " + filePath,
            errorMessage);
        return {};
    }

    AssetType extensionType = AssetType::Texture;
    if (!TryGetAssetTypeFromExtension(path, extensionType) ||
        extensionType != expectedType) {
        SetError(
            std::string("Unsupported ") + ToString(expectedType) +
                " file: " + filePath,
            errorMessage);
        return {};
    }

    const std::string pathKey = MakePathKey(path);
    const auto known = guidByNormalizedPath_.find(pathKey);
    if (known != guidByNormalizedPath_.end()) {
        return known->second;
    }

    const std::filesystem::path metaPath = MakeMetaPath(path);
    AssetGuid guid;
    AssetType metaType = expectedType;
    LocalFingerprint fingerprint;
    if (std::filesystem::is_regular_file(metaPath, errorCode)) {
        if (!ReadMetaFile(metaPath, guid, metaType, fingerprint) ||
            metaType != expectedType) {
            SetError("Asset .meta file is invalid: " + MakeUtf8FromPath(metaPath), errorMessage);
            return {};
        }
        LocalFingerprint currentFingerprint;
        if (CalculateFingerprint(path, currentFingerprint) &&
            (currentFingerprint.fileSize != fingerprint.fileSize ||
                currentFingerprint.contentHash != fingerprint.contentHash)) {
            WriteMetaFile(metaPath, guid, expectedType, currentFingerprint);
        }
    } else {
        guid = GenerateGuid();
        if (guid.empty() || !CalculateFingerprint(path, fingerprint) ||
            !WriteMetaFile(metaPath, guid, expectedType, fingerprint)) {
            SetError("Asset .meta file could not be created: " + MakeUtf8FromPath(metaPath), errorMessage);
            return {};
        }
    }

    const auto duplicateGuid = assetsByGuid_.find(guid);
    if (duplicateGuid != assetsByGuid_.end() &&
        MakePathKey(MakePathFromUtf8(duplicateGuid->second.filePath)) != pathKey) {
        SetError("Duplicate Asset GUID was found: " + guid, errorMessage);
        return {};
    }

    AssetInfo info{ guid, expectedType, MakeUtf8FromPath(path) };
    assetsByGuid_[guid] = info;
    guidByNormalizedPath_[pathKey] = guid;
    return guid;
}

std::shared_ptr<Model> AssetManager::LoadModel(
    const AssetGuid& guid,
    bool skySphere,
    std::string* errorMessage) {
    lastError_.clear();
    const std::string cacheKey = guid + (skySphere ? "|Sky" : "|Object");
    const auto cached = modelCache_.find(cacheKey);
    if (cached != modelCache_.end()) {
        return cached->second;
    }

    auto assetIterator = assetsByGuid_.find(guid);
    if (assetIterator == assetsByGuid_.end()) {
        RefreshAssets();
        assetIterator = assetsByGuid_.find(guid);
    }
    if (assetIterator == assetsByGuid_.end() ||
        assetIterator->second.type != AssetType::Model ||
        !ResolveRecordPath(assetIterator->second)) {
        SetError("Model GUID could not be resolved: " + guid, errorMessage);
        return nullptr;
    }
    // ResolveRecordPath内で再走査された場合にiteratorが無効になるため取り直す。
    assetIterator = assetsByGuid_.find(guid);
    if (assetIterator == assetsByGuid_.end()) {
        SetError("Model GUID could not be resolved: " + guid, errorMessage);
        return nullptr;
    }

    std::unique_ptr<Model> created = skySphere
        ? graphics_->CreateSkySphereModel(assetIterator->second.filePath)
        : graphics_->CreateModel(assetIterator->second.filePath);
    if (created == nullptr) {
        SetError("Model could not be loaded: " + assetIterator->second.filePath, errorMessage);
        return nullptr;
    }

    std::shared_ptr<Model> shared(std::move(created));
    modelCache_[cacheKey] = shared;
    return shared;
}

int AssetManager::LoadTexture(
    const AssetGuid& guid,
    std::string* errorMessage) {
    return LoadTextureInternal(guid, true, errorMessage);
}

int AssetManager::LoadLinearTexture(
    const AssetGuid& guid,
    std::string* errorMessage) {
    return LoadTextureInternal(guid, false, errorMessage);
}

int AssetManager::LoadTextureInternal(
    const AssetGuid& guid,
    bool useSrgb,
    std::string* errorMessage) {
    lastError_.clear();
    const std::string cacheKey = guid + (useSrgb ? "|srgb" : "|linear");
    const auto cached = textureCache_.find(cacheKey);
    if (cached != textureCache_.end()) {
        return cached->second;
    }

    auto assetIterator = assetsByGuid_.find(guid);
    if (assetIterator == assetsByGuid_.end()) {
        RefreshAssets();
        assetIterator = assetsByGuid_.find(guid);
    }
    if (assetIterator == assetsByGuid_.end() ||
        assetIterator->second.type != AssetType::Texture ||
        !ResolveRecordPath(assetIterator->second)) {
        SetError("Texture GUID could not be resolved: " + guid, errorMessage);
        return -1;
    }
    assetIterator = assetsByGuid_.find(guid);
    if (assetIterator == assetsByGuid_.end()) {
        SetError("Texture GUID could not be resolved: " + guid, errorMessage);
        return -1;
    }

    const int textureHandle = graphics_->LoadTexture(
        assetIterator->second.filePath, useSrgb);
    if (textureHandle < 0) {
        SetError("Texture could not be loaded: " + assetIterator->second.filePath, errorMessage);
        return -1;
    }
    textureCache_[cacheKey] = textureHandle;
    textureGuidByHandle_[textureHandle] = guid;
    return textureHandle;
}

const AssetInfo* AssetManager::FindAsset(const AssetGuid& guid) const {
    const auto iterator = assetsByGuid_.find(guid);
    return iterator == assetsByGuid_.end() ? nullptr : &iterator->second;
}

AssetGuid AssetManager::FindGuidByPath(const std::string& filePath) const {
    if (filePath.empty()) {
        return {};
    }
    const auto iterator = guidByNormalizedPath_.find(
        MakePathKey(MakePathFromUtf8(filePath)));
    return iterator == guidByNormalizedPath_.end() ? AssetGuid{} : iterator->second;
}

AssetGuid AssetManager::FindTextureGuidByHandle(int textureHandle) const {
    const auto iterator = textureGuidByHandle_.find(textureHandle);
    return iterator == textureGuidByHandle_.end() ? AssetGuid{} : iterator->second;
}

std::string AssetManager::GetAssetPath(const AssetGuid& guid) const {
    const AssetInfo* asset = FindAsset(guid);
    return asset != nullptr ? asset->filePath : std::string{};
}

bool AssetManager::ResolveRecordPath(AssetInfo& asset) {
    const AssetGuid guid = asset.guid;
    std::error_code errorCode;
    if (std::filesystem::is_regular_file(
        MakePathFromUtf8(asset.filePath), errorCode)) {
        return true;
    }
    RefreshAssets();
    return assetsByGuid_.contains(guid);
}

bool AssetManager::IsInsideAssetRoot(const std::string& filePath) const {
    std::error_code errorCode;
    const std::filesystem::path relative = std::filesystem::relative(
        MakeAbsolutePath(MakePathFromUtf8(filePath)),
        MakeAbsolutePath(MakePathFromUtf8(assetRootDirectory_)),
        errorCode);
    if (errorCode || relative.empty()) {
        return false;
    }
    const auto first = relative.begin();
    return first == relative.end() || *first != L"..";
}

void AssetManager::SetError(
    const std::string& message,
    std::string* errorMessage) {
    lastError_ = message;
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    OutputDebugStringA((message + "\n").c_str());
}
