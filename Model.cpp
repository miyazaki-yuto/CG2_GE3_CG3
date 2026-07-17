#include "Model.h"

#include "DX12Utility.h"
#include "DebugCamera.h"
#include "DirectXCommon.h"
#include "Matrix4x4.h"
#include "TextureManager.h"

#include <Windows.h>

#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace {

// OBJでUVまたは法線が省略されていることを表す、実在しないインデックス。
constexpr size_t kMissingObjIndex = static_cast<size_t>(-1);

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

// "位置/UV/法線"、"位置//法線"、"位置/UV"、"位置"を分解する。
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
std::string LoadMaterialTexturePath(
    const std::filesystem::path& objDirectory,
    const std::string& materialFileName) {
    const std::filesystem::path materialPath =
        (objDirectory / MakePathFromUtf8(materialFileName)).lexically_normal();
    std::ifstream materialFile{ materialPath };
    if (!materialFile.is_open()) {
        return {};
    }

    std::string line;
    while (std::getline(materialFile, line)) {
        std::istringstream lineStream(line);
        std::string identifier;
        lineStream >> identifier;
        if (identifier != "map_Kd") {
            continue;
        }

        std::string textureFileName;
        std::getline(lineStream >> std::ws, textureFileName);
        if (textureFileName.empty()) {
            return {};
        }

        const std::filesystem::path texturePath =
            (materialPath.parent_path() /
                MakePathFromUtf8(textureFileName)).lexically_normal();
        return MakeUtf8FromPath(texturePath);
    }

    return {};
}

} // namespace

bool Model::Initialize(
    DirectXCommon* dxCommon,
    DebugCamera* debugCamera,
    TextureManager* textureManager,
    ID3D12RootSignature* rootSignature,
    ID3D12PipelineState* pipelineState,
    uint32_t windowWidth,
    uint32_t windowHeight,
    const std::string& objFilePath) {
    assert(dxCommon != nullptr);
    assert(debugCamera != nullptr);
    assert(textureManager != nullptr);
    assert(rootSignature != nullptr);
    assert(pipelineState != nullptr);
    assert(windowWidth > 0 && windowHeight > 0);

    dxCommon_ = dxCommon;
    debugCamera_ = debugCamera;
    textureManager_ = textureManager;
    rootSignature_ = rootSignature;
    pipelineState_ = pipelineState;
    windowWidth_ = windowWidth;
    windowHeight_ = windowHeight;
    sourcePath_ = objFilePath;

    std::vector<TextureVertexData> vertices;
    std::vector<uint32_t> indices;
    if (!LoadObjFile(objFilePath, vertices, indices, materialTexturePath_)) {
        return false;
    }

    CreateMeshResources(vertices, indices);
    CreateConstantBufferResources();

    viewport_.Width = static_cast<float>(windowWidth_);
    viewport_.Height = static_cast<float>(windowHeight_);
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;
    scissorRect_.right = static_cast<LONG>(windowWidth_);
    scissorRect_.bottom = static_cast<LONG>(windowHeight_);
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
    std::string materialFileName;
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
            std::getline(lineStream >> std::ws, materialFileName);
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
            }
        }
    }

    if (vertices.empty() || indices.empty()) {
        lastError_ = "OBJ contains no drawable faces: " + filePath;
        return false;
    }

    if (!materialFileName.empty()) {
        materialTexturePath = LoadMaterialTexturePath(
            objPath.parent_path(), materialFileName);
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
    vertexResource_ = DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), vertexBufferSize);
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = static_cast<UINT>(vertexBufferSize);
    vertexBufferView_.StrideInBytes = sizeof(TextureVertexData);

    TextureVertexData* mappedVertices = nullptr;
    HRESULT hr = vertexResource_->Map(
        0, nullptr, reinterpret_cast<void**>(&mappedVertices));
    assert(SUCCEEDED(hr));
    std::memcpy(mappedVertices, vertices.data(), vertexBufferSize);
    vertexResource_->Unmap(0, nullptr);

    indexCount_ = static_cast<uint32_t>(indices.size());
    const size_t indexBufferSize = sizeof(uint32_t) * indices.size();
    indexResource_ = DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), indexBufferSize);
    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = static_cast<UINT>(indexBufferSize);
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

    uint32_t* mappedIndices = nullptr;
    hr = indexResource_->Map(
        0, nullptr, reinterpret_cast<void**>(&mappedIndices));
    assert(SUCCEEDED(hr));
    std::memcpy(mappedIndices, indices.data(), indexBufferSize);
    indexResource_->Unmap(0, nullptr);
}

void Model::CreateConstantBufferResources() {
    const UINT materialBufferSize = (sizeof(Material) + 255u) & ~255u;
    materialResource_ = DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), materialBufferSize);
    HRESULT hr = materialResource_->Map(
        0, nullptr, reinterpret_cast<void**>(&materialData_));
    assert(SUCCEEDED(hr));
    materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData_->enableLighting = 1;
    materialData_->padding[0] = 0.0f;
    materialData_->padding[1] = 0.0f;
    materialData_->padding[2] = 0.0f;
    materialData_->uvTransform = MakeIdentity4x4();

    wvpResource_ = DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), sizeof(TransformationMatrix));
    hr = wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));
    assert(SUCCEEDED(hr));
    wvpData_->WVP = MakeIdentity4x4();
    wvpData_->World = MakeIdentity4x4();

    const UINT lightBufferSize = (sizeof(DirectionalLight) + 255u) & ~255u;
    directionalLightResource_ = DX12Utility::CreateBufferResource(
        dxCommon_->GetDevice(), lightBufferSize);
    hr = directionalLightResource_->Map(
        0, nullptr, reinterpret_cast<void**>(&directionalLightData_));
    assert(SUCCEEDED(hr));
    directionalLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLightData_->direction = { 0.0f, -1.0f, 1.0f };
    directionalLightData_->direction.Normalize();
    directionalLightData_->intensity = 1.0f;
}

void Model::Draw(
    const TransformData& transform,
    const Vector4& color,
    int textureHandle,
    const UVTransform& uvTransform) {
    assert(dxCommon_ != nullptr);
    assert(textureManager_ != nullptr);
    assert(rootSignature_ != nullptr);
    assert(pipelineState_ != nullptr);
    assert(textureHandle >= 0);
    assert(indexCount_ > 0);

    const Matrix4x4 worldMatrix = MakeAffineMatrix(
        transform.scale, transform.rotate, transform.translate);
    wvpData_->World = worldMatrix;
    // PrimitiveDrawerと同じ共有デバッグカメラのViewProjectionを使用する。
    wvpData_->WVP = Multiply(worldMatrix, debugCamera_->GetViewProjectionMatrix());
    materialData_->color = { color.x, color.y, color.z, color.w };
    materialData_->uvTransform = MakeUVTransformMatrix(uvTransform);

    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    ID3D12DescriptorHeap* descriptorHeaps[] = { textureManager_->GetSrvHeap() };
    commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipelineState_.Get());
    commandList->RSSetViewports(1, &viewport_);
    commandList->RSSetScissorRects(1, &scissorRect_);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetIndexBuffer(&indexBufferView_);

    // Graphicsの共通ルートシグネチャに対応する順番で各リソースを設定する。
    commandList->SetGraphicsRootConstantBufferView(
        0, materialResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(
        1, wvpResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(
        2, textureManager_->GetSrvHandleGPU(textureHandle));
    commandList->SetGraphicsRootConstantBufferView(
        3, directionalLightResource_->GetGPUVirtualAddress());
    commandList->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
}
