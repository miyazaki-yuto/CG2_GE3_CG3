#include "Model.h"

#include "DebugCamera.h"
#include "DirectXCommon.h"
#include "LightingManager.h"
#include "Material.h"
#include "Matrix4x4.h"
#include "TextureManager.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>

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

} // namespace

bool Model::Initialize(
    DirectXCommon* dxCommon,
    DebugCamera* debugCamera,
    LightingManager* lightingManager,
    TextureManager* textureManager,
    ID3D12RootSignature* rootSignature,
    ID3D12PipelineState* pipelineState,
    ID3D12RootSignature* shadowRootSignature,
    ID3D12PipelineState* shadowPipelineState,
    const std::string& objFilePath) {
    assert(dxCommon != nullptr);
    assert(debugCamera != nullptr);
    assert(lightingManager != nullptr);
    assert(textureManager != nullptr);
    assert(rootSignature != nullptr);
    assert(pipelineState != nullptr);
    assert(shadowRootSignature != nullptr);
    assert(shadowPipelineState != nullptr);

    dxCommon_ = dxCommon;
    debugCamera_ = debugCamera;
    lightingManager_ = lightingManager;
    textureManager_ = textureManager;
    rootSignature_ = rootSignature;
    pipelineState_ = pipelineState;
    shadowRootSignature_ = shadowRootSignature;
    shadowPipelineState_ = shadowPipelineState;
    sourcePath_ = objFilePath;

    std::vector<TextureVertexData> vertices;
    std::vector<uint32_t> indices;
    if (!LoadObjFile(objFilePath, vertices, indices, materialTexturePath_)) {
        return false;
    }

    // 各newmtlのmap_Kdを読み込み、SubMeshごとに使用できるハンドルへ変換する。
    // TextureManagerにはキャッシュがあるため、同じ画像を複数マテリアルが使っても重複しない。
    for (ModelMaterial& material : materials_) {
        if (!material.texturePath.empty()) {
            material.textureHandle = textureManager_->LoadTexture(
                material.texturePath, dxCommon_->GetCommandList());
        }
        if (!material.normalTexturePath.empty()) {
            // Normal Mapは色ではなく方向データなので、sRGB変換を行わずLinearで読む。
            material.normalTextureHandle = textureManager_->LoadTexture(
                material.normalTexturePath,
                dxCommon_->GetCommandList(),
                false);
        }
    }

    CreateMeshResources(vertices, indices);
    return true;
}

bool Model::LoadObjFile(
    const std::string& filePath,
    std::vector<TextureVertexData>& vertices,
    std::vector<uint32_t>& indices,
    std::string& materialTexturePath) {
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
}

void Model::CreateMeshResources(
    const std::vector<TextureVertexData>& vertices,
    const std::vector<uint32_t>& indices) {
    assert(!vertices.empty());
    assert(!indices.empty());

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
    const std::vector<std::shared_ptr<Material>>& shaderMaterials) {
    assert(dxCommon_ != nullptr);
    assert(textureManager_ != nullptr);
    assert(rootSignature_ != nullptr);
    assert(pipelineState_ != nullptr);
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

    // 座標変換行列もDrawごとに独立させる。
    // 逆転置行列を使うと、X/Y/Zで異なる倍率を掛けても法線の向きが崩れない。
    const DynamicBufferAllocation transformationAllocation =
        dxCommon_->AllocateDynamicBuffer(sizeof(TransformationMatrix));
    auto* transformationData = static_cast<TransformationMatrix*>(
        transformationAllocation.cpuAddress);
    transformationData->World = worldMatrix;
    transformationData->WVP = Multiply(
        worldMatrix, debugCamera_->GetViewProjectionMatrix());
    transformationData->WorldInverseTranspose = Transpose(Inverse(worldMatrix));

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap* descriptorHeaps[] = { textureManager_->GetSrvHeap() };
    commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetIndexBuffer(&indexBufferView_);

    // Graphicsの共通ルートシグネチャに対応する順番で各リソースを設定する。
    commandList->SetGraphicsRootConstantBufferView(
        1, transformationAllocation.gpuAddress);
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
            color.w * sourceMaterial.opacity
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
                commandList->SetPipelineState(pipelineState_.Get());
            }
        } else {
            commandList->SetPipelineState(pipelineState_.Get());
        }

        commandList->SetGraphicsRootConstantBufferView(
            0, subMeshMaterialAllocation.gpuAddress);
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
    const Matrix4x4& lightViewProjection) {
    assert(dxCommon_ != nullptr);
    assert(shadowRootSignature_ != nullptr);
    assert(shadowPipelineState_ != nullptr);
    assert(indexCount_ > 0);
    if (isSkySphere_) {
        return;
    }

    const DynamicBufferAllocation shadowTransformAllocation =
        dxCommon_->AllocateDynamicBuffer(sizeof(Matrix4x4));
    *static_cast<Matrix4x4*>(shadowTransformAllocation.cpuAddress) =
        Multiply(worldMatrix, lightViewProjection);

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(shadowRootSignature_.Get());
    commandList->SetPipelineState(shadowPipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetIndexBuffer(&indexBufferView_);
    commandList->SetGraphicsRootConstantBufferView(
        0, shadowTransformAllocation.gpuAddress);
    commandList->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
}
