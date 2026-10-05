#include "Model.h"

#include "DebugCamera.h"
#include "DirectXCommon.h"
#include "LightingManager.h"
#include "Material.h"
#include "Matrix4x4.h"
#include "TextureManager.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace {

// OBJでUVまたは法線が省略されていることを表す、実在しないインデックス。
constexpr size_t kMissingObjIndex = static_cast<size_t>(-1);

void GenerateTangents(
    std::vector<TextureVertexData>& vertices,
    const std::vector<uint32_t>& indices) {
    std::vector<Vector3> tangentSums(vertices.size());
    std::vector<Vector3> bitangentSums(vertices.size());

    for (size_t index = 0; index + 2 < indices.size(); index += 3) {
        const uint32_t indicesInTriangle[3] = {
            indices[index], indices[index + 1], indices[index + 2]
        };
        const TextureVertexData& vertex0 = vertices[indicesInTriangle[0]];
        const TextureVertexData& vertex1 = vertices[indicesInTriangle[1]];
        const TextureVertexData& vertex2 = vertices[indicesInTriangle[2]];
        const Vector3 position0 = {
            vertex0.position.x, vertex0.position.y, vertex0.position.z
        };
        const Vector3 position1 = {
            vertex1.position.x, vertex1.position.y, vertex1.position.z
        };
        const Vector3 position2 = {
            vertex2.position.x, vertex2.position.y, vertex2.position.z
        };
        const Vector3 edge1 = position1 - position0;
        const Vector3 edge2 = position2 - position0;
        const float deltaU1 = vertex1.texcoord.x - vertex0.texcoord.x;
        const float deltaV1 = vertex1.texcoord.y - vertex0.texcoord.y;
        const float deltaU2 = vertex2.texcoord.x - vertex0.texcoord.x;
        const float deltaV2 = vertex2.texcoord.y - vertex0.texcoord.y;
        const float determinant = deltaU1 * deltaV2 - deltaV1 * deltaU2;
        if (std::fabs(determinant) <= 0.000001f) {
            continue;
        }

        const float inverseDeterminant = 1.0f / determinant;
        const Vector3 tangent =
            (edge1 * deltaV2 - edge2 * deltaV1) * inverseDeterminant;
        const Vector3 bitangent =
            (edge2 * deltaU1 - edge1 * deltaU2) * inverseDeterminant;
        for (uint32_t vertexIndex : indicesInTriangle) {
            tangentSums[vertexIndex] = tangentSums[vertexIndex] + tangent;
            bitangentSums[vertexIndex] = bitangentSums[vertexIndex] + bitangent;
        }
    }

    for (size_t vertexIndex = 0; vertexIndex < vertices.size(); ++vertexIndex) {
        const Vector3 normal = vertices[vertexIndex].normal;
        Vector3 tangent = tangentSums[vertexIndex] -
            normal * Vector3::Dot(normal, tangentSums[vertexIndex]);
        if (tangent.Length() <= 0.000001f) {
            const Vector3 helperAxis = std::fabs(normal.y) < 0.999f
                ? Vector3{ 0.0f, 1.0f, 0.0f }
                : Vector3{ 1.0f, 0.0f, 0.0f };
            tangent = Cross(helperAxis, normal);
        }
        tangent.Normalize();
        const float handedness = Vector3::Dot(
            Cross(normal, tangent), bitangentSums[vertexIndex]) < 0.0f
            ? -1.0f
            : 1.0f;
        vertices[vertexIndex].tangent = {
            tangent.x, tangent.y, tangent.z, handedness
        };
    }
}

// 外部APIではUTF-8のstd::stringを使い、Windowsのファイルアクセス時だけ
// std::filesystem::pathへ変換する。日本語を含む絶対パスでも開けるようにする。
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

#if 0
// Assimp移行前の自作OBJ/MTL Parser。比較参照用として一時的に残すが使用しない。
// OBJでは位置・UV・法線が別々のインデックスを持つため、3つの組をキーにする。
struct ObjVertexKey {
    size_t positionIndex;
    size_t texcoordIndex;
    size_t normalIndex;

    bool operator==(const ObjVertexKey& other) const {
        return positionIndex == other.positionIndex &&
            texcoordIndex == other.texcoordIndex &&
            normalIndex == other.normalIndex;
    }
};

struct ObjVertexKeyHash {
    size_t operator()(const ObjVertexKey& key) const {
        size_t result = std::hash<size_t>{}(key.positionIndex);
        result ^= std::hash<size_t>{}(key.texcoordIndex) +
            0x9e3779b9u + (result << 6) + (result >> 2);
        result ^= std::hash<size_t>{}(key.normalIndex) +
            0x9e3779b9u + (result << 6) + (result >> 2);
        return result;
    }
};

struct ObjVertexReference {
    size_t positionIndex = 0;
    size_t texcoordIndex = kMissingObjIndex;
    size_t normalIndex = kMissingObjIndex;
};

// OBJのインデックスは1始まり。負数の場合は配列の末尾から数える。
bool ResolveObjIndex(const std::string& text, size_t elementCount, size_t& result) {
    if (text.empty() || elementCount == 0) {
        return false;
    }

    try {
        const long long objIndex = std::stoll(text);
        if (objIndex == 0) {
            return false;
        }

        const long long resolvedIndex = objIndex > 0
            ? objIndex - 1
            : static_cast<long long>(elementCount) + objIndex;
        if (resolvedIndex < 0 ||
            resolvedIndex >= static_cast<long long>(elementCount)) {
            return false;
        }

        result = static_cast<size_t>(resolvedIndex);
        return true;
    } catch (...) {
        return false;
    }
}

// 位置/UV/法線、位置//法線、位置/UV、位置を分解する。
bool ParseFaceVertex(
    const std::string& token,
    size_t positionCount,
    size_t texcoordCount,
    size_t normalCount,
    ObjVertexReference& result) {
    const size_t firstSlash = token.find('/');
    const size_t secondSlash = firstSlash == std::string::npos
        ? std::string::npos
        : token.find('/', firstSlash + 1);

    const std::string positionText = token.substr(0, firstSlash);
    if (!ResolveObjIndex(positionText, positionCount, result.positionIndex)) {
        return false;
    }

    if (firstSlash != std::string::npos) {
        const size_t texcoordStart = firstSlash + 1;
        const size_t texcoordLength = secondSlash == std::string::npos
            ? std::string::npos
            : secondSlash - texcoordStart;
        const std::string texcoordText = token.substr(texcoordStart, texcoordLength);
        if (!texcoordText.empty() &&
            !ResolveObjIndex(texcoordText, texcoordCount, result.texcoordIndex)) {
            return false;
        }
    }

    if (secondSlash != std::string::npos) {
        const std::string normalText = token.substr(secondSlash + 1);
        if (!normalText.empty() &&
            !ResolveObjIndex(normalText, normalCount, result.normalIndex)) {
            return false;
        }
    }

    return true;
}

// MTLのmap_Kdから、OBJに付属するベースカラーテクスチャのパスを取得する。
bool LoadMaterialFile(
    const std::filesystem::path& objDirectory,
    const std::string& materialFileName,
    std::vector<ModelMaterial>& materials,
    std::unordered_map<std::string, uint32_t>& materialLookup) {
    const std::filesystem::path materialPath =
        (objDirectory / MakePathFromUtf8(materialFileName)).lexically_normal();
    std::ifstream materialFile{ materialPath };
    if (!materialFile.is_open()) {
        return false;
    }

    ModelMaterial* currentMaterial = nullptr;
    std::string line;
    while (std::getline(materialFile, line)) {
        std::istringstream lineStream(line);
        std::string identifier;
        lineStream >> identifier;
        if (identifier.empty() || identifier[0] == '#') {
            continue;
        }

        if (identifier == "newmtl") {
            std::string materialName;
            std::getline(lineStream >> std::ws, materialName);
            if (materialName.empty()) {
                currentMaterial = nullptr;
                continue;
            }

            ModelMaterial material{};
            material.name = materialName;
            // MTLの既定値に合わせ、Ksが書かれていなければ鏡面反射なしにする。
            material.specularColor = { 0.0f, 0.0f, 0.0f, 1.0f };
            materials.push_back(material);
            const uint32_t materialIndex =
                static_cast<uint32_t>(materials.size() - 1);
            materialLookup[materialName] = materialIndex;
            currentMaterial = &materials.back();
            continue;
        }

        // newmtlより前にある値は、所属マテリアルが不明なので読み飛ばす。
        if (currentMaterial == nullptr) {
            continue;
        }

        if (identifier == "Kd") {
            lineStream >> currentMaterial->diffuseColor.r
                >> currentMaterial->diffuseColor.g
                >> currentMaterial->diffuseColor.b;
        } else if (identifier == "Ks") {
            lineStream >> currentMaterial->specularColor.r
                >> currentMaterial->specularColor.g
                >> currentMaterial->specularColor.b;
        } else if (identifier == "Ns") {
            lineStream >> currentMaterial->shininess;
            currentMaterial->shininess =
                (std::max)(currentMaterial->shininess, 0.0f);
            // 従来MTLのNsしかない場合も、同じ表面の鋭さをPBRのRoughnessへ近似変換する。
            if (!currentMaterial->hasExplicitRoughness) {
                currentMaterial->roughness = (std::clamp)(
                    std::sqrt(
                        2.0f / (currentMaterial->shininess + 2.0f)),
                    0.04f,
                    1.0f);
            }
        } else if (identifier == "Pm") {
            lineStream >> currentMaterial->metallic;
            currentMaterial->metallic =
                (std::clamp)(currentMaterial->metallic, 0.0f, 1.0f);
        } else if (identifier == "Pr") {
            lineStream >> currentMaterial->roughness;
            currentMaterial->roughness =
                (std::clamp)(currentMaterial->roughness, 0.04f, 1.0f);
            currentMaterial->hasExplicitRoughness = true;
        } else if (identifier == "d") {
            lineStream >> currentMaterial->opacity;
            currentMaterial->opacity =
                (std::clamp)(currentMaterial->opacity, 0.0f, 1.0f);
        } else if (identifier == "map_Kd") {
            std::string textureFileName;
            std::getline(lineStream >> std::ws, textureFileName);
            if (textureFileName.empty()) {
                continue;
            }

            // Blenderが引用符付きで出力したパスにも対応する。
            if (textureFileName.size() >= 2 &&
                textureFileName.front() == '"' &&
                textureFileName.back() == '"') {
                textureFileName = textureFileName.substr(
                    1, textureFileName.size() - 2);
            }

            const std::filesystem::path texturePath =
                (materialPath.parent_path() /
                    MakePathFromUtf8(textureFileName)).lexically_normal();
            currentMaterial->texturePath = MakeUtf8FromPath(texturePath);
        } else if (identifier == "map_Bump" || identifier == "bump" ||
            identifier == "norm") {
            std::string textureFileName;
            std::getline(lineStream >> std::ws, textureFileName);
            if (textureFileName.empty()) {
                continue;
            }

            // map_Bumpには「-bm 1.0」のようなOptionが付く場合がある。
            // 今は強度OptionをMaterialへ持たないため、最後のTokenを画像名として扱う。
            if (textureFileName.front() == '-') {
                std::istringstream optionStream(textureFileName);
                std::string token;
                while (optionStream >> token) {
                    textureFileName = token;
                }
            }
            if (textureFileName.size() >= 2 &&
                textureFileName.front() == '"' &&
                textureFileName.back() == '"') {
                textureFileName = textureFileName.substr(
                    1, textureFileName.size() - 2);
            }
            const std::filesystem::path normalTexturePath =
                (materialPath.parent_path() /
                    MakePathFromUtf8(textureFileName)).lexically_normal();
            currentMaterial->normalTexturePath =
                MakeUtf8FromPath(normalTexturePath);
        }
    }

    return true;
}

// Assimpは列ベクトル、エンジンは行ベクトルで座標変換するため転置して取り込む。
Matrix4x4 ConvertAssimpMatrix(const aiMatrix4x4& matrix) {
    return { {
        { matrix.a1, matrix.b1, matrix.c1, matrix.d1 },
        { matrix.a2, matrix.b2, matrix.c2, matrix.d2 },
        { matrix.a3, matrix.b3, matrix.c3, matrix.d3 },
        { matrix.a4, matrix.b4, matrix.c4, matrix.d4 }
    } };
}
#endif

// Assimpは列ベクトル、エンジンは行ベクトルで座標変換するため転置して取り込む。
Matrix4x4 ConvertAssimpMatrix(const aiMatrix4x4& matrix) {
    return { {
        { matrix.a1, matrix.b1, matrix.c1, matrix.d1 },
        { matrix.a2, matrix.b2, matrix.c2, matrix.d2 },
        { matrix.a3, matrix.b3, matrix.c3, matrix.d3 },
        { matrix.a4, matrix.b4, matrix.c4, matrix.d4 }
    } };
}

ModelNode::Quaternion NormalizeQuaternion(ModelNode::Quaternion value) {
    const float length = std::sqrt(
        value.x * value.x + value.y * value.y +
        value.z * value.z + value.w * value.w);
    if (length <= 0.000001f) {
        return {};
    }
    const float inverseLength = 1.0f / length;
    value.x *= inverseLength;
    value.y *= inverseLength;
    value.z *= inverseLength;
    value.w *= inverseLength;
    return value;
}

ModelNode::Quaternion SlerpQuaternion(
    ModelNode::Quaternion from,
    ModelNode::Quaternion to,
    float amount) {
    from = NormalizeQuaternion(from);
    to = NormalizeQuaternion(to);
    float dot = from.x * to.x + from.y * to.y +
        from.z * to.z + from.w * to.w;
    if (dot < 0.0f) {
        dot = -dot;
        to.x = -to.x;
        to.y = -to.y;
        to.z = -to.z;
        to.w = -to.w;
    }
    dot = (std::clamp)(dot, -1.0f, 1.0f);
    if (dot > 0.9995f) {
        return NormalizeQuaternion({
            from.x + (to.x - from.x) * amount,
            from.y + (to.y - from.y) * amount,
            from.z + (to.z - from.z) * amount,
            from.w + (to.w - from.w) * amount
        });
    }
    const float angle = std::acos(dot);
    const float sine = std::sin(angle);
    const float fromWeight = std::sin((1.0f - amount) * angle) / sine;
    const float toWeight = std::sin(amount * angle) / sine;
    return {
        from.x * fromWeight + to.x * toWeight,
        from.y * fromWeight + to.y * toWeight,
        from.z * fromWeight + to.z * toWeight,
        from.w * fromWeight + to.w * toWeight
    };
}

Matrix4x4 MakeQuaternionMatrix(ModelNode::Quaternion value) {
    value = NormalizeQuaternion(value);
    const float xx = value.x * value.x;
    const float yy = value.y * value.y;
    const float zz = value.z * value.z;
    const float xy = value.x * value.y;
    const float xz = value.x * value.z;
    const float yz = value.y * value.z;
    const float xw = value.x * value.w;
    const float yw = value.y * value.w;
    const float zw = value.z * value.w;
    return { {
        { 1.0f - 2.0f * (yy + zz), 2.0f * (xy + zw),
          2.0f * (xz - yw), 0.0f },
        { 2.0f * (xy - zw), 1.0f - 2.0f * (xx + zz),
          2.0f * (yz + xw), 0.0f },
        { 2.0f * (xz + yw), 2.0f * (yz - xw),
          1.0f - 2.0f * (xx + yy), 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    } };
}

Vector3 SampleVectorKeys(
    const std::vector<ModelVectorKey>& keys,
    float timeSeconds,
    const Vector3& fallback) {
    if (keys.empty()) {
        return fallback;
    }
    if (keys.size() == 1 || timeSeconds <= keys.front().timeSeconds) {
        return keys.front().value;
    }
    for (size_t index = 0; index + 1 < keys.size(); ++index) {
        const ModelVectorKey& from = keys[index];
        const ModelVectorKey& to = keys[index + 1];
        if (timeSeconds <= to.timeSeconds) {
            const float duration = to.timeSeconds - from.timeSeconds;
            const float amount = duration > 0.000001f
                ? (timeSeconds - from.timeSeconds) / duration
                : 0.0f;
            return from.value + (to.value - from.value) * amount;
        }
    }
    return keys.back().value;
}

ModelNode::Quaternion SampleQuaternionKeys(
    const std::vector<ModelQuaternionKey>& keys,
    float timeSeconds,
    const ModelNode::Quaternion& fallback) {
    if (keys.empty()) {
        return fallback;
    }
    if (keys.size() == 1 || timeSeconds <= keys.front().timeSeconds) {
        return keys.front().value;
    }
    for (size_t index = 0; index + 1 < keys.size(); ++index) {
        const ModelQuaternionKey& from = keys[index];
        const ModelQuaternionKey& to = keys[index + 1];
        if (timeSeconds <= to.timeSeconds) {
            const float duration = to.timeSeconds - from.timeSeconds;
            const float amount = duration > 0.000001f
                ? (timeSeconds - from.timeSeconds) / duration
                : 0.0f;
            return SlerpQuaternion(from.value, to.value, amount);
        }
    }
    return keys.back().value;
}

void AddBoneInfluence(
    TextureVertexData& vertex,
    uint32_t boneIndex,
    float weight) {
    float* weights = &vertex.boneWeights.x;
    for (uint32_t influence = 0; influence < 4; ++influence) {
        if (weights[influence] == 0.0f) {
            vertex.boneIndices[influence] = boneIndex;
            weights[influence] = weight;
            return;
        }
    }
    uint32_t lightest = 0;
    for (uint32_t influence = 1; influence < 4; ++influence) {
        if (weights[influence] < weights[lightest]) {
            lightest = influence;
        }
    }
    if (weight > weights[lightest]) {
        vertex.boneIndices[lightest] = boneIndex;
        weights[lightest] = weight;
    }
}

} // namespace

bool Model::Initialize(
    DirectXCommon* dxCommon,
    DebugCamera* debugCamera,
    LightingManager* lightingManager,
    TextureManager* textureManager,
    ID3D12RootSignature* rootSignature,
    const std::array<
        ID3D12PipelineState*, kBlendModeCount>& pipelineStates,
    ID3D12PipelineState* outlinePipelineState,
    ID3D12RootSignature* shadowRootSignature,
    ID3D12PipelineState* shadowPipelineState,
    const std::string& modelFilePath) {
    assert(dxCommon != nullptr);
    assert(debugCamera != nullptr);
    assert(lightingManager != nullptr);
    assert(textureManager != nullptr);
    assert(rootSignature != nullptr);
    for (ID3D12PipelineState* pipelineState : pipelineStates) {
        assert(pipelineState != nullptr);
    }
    assert(outlinePipelineState != nullptr);
    assert(shadowRootSignature != nullptr);
    assert(shadowPipelineState != nullptr);

    dxCommon_ = dxCommon;
    debugCamera_ = debugCamera;
    lightingManager_ = lightingManager;
    textureManager_ = textureManager;
    rootSignature_ = rootSignature;
    for (size_t index = 0; index < pipelineStates_.size(); ++index) {
        pipelineStates_[index] = pipelineStates[index];
    }
    outlinePipelineState_ = outlinePipelineState;
    shadowRootSignature_ = shadowRootSignature;
    shadowPipelineState_ = shadowPipelineState;
    sourcePath_ = modelFilePath;

    std::vector<TextureVertexData> vertices;
    std::vector<uint32_t> indices;
    if (!LoadModelFile(
            modelFilePath, vertices, indices, materialTexturePath_)) {
        return false;
    }

    // 各newmtlのmap_Kdを読み込み、SubMeshごとに使用できるハンドルへ変換する。
    // TextureManagerにはキャッシュがあるため、同じ画像を複数マテリアルが使っても重複しない。
    for (ModelMaterial& material : materials_) {
        if (!material.texturePath.empty()) {
            material.textureHandle = LoadMaterialTexture(
                material.texturePath, true);
        }
        if (!material.normalTexturePath.empty()) {
            // Normal Mapは色ではなく方向データなので、sRGB変換を行わずLinearで読む。
            material.normalTextureHandle = LoadMaterialTexture(
                material.normalTexturePath, false);
        }
        if (!material.metallicRoughnessTexturePath.empty()) {
            material.metallicRoughnessTextureHandle = LoadMaterialTexture(
                material.metallicRoughnessTexturePath, false);
        }
    }

    CreateMeshResources(vertices, indices);
    return true;
}

int Model::LoadMaterialTexture(
    const std::string& texturePath,
    bool useSrgb) {
    const auto embedded = embeddedTextures_.find(texturePath);
    if (embedded == embeddedTextures_.end()) {
        return textureManager_->LoadTexture(
            texturePath, dxCommon_->GetCommandList(), useSrgb);
    }

    const EmbeddedTextureData& texture = embedded->second;
    const std::string cacheKey = sourcePath_ + "#" + texturePath;
    if (texture.isCompressed) {
        return textureManager_->LoadTextureFromMemory(
            cacheKey,
            texture.bytes.data(),
            texture.bytes.size(),
            dxCommon_->GetCommandList(),
            useSrgb);
    }
    return textureManager_->LoadTextureFromRgbaMemory(
        cacheKey,
        texture.bytes.data(),
        texture.width,
        texture.height,
        dxCommon_->GetCommandList(),
        useSrgb);
}

bool Model::LoadModelFile(
    const std::string& filePath,
    std::vector<TextureVertexData>& vertices,
    std::vector<uint32_t>& indices,
    std::string& materialTexturePath) {
#if 0
    const std::filesystem::path objPath = MakePathFromUtf8(filePath).lexically_normal();
    std::ifstream file{ objPath };
    if (!file.is_open()) {
        lastError_ = "OBJ file could not be opened: " + filePath;
        return false;
    }

    std::vector<Vector3> positions;
    std::vector<Vector2> texcoords;
    std::vector<Vector3> normals;
    std::unordered_map<ObjVertexKey, uint32_t, ObjVertexKeyHash> vertexMap;
    std::vector<std::string> materialFileNames;
    std::string currentMaterialName;
    struct PendingSubMesh {
        uint32_t indexStart;
        uint32_t indexCount;
        std::string materialName;
    };
    std::vector<PendingSubMesh> pendingSubMeshes;
    std::string line;
    size_t lineNumber = 0;

    while (std::getline(file, line)) {
        ++lineNumber;
        std::istringstream lineStream(line);
        std::string identifier;
        lineStream >> identifier;
        if (identifier.empty() || identifier[0] == '#') {
            continue;
        }

        if (identifier == "v") {
            Vector3 position;
            if (!(lineStream >> position.x >> position.y >> position.z)) {
                lastError_ = "Invalid vertex position at OBJ line " +
                    std::to_string(lineNumber) + ": " + filePath;
                return false;
            }
            // OBJの右手座標系から、このエンジンの左手座標系へ変換する。
            position.x *= -1.0f;
            positions.push_back(position);
        } else if (identifier == "vt") {
            Vector2 texcoord{};
            if (!(lineStream >> texcoord.x >> texcoord.y)) {
                lastError_ = "Invalid texture coordinate at OBJ line " +
                    std::to_string(lineNumber) + ": " + filePath;
                return false;
            }
            // OBJとDirectXではV軸の向きが逆なので上下を反転する。
            texcoord.y = 1.0f - texcoord.y;
            texcoords.push_back(texcoord);
        } else if (identifier == "vn") {
            Vector3 normal;
            if (!(lineStream >> normal.x >> normal.y >> normal.z)) {
                lastError_ = "Invalid normal at OBJ line " +
                    std::to_string(lineNumber) + ": " + filePath;
                return false;
            }
            normal.x *= -1.0f;
            normal.Normalize();
            normals.push_back(normal);
        } else if (identifier == "mtllib") {
            std::string materialFileName;
            std::getline(lineStream >> std::ws, materialFileName);
            if (!materialFileName.empty()) {
                materialFileNames.push_back(materialFileName);
            }
        } else if (identifier == "usemtl") {
            // この後に現れる面が、どのnewmtlを使うか記録する。
            std::getline(lineStream >> std::ws, currentMaterialName);
        } else if (identifier == "f") {
            std::vector<ObjVertexReference> faceVertices;
            std::string vertexToken;
            while (lineStream >> vertexToken) {
                if (!vertexToken.empty() && vertexToken[0] == '#') {
                    break;
                }
                ObjVertexReference reference;
                if (!ParseFaceVertex(
                    vertexToken,
                    positions.size(),
                    texcoords.size(),
                    normals.size(),
                    reference)) {
                    lastError_ = "Invalid face index at OBJ line " +
                        std::to_string(lineNumber) + ": " + filePath;
                    return false;
                }
                faceVertices.push_back(reference);
            }

            if (faceVertices.size() < 3) {
                lastError_ = "A face has fewer than three vertices at OBJ line " +
                    std::to_string(lineNumber) + ": " + filePath;
                return false;
            }

            // 三角形以外の面は、先頭頂点を共有する三角形の扇形へ分割する。
            for (size_t triangleIndex = 1;
                triangleIndex + 1 < faceVertices.size();
                ++triangleIndex) {
                // X反転後もDirectXの表面方向に合うよう、頂点順を反転する。
                std::array<ObjVertexReference, 3> triangle = {
                    faceVertices[triangleIndex + 1],
                    faceVertices[triangleIndex],
                    faceVertices[0]
                };

                // vnが省略されたOBJでも照明できるよう、面法線を自動生成する。
                bool needsGeneratedNormal = false;
                for (const ObjVertexReference& reference : triangle) {
                    if (reference.normalIndex == kMissingObjIndex) {
                        needsGeneratedNormal = true;
                        break;
                    }
                }
                if (needsGeneratedNormal) {
                    const Vector3& p0 = positions[triangle[0].positionIndex];
                    const Vector3& p1 = positions[triangle[1].positionIndex];
                    const Vector3& p2 = positions[triangle[2].positionIndex];
                    Vector3 generatedNormal = Cross(p1 - p0, p2 - p0);
                    generatedNormal.Normalize();
                    normals.push_back(generatedNormal);
                    const size_t generatedNormalIndex = normals.size() - 1;
                    for (ObjVertexReference& reference : triangle) {
                        if (reference.normalIndex == kMissingObjIndex) {
                            reference.normalIndex = generatedNormalIndex;
                        }
                    }
                }

                // usemtlが変わった場所をSubMeshの境界として記録する。
                // MTLはOBJを最後まで読んだ後で解析し、名前から番号へ解決する。
                if (pendingSubMeshes.empty() ||
                    pendingSubMeshes.back().materialName != currentMaterialName) {
                    pendingSubMeshes.push_back({
                        static_cast<uint32_t>(indices.size()),
                        0,
                        currentMaterialName
                    });
                }

                for (const ObjVertexReference& reference : triangle) {
                    const ObjVertexKey key = {
                        reference.positionIndex,
                        reference.texcoordIndex,
                        reference.normalIndex
                    };
                    const auto found = vertexMap.find(key);
                    if (found != vertexMap.end()) {
                        indices.push_back(found->second);
                        continue;
                    }

                    if (vertices.size() >=
                        static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
                        lastError_ = "OBJ has too many unique vertices: " + filePath;
                        return false;
                    }

                    TextureVertexData vertex{};
                    const Vector3& position = positions[reference.positionIndex];
                    vertex.position = { position.x, position.y, position.z, 1.0f };
                    vertex.texcoord = reference.texcoordIndex == kMissingObjIndex
                        ? Vector2{ 0.0f, 0.0f }
                        : texcoords[reference.texcoordIndex];
                    vertex.normal = normals[reference.normalIndex];

                    const uint32_t newIndex = static_cast<uint32_t>(vertices.size());
                    vertices.push_back(vertex);
                    vertexMap.emplace(key, newIndex);
                    indices.push_back(newIndex);
                }
                pendingSubMeshes.back().indexCount += 3;
            }
        }
    }

    if (vertices.empty() || indices.empty()) {
        lastError_ = "OBJ contains no drawable faces: " + filePath;
        return false;
    }

    // Normal MapをWorld空間へ変換するTBN行列のため、OBJの位置とUVからTangentを作る。
    GenerateTangents(vertices, indices);

    // 0番はMTLがない面、または存在しないusemtl名に使うフォールバック。
    materials_.clear();
    ModelMaterial fallbackMaterial{};
    fallbackMaterial.name = "__default__";
    fallbackMaterial.specularColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    materials_.push_back(fallbackMaterial);

    std::unordered_map<std::string, uint32_t> materialLookup;
    materialLookup[fallbackMaterial.name] = 0;
    for (const std::string& materialFileName : materialFileNames) {
        LoadMaterialFile(
            objPath.parent_path(),
            materialFileName,
            materials_,
            materialLookup);
    }

    subMeshes_.clear();
    for (const PendingSubMesh& pending : pendingSubMeshes) {
        uint32_t materialIndex = 0;
        const auto found = materialLookup.find(pending.materialName);
        if (found != materialLookup.end()) {
            materialIndex = found->second;
        }
        subMeshes_.push_back({
            pending.indexStart,
            pending.indexCount,
            materialIndex
        });
    }

    // 既存APIとの互換用に、最初に見つかったmap_Kdのパスも保持する。
    materialTexturePath.clear();
    for (const ModelMaterial& material : materials_) {
        if (!material.texturePath.empty()) {
            materialTexturePath = material.texturePath;
            break;
        }
    }
    lastError_.clear();
    return true;
#else
    vertices.clear();
    indices.clear();
    materials_.clear();
    subMeshes_.clear();
    nodes_.clear();
    bones_.clear();
    animations_.clear();
    embeddedTextures_.clear();
    materialTexturePath.clear();

    Assimp::Importer importer;
    constexpr unsigned int kImportFlags =
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_ImproveCacheLocality |
        aiProcess_SortByPType |
        aiProcess_ConvertToLeftHanded |
        aiProcess_ValidateDataStructure;
    const aiScene* scene = importer.ReadFile(filePath, kImportFlags);
    if (scene == nullptr || scene->mRootNode == nullptr ||
        (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0) {
        lastError_ = "Assimp failed to load model: " + filePath + "\n" +
            importer.GetErrorString();
        return false;
    }

    // GLBでは画像がファイル内へ埋め込まれる。Importer破棄後もGPU転送できるよう
    // 圧縮バイト列、または展開済みRGBAをModel側へコピーして保持する。
    for (unsigned int textureIndex = 0;
        textureIndex < scene->mNumTextures;
        ++textureIndex) {
        const aiTexture& source = *scene->mTextures[textureIndex];
        EmbeddedTextureData texture{};
        if (source.mHeight == 0) {
            texture.isCompressed = true;
            texture.bytes.resize(source.mWidth);
            std::memcpy(
                texture.bytes.data(), source.pcData, texture.bytes.size());
        } else {
            texture.isCompressed = false;
            texture.width = source.mWidth;
            texture.height = source.mHeight;
            texture.bytes.resize(
                static_cast<size_t>(texture.width) * texture.height * 4);
            for (size_t pixelIndex = 0;
                pixelIndex < static_cast<size_t>(texture.width) * texture.height;
                ++pixelIndex) {
                const aiTexel& texel = source.pcData[pixelIndex];
                texture.bytes[pixelIndex * 4 + 0] = texel.r;
                texture.bytes[pixelIndex * 4 + 1] = texel.g;
                texture.bytes[pixelIndex * 4 + 2] = texel.b;
                texture.bytes[pixelIndex * 4 + 3] = texel.a;
            }
        }
        const std::string indexedName = "*" + std::to_string(textureIndex);
        embeddedTextures_[indexedName] = texture;
        if (source.mFilename.length > 0) {
            embeddedTextures_[source.mFilename.C_Str()] = std::move(texture);
        }
    }

    const std::filesystem::path modelPath =
        MakePathFromUtf8(filePath).lexically_normal();
    const auto resolveTexturePath = [&modelPath, scene](
        const aiMaterial& source,
        aiTextureType textureType) -> std::string {
        if (source.GetTextureCount(textureType) == 0) {
            return {};
        }
        aiString importedPath;
        if (source.GetTexture(textureType, 0, &importedPath) != AI_SUCCESS ||
            importedPath.length == 0) {
            return {};
        }
        if (scene->GetEmbeddedTexture(importedPath.C_Str()) != nullptr) {
            return importedPath.C_Str();
        }
        std::filesystem::path texturePath =
            MakePathFromUtf8(importedPath.C_Str());
        if (texturePath.is_relative()) {
            texturePath = modelPath.parent_path() / texturePath;
        }
        return MakeUtf8FromPath(texturePath.lexically_normal());
    };

    materials_.reserve(scene->mNumMaterials);
    for (unsigned int materialIndex = 0;
        materialIndex < scene->mNumMaterials;
        ++materialIndex) {
        const aiMaterial& source = *scene->mMaterials[materialIndex];
        ModelMaterial material{};

        aiString name;
        if (source.Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
            material.name = name.C_Str();
        }
        if (material.name.empty()) {
            material.name = "Material_" + std::to_string(materialIndex);
        }

        aiColor4D color{};
        if (source.Get(AI_MATKEY_BASE_COLOR, color) == AI_SUCCESS ||
            source.Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
            material.diffuseColor = { color.r, color.g, color.b, color.a };
        }
        if (source.Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS) {
            material.specularColor = { color.r, color.g, color.b, color.a };
        }
        source.Get(AI_MATKEY_SHININESS, material.shininess);
        material.shininess = (std::max)(material.shininess, 0.0f);
        if (source.Get(AI_MATKEY_METALLIC_FACTOR, material.metallic) ==
            AI_SUCCESS) {
            material.metallic = (std::clamp)(material.metallic, 0.0f, 1.0f);
        }
        if (source.Get(AI_MATKEY_ROUGHNESS_FACTOR, material.roughness) ==
            AI_SUCCESS) {
            material.roughness = (std::clamp)(material.roughness, 0.04f, 1.0f);
            material.hasExplicitRoughness = true;
        } else {
            material.roughness = (std::clamp)(
                std::sqrt(2.0f / (material.shininess + 2.0f)),
                0.04f,
                1.0f);
        }
        source.Get(AI_MATKEY_OPACITY, material.opacity);
        material.opacity = (std::clamp)(material.opacity, 0.0f, 1.0f);

        material.texturePath = resolveTexturePath(
            source, aiTextureType_BASE_COLOR);
        if (material.texturePath.empty()) {
            material.texturePath = resolveTexturePath(
                source, aiTextureType_DIFFUSE);
        }
        material.normalTexturePath = resolveTexturePath(
            source, aiTextureType_NORMALS);
        if (material.normalTexturePath.empty()) {
            material.normalTexturePath = resolveTexturePath(
                source, aiTextureType_HEIGHT);
        }
        material.metallicRoughnessTexturePath = resolveTexturePath(
            source, aiTextureType_DIFFUSE_ROUGHNESS);
        if (material.metallicRoughnessTexturePath.empty()) {
            material.metallicRoughnessTexturePath = resolveTexturePath(
                source, aiTextureType_METALNESS);
        }
        materials_.push_back(std::move(material));
    }
    if (materials_.empty()) {
        ModelMaterial fallback{};
        fallback.name = "__default__";
        materials_.push_back(std::move(fallback));
    }

    struct ImportedMeshRange {
        uint32_t indexStart = 0;
        uint32_t indexCount = 0;
        uint32_t materialIndex = 0;
        std::vector<ModelSubMesh::BoneOffset> boneOffsets;
    };
    std::vector<ImportedMeshRange> meshRanges(scene->mNumMeshes);
    std::unordered_map<std::string, uint32_t> boneLookup;

    for (unsigned int meshIndex = 0;
        meshIndex < scene->mNumMeshes;
        ++meshIndex) {
        const aiMesh& mesh = *scene->mMeshes[meshIndex];
        if (mesh.mNumVertices == 0 || mesh.mNumFaces == 0) {
            continue;
        }
        if (vertices.size() + mesh.mNumVertices >
            static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
            lastError_ = "Assimp model has too many vertices: " + filePath;
            return false;
        }

        const uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
        vertices.reserve(vertices.size() + mesh.mNumVertices);
        for (unsigned int vertexIndex = 0;
            vertexIndex < mesh.mNumVertices;
            ++vertexIndex) {
            TextureVertexData vertex{};
            const aiVector3D& position = mesh.mVertices[vertexIndex];
            vertex.position = {
                position.x, position.y, position.z, 1.0f
            };
            if (mesh.HasTextureCoords(0)) {
                const aiVector3D& uv = mesh.mTextureCoords[0][vertexIndex];
                vertex.texcoord = { uv.x, uv.y };
            }
            if (mesh.HasNormals()) {
                const aiVector3D& normal = mesh.mNormals[vertexIndex];
                vertex.normal = { normal.x, normal.y, normal.z };
            } else {
                vertex.normal = { 0.0f, 1.0f, 0.0f };
            }
            if (mesh.HasTangentsAndBitangents()) {
                const aiVector3D& tangent = mesh.mTangents[vertexIndex];
                const aiVector3D& bitangent = mesh.mBitangents[vertexIndex];
                const Vector3 tangentVector = {
                    tangent.x, tangent.y, tangent.z
                };
                const Vector3 bitangentVector = {
                    bitangent.x, bitangent.y, bitangent.z
                };
                const float handedness = Vector3::Dot(
                    Cross(vertex.normal, tangentVector), bitangentVector) < 0.0f
                    ? -1.0f : 1.0f;
                vertex.tangent = {
                    tangent.x, tangent.y, tangent.z, handedness
                };
            }
            vertices.push_back(vertex);
        }

        for (unsigned int boneSlot = 0;
            boneSlot < mesh.mNumBones;
            ++boneSlot) {
            const aiBone& sourceBone = *mesh.mBones[boneSlot];
            const std::string boneName = sourceBone.mName.C_Str();
            uint32_t boneIndex = 0;
            const auto foundBone = boneLookup.find(boneName);
            if (foundBone == boneLookup.end()) {
                if (bones_.size() >= kMaxSkinningBones) {
                    lastError_ = "Model exceeds the 128 bone limit: " + filePath;
                    return false;
                }
                boneIndex = static_cast<uint32_t>(bones_.size());
                boneLookup.emplace(boneName, boneIndex);
                ModelBone bone{};
                bone.name = boneName;
                bones_.push_back(std::move(bone));
            } else {
                boneIndex = foundBone->second;
            }
            meshRanges[meshIndex].boneOffsets.push_back({
                boneIndex,
                ConvertAssimpMatrix(sourceBone.mOffsetMatrix)
            });
            for (unsigned int weightIndex = 0;
                weightIndex < sourceBone.mNumWeights;
                ++weightIndex) {
                const aiVertexWeight& sourceWeight =
                    sourceBone.mWeights[weightIndex];
                if (sourceWeight.mVertexId >= mesh.mNumVertices ||
                    sourceWeight.mWeight <= 0.0f) {
                    continue;
                }
                AddBoneInfluence(
                    vertices[baseVertex + sourceWeight.mVertexId],
                    boneIndex,
                    sourceWeight.mWeight);
            }
        }

        const uint32_t indexStart = static_cast<uint32_t>(indices.size());
        for (unsigned int faceIndex = 0;
            faceIndex < mesh.mNumFaces;
            ++faceIndex) {
            const aiFace& face = mesh.mFaces[faceIndex];
            if (face.mNumIndices != 3) {
                continue;
            }
            indices.push_back(baseVertex + face.mIndices[0]);
            indices.push_back(baseVertex + face.mIndices[1]);
            indices.push_back(baseVertex + face.mIndices[2]);
        }
        const uint32_t indexCount =
            static_cast<uint32_t>(indices.size()) - indexStart;
        if (indexCount == 0) {
            vertices.resize(baseVertex);
            continue;
        }
        ImportedMeshRange& range = meshRanges[meshIndex];
        range.indexStart = indexStart;
        range.indexCount = indexCount;
        range.materialIndex = mesh.mMaterialIndex < materials_.size()
            ? mesh.mMaterialIndex : 0u;
    }

    // Mesh Geometryは1回だけ保持し、Nodeから参照されるたびにTransform付きの
    // SubMesh Drawを追加する。同じMeshを複数Nodeが使うglTFも重複頂点なしで扱える。
    std::function<uint32_t(
        const aiNode*, int32_t, const Matrix4x4&)> importNode;
    std::unordered_map<std::string, uint32_t> nodeLookup;
    importNode = [this, &importNode, &meshRanges, &nodeLookup](
        const aiNode* source,
        int32_t parentIndex,
        const Matrix4x4& parentTransform) -> uint32_t {
        ModelNode node{};
        node.name = source->mName.length > 0
            ? source->mName.C_Str()
            : "Node_" + std::to_string(nodes_.size());
        node.parentIndex = parentIndex;
        node.localTransform = ConvertAssimpMatrix(source->mTransformation);
        node.modelTransform = Multiply(
            node.localTransform, parentTransform);
        aiVector3D bindScale{};
        aiVector3D bindTranslation{};
        aiQuaternion bindRotation{};
        source->mTransformation.Decompose(
            bindScale, bindRotation, bindTranslation);
        node.bindScale = { bindScale.x, bindScale.y, bindScale.z };
        node.bindTranslation = {
            bindTranslation.x, bindTranslation.y, bindTranslation.z
        };
        node.bindRotation = {
            bindRotation.x, bindRotation.y, bindRotation.z, bindRotation.w
        };

        const uint32_t nodeIndex = static_cast<uint32_t>(nodes_.size());
        nodes_.push_back(std::move(node));
        nodeLookup.emplace(nodes_[nodeIndex].name, nodeIndex);
        for (unsigned int meshSlot = 0;
            meshSlot < source->mNumMeshes;
            ++meshSlot) {
            const unsigned int meshIndex = source->mMeshes[meshSlot];
            if (meshIndex >= meshRanges.size()) {
                continue;
            }
            const ImportedMeshRange& range = meshRanges[meshIndex];
            if (range.indexCount == 0) {
                continue;
            }
            const uint32_t subMeshIndex =
                static_cast<uint32_t>(subMeshes_.size());
            subMeshes_.push_back({
                range.indexStart,
                range.indexCount,
                range.materialIndex,
                nodeIndex,
                nodes_[nodeIndex].modelTransform,
                range.boneOffsets
            });
            nodes_[nodeIndex].subMeshIndices.push_back(subMeshIndex);
        }
        for (unsigned int childSlot = 0;
            childSlot < source->mNumChildren;
            ++childSlot) {
            const uint32_t childIndex = importNode(
                source->mChildren[childSlot],
                static_cast<int32_t>(nodeIndex),
                nodes_[nodeIndex].modelTransform);
            nodes_[nodeIndex].childIndices.push_back(childIndex);
        }
        return nodeIndex;
    };
    importNode(scene->mRootNode, -1, MakeIdentity4x4());

    for (ModelBone& bone : bones_) {
        const auto foundNode = nodeLookup.find(bone.name);
        if (foundNode == nodeLookup.end()) {
            lastError_ = "Bone node was not found: " + bone.name;
            return false;
        }
        bone.nodeIndex = foundNode->second;
    }

    animations_.reserve(scene->mNumAnimations);
    for (unsigned int animationIndex = 0;
        animationIndex < scene->mNumAnimations;
        ++animationIndex) {
        const aiAnimation& sourceAnimation =
            *scene->mAnimations[animationIndex];
        const double ticksPerSecond = sourceAnimation.mTicksPerSecond > 0.0
            ? sourceAnimation.mTicksPerSecond
            : 25.0;
        ModelAnimationClip clip{};
        clip.name = sourceAnimation.mName.length > 0
            ? sourceAnimation.mName.C_Str()
            : "Animation_" + std::to_string(animationIndex);
        clip.durationSeconds = static_cast<float>(
            sourceAnimation.mDuration / ticksPerSecond);
        clip.channels.reserve(sourceAnimation.mNumChannels);
        for (unsigned int channelIndex = 0;
            channelIndex < sourceAnimation.mNumChannels;
            ++channelIndex) {
            const aiNodeAnim& sourceChannel =
                *sourceAnimation.mChannels[channelIndex];
            const auto foundNode = nodeLookup.find(
                sourceChannel.mNodeName.C_Str());
            if (foundNode == nodeLookup.end()) {
                continue;
            }
            ModelAnimationChannel channel{};
            channel.nodeIndex = foundNode->second;
            channel.positions.reserve(sourceChannel.mNumPositionKeys);
            for (unsigned int keyIndex = 0;
                keyIndex < sourceChannel.mNumPositionKeys;
                ++keyIndex) {
                const aiVectorKey& key = sourceChannel.mPositionKeys[keyIndex];
                channel.positions.push_back({
                    static_cast<float>(key.mTime / ticksPerSecond),
                    { key.mValue.x, key.mValue.y, key.mValue.z }
                });
            }
            channel.rotations.reserve(sourceChannel.mNumRotationKeys);
            for (unsigned int keyIndex = 0;
                keyIndex < sourceChannel.mNumRotationKeys;
                ++keyIndex) {
                const aiQuatKey& key = sourceChannel.mRotationKeys[keyIndex];
                channel.rotations.push_back({
                    static_cast<float>(key.mTime / ticksPerSecond),
                    { key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w }
                });
            }
            channel.scales.reserve(sourceChannel.mNumScalingKeys);
            for (unsigned int keyIndex = 0;
                keyIndex < sourceChannel.mNumScalingKeys;
                ++keyIndex) {
                const aiVectorKey& key = sourceChannel.mScalingKeys[keyIndex];
                channel.scales.push_back({
                    static_cast<float>(key.mTime / ticksPerSecond),
                    { key.mValue.x, key.mValue.y, key.mValue.z }
                });
            }
            clip.channels.push_back(std::move(channel));
        }
        animations_.push_back(std::move(clip));
    }

    if (vertices.empty() || indices.empty() || subMeshes_.empty()) {
        lastError_ =
            "Assimp model contains no drawable triangles: " + filePath;
        return false;
    }

    bool needsGeneratedTangents = false;
    for (const TextureVertexData& vertex : vertices) {
        if (std::fabs(vertex.tangent.x) + std::fabs(vertex.tangent.y) +
            std::fabs(vertex.tangent.z) <= 0.000001f) {
            needsGeneratedTangents = true;
            break;
        }
    }
    if (needsGeneratedTangents) {
        GenerateTangents(vertices, indices);
    }

    for (TextureVertexData& vertex : vertices) {
        float* weights = &vertex.boneWeights.x;
        const float totalWeight =
            weights[0] + weights[1] + weights[2] + weights[3];
        if (totalWeight <= 0.000001f) {
            continue;
        }
        const float inverseWeight = 1.0f / totalWeight;
        for (uint32_t influence = 0; influence < 4; ++influence) {
            weights[influence] *= inverseWeight;
        }
    }

    for (const ModelMaterial& material : materials_) {
        if (!material.texturePath.empty()) {
            materialTexturePath = material.texturePath;
            break;
        }
    }
    lastError_.clear();
    return true;
#endif
}

bool Model::LoadObjFile(
    const std::string& filePath,
    std::vector<TextureVertexData>& vertices,
    std::vector<uint32_t>& indices,
    std::string& materialTexturePath) {
    return LoadModelFile(
        filePath, vertices, indices, materialTexturePath);
}

bool Model::EvaluateAnimation(
    uint32_t animationIndex,
    float timeSeconds,
    ModelPose& pose) const {
    if (animationIndex >= animations_.size() || nodes_.empty()) {
        pose.nodeModelTransforms.clear();
        return false;
    }

    const ModelAnimationClip& clip = animations_[animationIndex];
    const float sampleTime = (std::clamp)(
        timeSeconds, 0.0f, (std::max)(clip.durationSeconds, 0.0f));
    std::vector<Matrix4x4> localTransforms(nodes_.size());
    for (size_t nodeIndex = 0; nodeIndex < nodes_.size(); ++nodeIndex) {
        localTransforms[nodeIndex] = nodes_[nodeIndex].localTransform;
    }
    for (const ModelAnimationChannel& channel : clip.channels) {
        if (channel.nodeIndex >= nodes_.size()) {
            continue;
        }
        const ModelNode& node = nodes_[channel.nodeIndex];
        const Vector3 translation = SampleVectorKeys(
            channel.positions, sampleTime, node.bindTranslation);
        const Vector3 scale = SampleVectorKeys(
            channel.scales, sampleTime, node.bindScale);
        const ModelNode::Quaternion rotation = SampleQuaternionKeys(
            channel.rotations, sampleTime, node.bindRotation);
        localTransforms[channel.nodeIndex] = Multiply(
            Multiply(MakeScaleMatrix(scale), MakeQuaternionMatrix(rotation)),
            MakeTranslateMatrix(translation));
    }

    pose.nodeModelTransforms.resize(nodes_.size());
    for (size_t nodeIndex = 0; nodeIndex < nodes_.size(); ++nodeIndex) {
        const int32_t parentIndex = nodes_[nodeIndex].parentIndex;
        pose.nodeModelTransforms[nodeIndex] = parentIndex >= 0
            ? Multiply(
                localTransforms[nodeIndex],
                pose.nodeModelTransforms[static_cast<size_t>(parentIndex)])
            : localTransforms[nodeIndex];
    }
    return true;
}

void Model::CreateMeshResources(
    const std::vector<TextureVertexData>& vertices,
    const std::vector<uint32_t>& indices) {
    assert(!vertices.empty());
    assert(!indices.empty());

    // Scene Viewのピッキングで使うモデル空間AABB。
    // glTFの複数Nodeは、各SubMeshの累積行列を適用した後の範囲へまとめる。
    const float maximum = (std::numeric_limits<float>::max)();
    Vector3 minimumBounds = { maximum, maximum, maximum };
    Vector3 maximumBounds = { -maximum, -maximum, -maximum };
    hasBounds_ = false;
    for (const ModelSubMesh& subMesh : subMeshes_) {
        for (uint32_t indexOffset = 0;
            indexOffset < subMesh.indexCount;
            ++indexOffset) {
            const size_t indexPosition =
                static_cast<size_t>(subMesh.indexStart) + indexOffset;
            if (indexPosition >= indices.size()) {
                continue;
            }
            const uint32_t vertexIndex = indices[indexPosition];
            if (vertexIndex >= vertices.size()) {
                continue;
            }
            const Vector4& source = vertices[vertexIndex].position;
            const Vector3 position = Transform(
                { source.x, source.y, source.z },
                subMesh.nodeTransform);
            minimumBounds.x = (std::min)(minimumBounds.x, position.x);
            minimumBounds.y = (std::min)(minimumBounds.y, position.y);
            minimumBounds.z = (std::min)(minimumBounds.z, position.z);
            maximumBounds.x = (std::max)(maximumBounds.x, position.x);
            maximumBounds.y = (std::max)(maximumBounds.y, position.y);
            maximumBounds.z = (std::max)(maximumBounds.z, position.z);
            hasBounds_ = true;
        }
    }
    if (hasBounds_) {
        boundsMin_ = minimumBounds;
        boundsMax_ = maximumBounds;
    }

    vertexCount_ = static_cast<uint32_t>(vertices.size());
    const size_t vertexBufferSize = sizeof(TextureVertexData) * vertices.size();
    vertexResource_ = dxCommon_->CreateStaticBufferResource(
        vertices.data(),
        vertexBufferSize,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = static_cast<UINT>(vertexBufferSize);
    vertexBufferView_.StrideInBytes = sizeof(TextureVertexData);

    indexCount_ = static_cast<uint32_t>(indices.size());
    const size_t indexBufferSize = sizeof(uint32_t) * indices.size();
    indexResource_ = dxCommon_->CreateStaticBufferResource(
        indices.data(),
        indexBufferSize,
        D3D12_RESOURCE_STATE_INDEX_BUFFER);
    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = static_cast<UINT>(indexBufferSize);
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
}

void Model::Draw(
    const Matrix4x4& worldMatrix,
    const Vector4& color,
    int textureHandle,
    const UVTransform& uvTransform,
    bool enableLighting,
    const std::vector<int>& materialTextureHandles,
    const std::vector<int>& materialNormalTextureHandles,
    const std::vector<float>& materialMetallicValues,
    const std::vector<float>& materialRoughnessValues,
    const std::vector<uint8_t>& materialPbrOverrideEnabled,
    const std::vector<UVTransform>& materialUVTransforms,
    const std::vector<uint8_t>& materialUVTransformEnabled,
    const std::vector<std::shared_ptr<Material>>& shaderMaterials,
    BlendMode blendMode,
    const ModelPose* pose) {
    assert(dxCommon_ != nullptr);
    assert(textureManager_ != nullptr);
    assert(rootSignature_ != nullptr);
    assert(pipelineStates_[GetBlendModeIndex(blendMode)] != nullptr);
    assert(textureHandle >= 0);
    assert(indexCount_ > 0);
    assert(!materials_.empty());
    assert(!subMeshes_.empty());

    // 同じModelを1フレーム内で何回Drawしても上書きされないように、
    // 色とUV行列をDraw専用の領域へ書き込む。
    const DynamicBufferAllocation materialAllocation =
        dxCommon_->AllocateDynamicBuffer(sizeof(MaterialConstants));
    auto* materialData = static_cast<MaterialConstants*>(materialAllocation.cpuAddress);
    *materialData = {};
    materialData->color = { color.x, color.y, color.z, color.w };
    // 天球は空の画像そのものの色を表示したいので、Drawの引数でライティングを切れるようにする。
    // 通常のOBJモデルは既定値trueのため、今までどおりライトの影響を受ける。
    materialData->enableLighting = enableLighting ? 1 : 0;
    materialData->uvTransform = MakeUVTransformMatrix(uvTransform);
    materialData->specularColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData->specularShininess = 0.0f;
    materialData->metallic = 0.0f;
    materialData->roughness = 0.5f;

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap* descriptorHeaps[] = { textureManager_->GetSrvHeap() };
    commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    ID3D12PipelineState* selectedPipelineState =
        pipelineStates_[GetBlendModeIndex(blendMode)].Get();
    commandList->SetPipelineState(selectedPipelineState);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetIndexBuffer(&indexBufferView_);

    // Graphicsの共通ルートシグネチャに対応する順番で各リソースを設定する。
    commandList->SetGraphicsRootConstantBufferView(
        3, lightingManager_->GetLightingGpuAddress());
    commandList->SetGraphicsRootDescriptorTable(
        4, textureManager_->GetDirectionalShadowSrvHandleGPU());
    const int environmentTextureHandle =
        lightingManager_->GetEnvironmentTextureHandle() >= 0
        ? lightingManager_->GetEnvironmentTextureHandle()
        : textureHandle;
    commandList->SetGraphicsRootDescriptorTable(
        6, textureManager_->GetSrvHandleGPU(environmentTextureHandle));
    commandList->SetGraphicsRootDescriptorTable(
        7, textureManager_->GetPointShadowSrvHandleGPU());

    // usemtlで分割したSubMeshごとに、MTL定数とmap_Kdを切り替えて描画する。
    for (size_t subMeshIndex = 0;
        subMeshIndex < subMeshes_.size();
        ++subMeshIndex) {
        const ModelSubMesh& subMesh = subMeshes_[subMeshIndex];
        assert(subMesh.materialIndex < materials_.size());
        const ModelMaterial& sourceMaterial = materials_[subMesh.materialIndex];

        // glTF Nodeの累積行列をGameObjectのWorld行列より先に適用する。
        const bool hasPose = pose != nullptr &&
            pose->nodeModelTransforms.size() == nodes_.size();
        const Matrix4x4& nodeTransform = hasPose
            ? pose->nodeModelTransforms[subMesh.nodeIndex]
            : subMesh.nodeTransform;
        const Matrix4x4 subMeshWorld = Multiply(nodeTransform, worldMatrix);
        const DynamicBufferAllocation transformationAllocation =
            dxCommon_->AllocateDynamicBuffer(sizeof(TransformationMatrix));
        auto* transformationData = static_cast<TransformationMatrix*>(
            transformationAllocation.cpuAddress);
        transformationData->World = subMeshWorld;
        transformationData->WVP = Multiply(
            subMeshWorld, debugCamera_->GetViewProjectionMatrix());
        transformationData->WorldInverseTranspose =
            Transpose(Inverse(subMeshWorld));

        // 1つ目は先に確保した領域を再利用し、2つ目以降だけ追加確保する。
        const DynamicBufferAllocation subMeshMaterialAllocation =
            subMeshIndex == 0
            ? materialAllocation
            : dxCommon_->AllocateDynamicBuffer(sizeof(MaterialConstants));
        auto* subMeshMaterial = static_cast<MaterialConstants*>(
            subMeshMaterialAllocation.cpuAddress);
        *subMeshMaterial = {};

        // Drawのcolorは全体の色調整、KdはMTL固有色として乗算する。
        subMeshMaterial->color = {
            color.x * sourceMaterial.diffuseColor.r,
            color.y * sourceMaterial.diffuseColor.g,
            color.z * sourceMaterial.diffuseColor.b,
            color.w * sourceMaterial.diffuseColor.a * sourceMaterial.opacity
        };
        subMeshMaterial->enableLighting = enableLighting ? 1 : 0;

        // Material側でUVを個別設定している場合だけ、その値を使用する。
        // 個別設定がOFFならModel Renderer全体のUVへ戻るので、
        // 単一MaterialのOBJは今までどおり1か所の調整だけで扱える。
        const bool usesMaterialUVTransform =
            subMesh.materialIndex < materialUVTransformEnabled.size() &&
            materialUVTransformEnabled[subMesh.materialIndex] != 0 &&
            subMesh.materialIndex < materialUVTransforms.size();
        const UVTransform& subMeshUVTransform = usesMaterialUVTransform
            ? materialUVTransforms[subMesh.materialIndex]
            : uvTransform;
        subMeshMaterial->uvTransform =
            MakeUVTransformMatrix(subMeshUVTransform);
        subMeshMaterial->specularColor = sourceMaterial.specularColor;
        subMeshMaterial->specularShininess = sourceMaterial.shininess;
        const bool usesPbrOverride =
            subMesh.materialIndex < materialPbrOverrideEnabled.size() &&
            materialPbrOverrideEnabled[subMesh.materialIndex] != 0 &&
            subMesh.materialIndex < materialMetallicValues.size() &&
            subMesh.materialIndex < materialRoughnessValues.size();
        subMeshMaterial->metallic = usesPbrOverride
            ? (std::clamp)(
                materialMetallicValues[subMesh.materialIndex], 0.0f, 1.0f)
            : sourceMaterial.metallic;
        subMeshMaterial->roughness = usesPbrOverride
            ? (std::clamp)(
                materialRoughnessValues[subMesh.materialIndex], 0.04f, 1.0f)
            : sourceMaterial.roughness;

        const int overrideNormalTextureHandle =
            subMesh.materialIndex < materialNormalTextureHandles.size()
            ? materialNormalTextureHandles[subMesh.materialIndex]
            : -1;
        const int normalTextureHandle = overrideNormalTextureHandle >= 0
            ? overrideNormalTextureHandle
            : sourceMaterial.normalTextureHandle;
        subMeshMaterial->normalMapEnabled = normalTextureHandle >= 0 ? 1 : 0;
        const int metallicRoughnessTextureHandle =
            sourceMaterial.metallicRoughnessTextureHandle;
        subMeshMaterial->metallicRoughnessMapEnabled =
            metallicRoughnessTextureHandle >= 0 ? 1 : 0;

        // Textureは「Inspectorの個別指定 → MTLのmap_Kd → Fallback」の順で選ぶ。
        // Inspectorの指定を解除すると自動的に元のMTL Textureへ戻る。
        const int materialTextureHandle =
            subMesh.materialIndex < materialTextureHandles.size()
            ? materialTextureHandles[subMesh.materialIndex]
            : -1;
        const int subMeshTextureHandle = materialTextureHandle >= 0
            ? materialTextureHandle
            : (sourceMaterial.textureHandle >= 0
                ? sourceMaterial.textureHandle
                : textureHandle);
        assert(subMeshTextureHandle >= 0);

        const DynamicBufferAllocation customParameterAllocation =
            dxCommon_->AllocateDynamicBuffer(
                ShaderManager::kMaterialParameterBufferSize,
                D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        std::memset(
            customParameterAllocation.cpuAddress,
            0,
            ShaderManager::kMaterialParameterBufferSize);
        Material* shaderMaterial =
            subMesh.materialIndex < shaderMaterials.size()
            ? shaderMaterials[subMesh.materialIndex].get()
            : nullptr;
        if (shaderMaterial != nullptr) {
            shaderMaterial->WriteParameterBuffer(
                customParameterAllocation.cpuAddress,
                ShaderManager::kMaterialParameterBufferSize);
            if (ID3D12PipelineState* materialPipeline =
                shaderMaterial->GetPipelineState()) {
                commandList->SetPipelineState(materialPipeline);
            } else {
                commandList->SetPipelineState(selectedPipelineState);
            }
        } else {
            commandList->SetPipelineState(selectedPipelineState);
        }

        commandList->SetGraphicsRootConstantBufferView(
            0, subMeshMaterialAllocation.gpuAddress);
        commandList->SetGraphicsRootConstantBufferView(
            1, transformationAllocation.gpuAddress);
        commandList->SetGraphicsRootConstantBufferView(
            8, customParameterAllocation.gpuAddress);
        commandList->SetGraphicsRootDescriptorTable(
            2, textureManager_->GetSrvHandleGPU(subMeshTextureHandle));
        // Normal Mapがない場合もRoot Parameterには有効なSRVを設定し、
        // normalMapEnabledでShader側のSampleだけを止める。
        commandList->SetGraphicsRootDescriptorTable(
            5,
            textureManager_->GetSrvHandleGPU(
                normalTextureHandle >= 0
                ? normalTextureHandle
                : subMeshTextureHandle));
        commandList->SetGraphicsRootDescriptorTable(
            9,
            textureManager_->GetSrvHandleGPU(
                metallicRoughnessTextureHandle >= 0
                ? metallicRoughnessTextureHandle
                : subMeshTextureHandle));
        const DynamicBufferAllocation skinningAllocation =
            dxCommon_->AllocateDynamicBuffer(sizeof(SkinningConstants));
        auto* skinning = static_cast<SkinningConstants*>(
            skinningAllocation.cpuAddress);
        *skinning = {};
        skinning->boneCount = static_cast<uint32_t>(bones_.size());
        if (hasPose && !subMesh.boneOffsets.empty()) {
            skinning->enabled = 1;
            const Matrix4x4 inverseMeshTransform = Inverse(nodeTransform);
            for (const ModelSubMesh::BoneOffset& boneOffset :
                subMesh.boneOffsets) {
                if (boneOffset.boneIndex >= bones_.size()) {
                    continue;
                }
                const ModelBone& bone = bones_[boneOffset.boneIndex];
                skinning->boneMatrices[boneOffset.boneIndex] = Multiply(
                    Multiply(
                        boneOffset.offsetMatrix,
                        pose->nodeModelTransforms[bone.nodeIndex]),
                    inverseMeshTransform);
            }
        }
        commandList->SetGraphicsRootConstantBufferView(
            10, skinningAllocation.gpuAddress);
        commandList->DrawIndexedInstanced(
            subMesh.indexCount,
            1,
            subMesh.indexStart,
            0,
            0);
    }
}

void Model::DrawOutline(
    const Matrix4x4& worldMatrix,
    const ModelPose* pose) {
    assert(dxCommon_ != nullptr);
    assert(rootSignature_ != nullptr);
    assert(outlinePipelineState_ != nullptr);
    if (isSkySphere_ || indexCount_ == 0 || subMeshes_.empty()) {
        return;
    }

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(outlinePipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetIndexBuffer(&indexBufferView_);

    for (const ModelSubMesh& subMesh : subMeshes_) {
        const bool hasPose = pose != nullptr &&
            pose->nodeModelTransforms.size() == nodes_.size();
        const Matrix4x4& nodeTransform = hasPose
            ? pose->nodeModelTransforms[subMesh.nodeIndex]
            : subMesh.nodeTransform;
        const Matrix4x4 subMeshWorld = Multiply(nodeTransform, worldMatrix);

        const DynamicBufferAllocation transformationAllocation =
            dxCommon_->AllocateDynamicBuffer(sizeof(TransformationMatrix));
        auto* transformation = static_cast<TransformationMatrix*>(
            transformationAllocation.cpuAddress);
        transformation->World = subMeshWorld;
        transformation->WVP = Multiply(
            subMeshWorld, debugCamera_->GetViewProjectionMatrix());
        transformation->WorldInverseTranspose =
            Transpose(Inverse(subMeshWorld));
        commandList->SetGraphicsRootConstantBufferView(
            1, transformationAllocation.gpuAddress);

        const DynamicBufferAllocation skinningAllocation =
            dxCommon_->AllocateDynamicBuffer(sizeof(SkinningConstants));
        auto* skinning = static_cast<SkinningConstants*>(
            skinningAllocation.cpuAddress);
        *skinning = {};
        skinning->boneCount = static_cast<uint32_t>(bones_.size());
        if (hasPose && !subMesh.boneOffsets.empty()) {
            skinning->enabled = 1;
            const Matrix4x4 inverseMeshTransform = Inverse(nodeTransform);
            for (const ModelSubMesh::BoneOffset& boneOffset :
                subMesh.boneOffsets) {
                if (boneOffset.boneIndex >= bones_.size()) {
                    continue;
                }
                const ModelBone& bone = bones_[boneOffset.boneIndex];
                skinning->boneMatrices[boneOffset.boneIndex] = Multiply(
                    Multiply(
                        boneOffset.offsetMatrix,
                        pose->nodeModelTransforms[bone.nodeIndex]),
                    inverseMeshTransform);
            }
        }
        commandList->SetGraphicsRootConstantBufferView(
            10, skinningAllocation.gpuAddress);
        commandList->DrawIndexedInstanced(
            subMesh.indexCount,
            1,
            subMesh.indexStart,
            0,
            0);
    }
}

void Model::DrawShadow(
    const Matrix4x4& worldMatrix,
    const Matrix4x4& lightViewProjection,
    const ModelPose* pose) {
    assert(dxCommon_ != nullptr);
    assert(shadowRootSignature_ != nullptr);
    assert(shadowPipelineState_ != nullptr);
    assert(indexCount_ > 0);
    if (isSkySphere_) {
        return;
    }

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(shadowRootSignature_.Get());
    commandList->SetPipelineState(shadowPipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetIndexBuffer(&indexBufferView_);
    for (const ModelSubMesh& subMesh : subMeshes_) {
        const bool hasPose = pose != nullptr &&
            pose->nodeModelTransforms.size() == nodes_.size();
        const Matrix4x4& nodeTransform = hasPose
            ? pose->nodeModelTransforms[subMesh.nodeIndex]
            : subMesh.nodeTransform;
        const Matrix4x4 subMeshWorld = Multiply(nodeTransform, worldMatrix);
        const DynamicBufferAllocation shadowTransformAllocation =
            dxCommon_->AllocateDynamicBuffer(sizeof(Matrix4x4));
        *static_cast<Matrix4x4*>(shadowTransformAllocation.cpuAddress) =
            Multiply(subMeshWorld, lightViewProjection);
        commandList->SetGraphicsRootConstantBufferView(
            0, shadowTransformAllocation.gpuAddress);
        const DynamicBufferAllocation skinningAllocation =
            dxCommon_->AllocateDynamicBuffer(sizeof(SkinningConstants));
        auto* skinning = static_cast<SkinningConstants*>(
            skinningAllocation.cpuAddress);
        *skinning = {};
        skinning->boneCount = static_cast<uint32_t>(bones_.size());
        if (hasPose && !subMesh.boneOffsets.empty()) {
            skinning->enabled = 1;
            const Matrix4x4 inverseMeshTransform = Inverse(nodeTransform);
            for (const ModelSubMesh::BoneOffset& boneOffset :
                subMesh.boneOffsets) {
                if (boneOffset.boneIndex >= bones_.size()) {
                    continue;
                }
                const ModelBone& bone = bones_[boneOffset.boneIndex];
                skinning->boneMatrices[boneOffset.boneIndex] = Multiply(
                    Multiply(
                        boneOffset.offsetMatrix,
                        pose->nodeModelTransforms[bone.nodeIndex]),
                    inverseMeshTransform);
            }
        }
        commandList->SetGraphicsRootConstantBufferView(
            1, skinningAllocation.gpuAddress);
        commandList->DrawIndexedInstanced(
            subMesh.indexCount,
            1,
            subMesh.indexStart,
            0,
            0);
    }
}
