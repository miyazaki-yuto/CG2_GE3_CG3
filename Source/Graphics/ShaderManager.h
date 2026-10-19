#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

enum class ShaderParameterType {
    Float,
    Float2,
    Float3,
    Float4,
    Int,
    Bool
};

struct ShaderParameterValue {
    ShaderParameterType type = ShaderParameterType::Float;
    std::array<float, 4> floats{};
    int32_t integer = 0;
};

struct ShaderParameterDefinition {
    std::string name;
    ShaderParameterType type = ShaderParameterType::Float;
    uint32_t offset = 0;
    ShaderParameterValue defaultValue{};
};

// DXC、JSONシェーダー定義、PSO、ホットリロードを一元管理する。
class ShaderManager {
public:
    static constexpr uint32_t kMaterialParameterBufferSize = 256;

    ShaderManager() = default;
    ~ShaderManager() = default;

    ShaderManager(const ShaderManager&) = delete;
    ShaderManager& operator=(const ShaderManager&) = delete;

    void Initialize(std::ostream& logStream);

    Microsoft::WRL::ComPtr<IDxcBlob> LoadShader(
        const std::wstring& filePath,
        const std::wstring& profile,
        std::ostream& logStream,
        const std::wstring& entryPoint = L"main");
    Microsoft::WRL::ComPtr<IDxcBlob> ReloadShader(
        const std::wstring& filePath,
        const std::wstring& profile,
        std::ostream& logStream,
        const std::wstring& entryPoint = L"main");

    // Graphicsが作成した標準Object3D PSOをテンプレートとして保存する。
    // JSONから追加するシェーダーは頂点形式とRoot Signatureを共有する。
    void SetMaterialPipelineTemplate(
        ID3D12Device* device,
        ID3D12RootSignature* rootSignature,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pipelineTemplate);

    bool LoadShaderDefinition(
        const std::filesystem::path& jsonPath,
        std::ostream& logStream,
        bool forceReload = false);
    size_t LoadShaderDefinitions(
        const std::filesystem::path& directory,
        std::ostream& logStream);

    // 500ms間隔でJSON、HLSL、直接・間接includeファイルを確認する。
    void UpdateHotReload(std::ostream& logStream);

    ID3D12PipelineState* GetMaterialPipelineState(
        const std::string& shaderName) const;
    std::vector<ShaderParameterDefinition> GetParameterDefinitions(
        const std::string& shaderName) const;
    std::vector<std::string> GetShaderNames() const;
    uint64_t GetShaderGeneration(const std::string& shaderName) const;
    const std::string& GetLastError() const { return lastError_; }

    void ClearCache();
    size_t GetCachedShaderCount() const;
    bool IsInitialized() const;

private:
    struct ShaderKey {
        std::wstring filePath;
        std::wstring profile;
        std::wstring entryPoint;

        bool operator==(const ShaderKey& other) const {
            return filePath == other.filePath &&
                profile == other.profile &&
                entryPoint == other.entryPoint;
        }
    };

    struct ShaderKeyHash {
        size_t operator()(const ShaderKey& key) const;
    };

    struct DependencyStamp {
        std::filesystem::path path;
        std::filesystem::file_time_type writeTime{};
    };

    struct MaterialShaderRecord {
        std::string name;
        std::filesystem::path jsonPath;
        std::filesystem::path vertexShaderPath;
        std::filesystem::path pixelShaderPath;
        std::wstring vertexProfile = L"vs_6_0";
        std::wstring pixelProfile = L"ps_6_0";
        std::wstring vertexEntry = L"main";
        std::wstring pixelEntry = L"main";
        std::vector<ShaderParameterDefinition> parameters;
        std::vector<DependencyStamp> dependencies;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
        uint64_t generation = 1;
    };

    Microsoft::WRL::ComPtr<IDxcBlob> LoadShaderInternal(
        const std::wstring& filePath,
        const std::wstring& profile,
        std::ostream& logStream,
        const std::wstring& entryPoint,
        bool forceReload);
    Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(
        const ShaderKey& key,
        std::ostream& logStream);

    mutable std::mutex mutex_;
    Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils_;
    Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler_;
    Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler_;
    std::unordered_map<
        ShaderKey,
        Microsoft::WRL::ComPtr<IDxcBlob>,
        ShaderKeyHash> shaderCache_;

    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> materialRootSignature_;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC materialPipelineTemplate_{};
    std::vector<D3D12_INPUT_ELEMENT_DESC> materialInputElements_;
    std::unordered_map<std::string, MaterialShaderRecord> materialShaders_;
    std::vector<std::filesystem::path> definitionDirectories_;
    // GPUが旧PSOを参照中でも解放されないよう、置換後も寿命を保持する。
    std::vector<Microsoft::WRL::ComPtr<ID3D12PipelineState>>
        retiredPipelineStates_;
    std::chrono::steady_clock::time_point lastHotReloadCheck_{};
    std::string lastError_;
};
