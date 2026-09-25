#include "ShaderManager.h"

#include "DX12Utility.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <format>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

#pragma comment(lib, "dxcompiler.lib")

namespace {

std::wstring ResolveShaderPath(const std::wstring& filePath) {
    std::filesystem::path path(filePath);
    if (path.is_relative()) {
        std::error_code error;
        const auto currentPath = std::filesystem::current_path(error);
        if (!error) {
            path = currentPath / path;
        }
    }
    std::error_code error;
    const auto canonicalPath = std::filesystem::weakly_canonical(path, error);
    return (!error ? canonicalPath : path.lexically_normal()).wstring();
}

std::filesystem::path ResolvePath(
    const std::filesystem::path& baseDirectory,
    const std::string& utf8Path) {
    const auto* first = reinterpret_cast<const char8_t*>(utf8Path.data());
    std::filesystem::path path(
        std::u8string(first, first + utf8Path.size()));
    if (path.is_relative()) {
        path = baseDirectory / path;
    }
    std::error_code error;
    const auto canonicalPath = std::filesystem::weakly_canonical(path, error);
    return !error ? canonicalPath : path.lexically_normal();
}

std::string PathToUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return {
        reinterpret_cast<const char*>(text.data()),
        text.size()
    };
}

bool ReadTextFile(
    const std::filesystem::path& path,
    std::string& text) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    text = stream.str();
    return input.good() || input.eof();
}

std::string UnescapeJsonString(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    bool escaped = false;
    for (char character : text) {
        if (escaped) {
            if (character == 'n') {
                result.push_back('\n');
            } else if (character == 't') {
                result.push_back('\t');
            } else {
                result.push_back(character);
            }
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else {
            result.push_back(character);
        }
    }
    return result;
}

bool ReadStringField(
    const std::string& object,
    const std::string& field,
    std::string& result,
    bool required = true) {
    const std::regex pattern(
        "\"" + field + "\"\\s*:\\s*\"((?:\\\\.|[^\"])*)\"",
        std::regex::ECMAScript);
    std::smatch match;
    if (!std::regex_search(object, match, pattern)) {
        return !required;
    }
    result = UnescapeJsonString(match[1].str());
    return true;
}

bool ReadUIntField(
    const std::string& object,
    const std::string& field,
    uint32_t& result,
    bool required = true) {
    const std::regex pattern(
        "\"" + field + "\"\\s*:\\s*([0-9]+)");
    std::smatch match;
    if (!std::regex_search(object, match, pattern)) {
        return !required;
    }
    try {
        result = static_cast<uint32_t>(std::stoul(match[1].str()));
        return true;
    } catch (...) {
        return false;
    }
}

bool ReadBoolField(
    const std::string& object,
    const std::string& field,
    bool& result,
    bool required = false) {
    const std::regex pattern(
        "\"" + field + "\"\\s*:\\s*(true|false)");
    std::smatch match;
    if (!std::regex_search(object, match, pattern)) {
        return !required;
    }
    result = match[1].str() == "true";
    return true;
}

std::vector<float> ReadDefaultNumbers(const std::string& object) {
    const std::regex arrayPattern(
        "\"default\"\\s*:\\s*\\[([^\\]]*)\\]");
    const std::regex scalarPattern(
        "\"default\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?"
        "(?:[eE][+-]?[0-9]+)?)");
    const std::regex numberPattern(
        "-?[0-9]+(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?");
    std::smatch match;
    std::string values;
    if (std::regex_search(object, match, arrayPattern)) {
        values = match[1].str();
    } else if (std::regex_search(object, match, scalarPattern)) {
        values = match[1].str();
    } else {
        return {};
    }

    std::vector<float> result;
    for (std::sregex_iterator it(
            values.begin(), values.end(), numberPattern), end;
        it != end;
        ++it) {
        try {
            result.push_back(std::stof((*it)[0].str()));
        } catch (...) {
            return {};
        }
    }
    return result;
}

std::vector<std::string> ExtractObjectArray(
    const std::string& json,
    const std::string& field) {
    const size_t fieldPosition = json.find("\"" + field + "\"");
    if (fieldPosition == std::string::npos) {
        return {};
    }
    const size_t arrayStart = json.find('[', fieldPosition);
    if (arrayStart == std::string::npos) {
        return {};
    }

    std::vector<std::string> result;
    int depth = 0;
    size_t objectStart = std::string::npos;
    bool inString = false;
    bool escaped = false;
    for (size_t index = arrayStart + 1; index < json.size(); ++index) {
        const char character = json[index];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                inString = false;
            }
            continue;
        }
        if (character == '"') {
            inString = true;
        } else if (character == '{') {
            if (depth++ == 0) {
                objectStart = index;
            }
        } else if (character == '}') {
            if (--depth == 0 && objectStart != std::string::npos) {
                result.push_back(
                    json.substr(objectStart, index - objectStart + 1));
                objectStart = std::string::npos;
            }
        } else if (character == ']' && depth == 0) {
            break;
        }
    }
    return result;
}

bool ParseParameterType(
    const std::string& text,
    ShaderParameterType& type,
    uint32_t& size) {
    if (text == "float") {
        type = ShaderParameterType::Float;
        size = 4;
    } else if (text == "float2") {
        type = ShaderParameterType::Float2;
        size = 8;
    } else if (text == "float3") {
        type = ShaderParameterType::Float3;
        size = 12;
    } else if (text == "float4" || text == "color") {
        type = ShaderParameterType::Float4;
        size = 16;
    } else if (text == "int") {
        type = ShaderParameterType::Int;
        size = 4;
    } else if (text == "bool") {
        type = ShaderParameterType::Bool;
        size = 4;
    } else {
        return false;
    }
    return true;
}

struct ParsedDefinition {
    std::string name;
    std::filesystem::path jsonPath;
    std::filesystem::path vertexShaderPath;
    std::filesystem::path pixelShaderPath;
    std::wstring vertexProfile = L"vs_6_0";
    std::wstring pixelProfile = L"ps_6_0";
    std::wstring vertexEntry = L"main";
    std::wstring pixelEntry = L"main";
    std::string blend = "Alpha";
    std::string cull = "Back";
    bool depthWrite = true;
    bool depthTest = true;
    std::vector<ShaderParameterDefinition> parameters;
};

bool ParseDefinition(
    const std::filesystem::path& jsonPath,
    ParsedDefinition& definition,
    std::string& error) {
    std::string json;
    if (!ReadTextFile(jsonPath, json)) {
        error = "Could not open shader definition: " + PathToUtf8(jsonPath);
        return false;
    }
    std::string vertexPath;
    std::string pixelPath;
    if (!ReadStringField(json, "name", definition.name) ||
        !ReadStringField(json, "vertexShader", vertexPath) ||
        !ReadStringField(json, "pixelShader", pixelPath)) {
        error = "Shader JSON requires name, vertexShader, and pixelShader.";
        return false;
    }
    definition.jsonPath = jsonPath;
    definition.vertexShaderPath =
        ResolvePath(jsonPath.parent_path(), vertexPath);
    definition.pixelShaderPath =
        ResolvePath(jsonPath.parent_path(), pixelPath);

    std::string value;
    if (ReadStringField(json, "vertexEntry", value, false) && !value.empty()) {
        definition.vertexEntry.assign(value.begin(), value.end());
    }
    value.clear();
    if (ReadStringField(json, "pixelEntry", value, false) && !value.empty()) {
        definition.pixelEntry.assign(value.begin(), value.end());
    }
    value.clear();
    if (ReadStringField(json, "vertexProfile", value, false) && !value.empty()) {
        definition.vertexProfile.assign(value.begin(), value.end());
    }
    value.clear();
    if (ReadStringField(json, "pixelProfile", value, false) && !value.empty()) {
        definition.pixelProfile.assign(value.begin(), value.end());
    }
    ReadStringField(json, "blend", definition.blend, false);
    ReadStringField(json, "cull", definition.cull, false);
    ReadBoolField(json, "depthWrite", definition.depthWrite);
    ReadBoolField(json, "depthTest", definition.depthTest);

    std::set<std::string> names;
    std::array<bool, ShaderManager::kMaterialParameterBufferSize> occupied{};
    for (const std::string& object :
        ExtractObjectArray(json, "parameters")) {
        ShaderParameterDefinition parameter{};
        std::string typeText;
        uint32_t parameterSize = 0;
        if (!ReadStringField(object, "name", parameter.name) ||
            !ReadStringField(object, "type", typeText) ||
            !ReadUIntField(object, "offset", parameter.offset) ||
            !ParseParameterType(typeText, parameter.type, parameterSize)) {
            error = "Invalid shader parameter in: " + PathToUtf8(jsonPath);
            return false;
        }
        if (!names.insert(parameter.name).second ||
            parameter.offset % 4 != 0 ||
            parameter.offset + parameterSize >
                ShaderManager::kMaterialParameterBufferSize) {
            error = "Duplicate, unaligned, or out-of-range shader parameter: " +
                parameter.name;
            return false;
        }
        for (uint32_t byteIndex = parameter.offset;
            byteIndex < parameter.offset + parameterSize;
            ++byteIndex) {
            if (occupied[byteIndex]) {
                error = "Overlapping shader parameter: " + parameter.name;
                return false;
            }
            occupied[byteIndex] = true;
        }
        parameter.defaultValue.type = parameter.type;
        const std::vector<float> defaults = ReadDefaultNumbers(object);
        for (size_t index = 0;
            index < defaults.size() && index < 4;
            ++index) {
            parameter.defaultValue.floats[index] = defaults[index];
        }
        if (parameter.type == ShaderParameterType::Int ||
            parameter.type == ShaderParameterType::Bool) {
            parameter.defaultValue.integer = defaults.empty()
                ? 0 : static_cast<int32_t>(defaults[0]);
            bool boolDefault = false;
            if (parameter.type == ShaderParameterType::Bool &&
                ReadBoolField(object, "default", boolDefault)) {
                parameter.defaultValue.integer = boolDefault ? 1 : 0;
            }
        }
        definition.parameters.push_back(std::move(parameter));
    }
    return true;
}

void CollectDependenciesRecursive(
    const std::filesystem::path& shaderPath,
    std::set<std::filesystem::path>& visited) {
    std::error_code error;
    const auto canonical =
        std::filesystem::weakly_canonical(shaderPath, error);
    const auto resolved = !error ? canonical : shaderPath.lexically_normal();
    if (!visited.insert(resolved).second) {
        return;
    }
    std::string source;
    if (!ReadTextFile(resolved, source)) {
        return;
    }
    const std::regex includePattern(
        "#\\s*include\\s*\"([^\"]+)\"");
    for (std::sregex_iterator it(
            source.begin(), source.end(), includePattern), end;
        it != end;
        ++it) {
        CollectDependenciesRecursive(
            ResolvePath(resolved.parent_path(), (*it)[1].str()),
            visited);
    }
}

D3D12_CULL_MODE ParseCullMode(const std::string& value) {
    if (value == "None") {
        return D3D12_CULL_MODE_NONE;
    }
    if (value == "Front") {
        return D3D12_CULL_MODE_FRONT;
    }
    return D3D12_CULL_MODE_BACK;
}

} // namespace

void ShaderManager::Initialize(std::ostream& logStream) {
    std::scoped_lock lock(mutex_);
    if (dxcUtils_ != nullptr) {
        return;
    }
    HRESULT result =
        DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils_));
    if (SUCCEEDED(result)) {
        result = DxcCreateInstance(
            CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler_));
    }
    if (SUCCEEDED(result)) {
        result = dxcUtils_->CreateDefaultIncludeHandler(&includeHandler_);
    }
    if (FAILED(result)) {
        DX12Utility::Log(
            logStream,
            std::format(
                "ShaderManager initialization failed. HRESULT=0x{:08X}",
                static_cast<unsigned long>(result)));
        throw std::runtime_error("Failed to initialize ShaderManager.");
    }
    DX12Utility::Log(logStream, "ShaderManager initialized.");
}

Microsoft::WRL::ComPtr<IDxcBlob> ShaderManager::LoadShader(
    const std::wstring& filePath,
    const std::wstring& profile,
    std::ostream& logStream,
    const std::wstring& entryPoint) {
    return LoadShaderInternal(
        filePath, profile, logStream, entryPoint, false);
}

Microsoft::WRL::ComPtr<IDxcBlob> ShaderManager::ReloadShader(
    const std::wstring& filePath,
    const std::wstring& profile,
    std::ostream& logStream,
    const std::wstring& entryPoint) {
    return LoadShaderInternal(
        filePath, profile, logStream, entryPoint, true);
}

Microsoft::WRL::ComPtr<IDxcBlob> ShaderManager::LoadShaderInternal(
    const std::wstring& filePath,
    const std::wstring& profile,
    std::ostream& logStream,
    const std::wstring& entryPoint,
    bool forceReload) {
    std::scoped_lock lock(mutex_);
    if (dxcUtils_ == nullptr || filePath.empty() ||
        profile.empty() || entryPoint.empty()) {
        return {};
    }
    ShaderKey key{ ResolveShaderPath(filePath), profile, entryPoint };
    const auto cached = shaderCache_.find(key);
    if (!forceReload && cached != shaderCache_.end()) {
        return cached->second;
    }
    auto shader = CompileShader(key, logStream);
    if (shader != nullptr) {
        shaderCache_[key] = shader;
        return shader;
    }
    if (forceReload) {
        return {};
    }
    return cached != shaderCache_.end()
        ? cached->second
        : Microsoft::WRL::ComPtr<IDxcBlob>{};
}

Microsoft::WRL::ComPtr<IDxcBlob> ShaderManager::CompileShader(
    const ShaderKey& key,
    std::ostream& logStream) {
    DX12Utility::Log(
        logStream,
        DX12Utility::ConvertString(std::format(
            L"Begin shader compile, path:{}, profile:{}, entry:{}",
            key.filePath, key.profile, key.entryPoint)));
    Microsoft::WRL::ComPtr<IDxcBlobEncoding> source;
    HRESULT result =
        dxcUtils_->LoadFile(key.filePath.c_str(), nullptr, &source);
    if (FAILED(result) || source == nullptr) {
        DX12Utility::Log(logStream, "Failed to load HLSL file.");
        return {};
    }
    DxcBuffer buffer{
        source->GetBufferPointer(),
        source->GetBufferSize(),
        DXC_CP_UTF8
    };
    const std::wstring includeDirectory =
        std::filesystem::path(key.filePath).parent_path().wstring();
    std::vector<std::wstring> argumentStorage{
        key.filePath, L"-E", key.entryPoint, L"-T", key.profile,
        L"-I", includeDirectory, L"-HV", L"2021", L"-Zpr"
    };
#ifdef _DEBUG
    argumentStorage.insert(
        argumentStorage.end(), { L"-Zi", L"-Qembed_debug", L"-Od" });
#else
    argumentStorage.push_back(L"-O3");
#endif
    std::vector<LPCWSTR> arguments;
    for (const auto& argument : argumentStorage) {
        arguments.push_back(argument.c_str());
    }
    Microsoft::WRL::ComPtr<IDxcResult> compileResult;
    result = dxcCompiler_->Compile(
        &buffer,
        arguments.data(),
        static_cast<uint32_t>(arguments.size()),
        includeHandler_.Get(),
        IID_PPV_ARGS(&compileResult));
    if (FAILED(result) || compileResult == nullptr) {
        return {};
    }
    Microsoft::WRL::ComPtr<IDxcBlobUtf8> diagnostics;
    compileResult->GetOutput(
        DXC_OUT_ERRORS, IID_PPV_ARGS(&diagnostics), nullptr);
    if (diagnostics != nullptr && diagnostics->GetStringLength() > 0) {
        DX12Utility::Log(logStream, diagnostics->GetStringPointer());
    }
    HRESULT status = E_FAIL;
    if (FAILED(compileResult->GetStatus(&status)) || FAILED(status)) {
        DX12Utility::Log(logStream, "Shader compile failed.");
        return {};
    }
    Microsoft::WRL::ComPtr<IDxcBlob> shader;
    if (FAILED(compileResult->GetOutput(
            DXC_OUT_OBJECT, IID_PPV_ARGS(&shader), nullptr))) {
        return {};
    }
    DX12Utility::Log(logStream, "Shader compile succeeded.");
    return shader;
}

void ShaderManager::SetMaterialPipelineTemplate(
    ID3D12Device* device,
    ID3D12RootSignature* rootSignature,
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pipelineTemplate) {
    std::scoped_lock lock(mutex_);
    device_ = device;
    materialRootSignature_ = rootSignature;
    materialPipelineTemplate_ = pipelineTemplate;
    materialPipelineTemplate_.pRootSignature = materialRootSignature_.Get();
    materialPipelineTemplate_.VS = {};
    materialPipelineTemplate_.PS = {};
    materialInputElements_.assign(
        pipelineTemplate.InputLayout.pInputElementDescs,
        pipelineTemplate.InputLayout.pInputElementDescs +
            pipelineTemplate.InputLayout.NumElements);
    materialPipelineTemplate_.InputLayout = {
        materialInputElements_.data(),
        static_cast<UINT>(materialInputElements_.size())
    };
}

bool ShaderManager::LoadShaderDefinition(
    const std::filesystem::path& jsonPath,
    std::ostream& logStream,
    bool forceReload) {
    ParsedDefinition definition;
    std::string error;
    if (!ParseDefinition(jsonPath, definition, error)) {
        lastError_ = error;
        DX12Utility::Log(logStream, error);
        return false;
    }
    if (device_ == nullptr || materialRootSignature_ == nullptr) {
        lastError_ = "Material pipeline template is not configured.";
        return false;
    }

    auto vertexShader = forceReload
        ? ReloadShader(
            definition.vertexShaderPath.wstring(),
            definition.vertexProfile,
            logStream,
            definition.vertexEntry)
        : LoadShader(
            definition.vertexShaderPath.wstring(),
            definition.vertexProfile,
            logStream,
            definition.vertexEntry);
    auto pixelShader = forceReload
        ? ReloadShader(
            definition.pixelShaderPath.wstring(),
            definition.pixelProfile,
            logStream,
            definition.pixelEntry)
        : LoadShader(
            definition.pixelShaderPath.wstring(),
            definition.pixelProfile,
            logStream,
            definition.pixelEntry);
    if (vertexShader == nullptr || pixelShader == nullptr) {
        lastError_ = "Shader definition compile failed: " + definition.name;
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc =
        materialPipelineTemplate_;
    pipelineDesc.pRootSignature = materialRootSignature_.Get();
    pipelineDesc.InputLayout = {
        materialInputElements_.data(),
        static_cast<UINT>(materialInputElements_.size())
    };
    pipelineDesc.VS = {
        vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()
    };
    pipelineDesc.PS = {
        pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()
    };
    pipelineDesc.RasterizerState.CullMode =
        ParseCullMode(definition.cull);
    pipelineDesc.DepthStencilState.DepthEnable =
        definition.depthTest ? TRUE : FALSE;
    pipelineDesc.DepthStencilState.DepthWriteMask =
        definition.depthWrite
        ? D3D12_DEPTH_WRITE_MASK_ALL
        : D3D12_DEPTH_WRITE_MASK_ZERO;
    auto& blend = pipelineDesc.BlendState.RenderTarget[0];
    if (definition.blend == "Opaque") {
        blend.BlendEnable = FALSE;
    } else {
        blend.BlendEnable = TRUE;
        blend.SrcBlend = definition.blend == "Additive"
            ? D3D12_BLEND_ONE : D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend = definition.blend == "Additive"
            ? D3D12_BLEND_ONE : D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOp = D3D12_BLEND_OP_ADD;
    }

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
    const HRESULT result = device_->CreateGraphicsPipelineState(
        &pipelineDesc, IID_PPV_ARGS(&pipelineState));
    if (FAILED(result)) {
        lastError_ = std::format(
            "Failed to create material PSO '{}'. HRESULT=0x{:08X}",
            definition.name,
            static_cast<unsigned long>(result));
        DX12Utility::Log(logStream, lastError_);
        return false;
    }

    std::set<std::filesystem::path> dependencyPaths;
    dependencyPaths.insert(definition.jsonPath);
    CollectDependenciesRecursive(
        definition.vertexShaderPath, dependencyPaths);
    CollectDependenciesRecursive(
        definition.pixelShaderPath, dependencyPaths);
    std::vector<DependencyStamp> dependencies;
    for (const auto& path : dependencyPaths) {
        std::error_code timeError;
        dependencies.push_back({
            path, std::filesystem::last_write_time(path, timeError)
        });
    }

    std::scoped_lock lock(mutex_);
    auto existing = materialShaders_.find(definition.name);
    uint64_t generation = 1;
    if (existing != materialShaders_.end()) {
        generation = existing->second.generation + 1;
        if (existing->second.pipelineState != nullptr) {
            retiredPipelineStates_.push_back(
                existing->second.pipelineState);
        }
    }
    MaterialShaderRecord record{};
    record.name = definition.name;
    record.jsonPath = definition.jsonPath;
    record.vertexShaderPath = definition.vertexShaderPath;
    record.pixelShaderPath = definition.pixelShaderPath;
    record.vertexProfile = definition.vertexProfile;
    record.pixelProfile = definition.pixelProfile;
    record.vertexEntry = definition.vertexEntry;
    record.pixelEntry = definition.pixelEntry;
    record.parameters = std::move(definition.parameters);
    record.dependencies = std::move(dependencies);
    record.pipelineState = std::move(pipelineState);
    record.generation = generation;
    materialShaders_[record.name] = std::move(record);
    lastError_.clear();
    DX12Utility::Log(
        logStream,
        "Material shader loaded: " + definition.name);
    return true;
}

size_t ShaderManager::LoadShaderDefinitions(
    const std::filesystem::path& directory,
    std::ostream& logStream) {
    std::error_code error;
    if (!std::filesystem::exists(directory, error)) {
        DX12Utility::Log(
            logStream,
            "Shader definition directory does not exist: " +
                PathToUtf8(directory));
        return 0;
    }
    const auto canonicalDirectory =
        std::filesystem::weakly_canonical(directory, error);
    const auto resolvedDirectory =
        !error ? canonicalDirectory : directory.lexically_normal();
    {
        std::scoped_lock lock(mutex_);
        if (std::ranges::find(
                definitionDirectories_, resolvedDirectory) ==
            definitionDirectories_.end()) {
            definitionDirectories_.push_back(resolvedDirectory);
        }
    }
    size_t loadedCount = 0;
    for (const auto& entry :
        std::filesystem::recursive_directory_iterator(
            resolvedDirectory, error)) {
        if (error || !entry.is_regular_file()) {
            continue;
        }
        const std::string fileName =
            PathToUtf8(entry.path().filename());
        if (fileName.ends_with(".shader.json") &&
            LoadShaderDefinition(entry.path(), logStream)) {
            ++loadedCount;
        }
    }
    return loadedCount;
}

void ShaderManager::UpdateHotReload(std::ostream& logStream) {
    const auto now = std::chrono::steady_clock::now();
    if (lastHotReloadCheck_.time_since_epoch().count() != 0 &&
        now - lastHotReloadCheck_ < std::chrono::milliseconds(500)) {
        return;
    }
    lastHotReloadCheck_ = now;

    std::vector<std::filesystem::path> changedDefinitions;
    std::vector<std::filesystem::path> directories;
    std::set<std::filesystem::path> knownDefinitions;
    {
        std::scoped_lock lock(mutex_);
        directories = definitionDirectories_;
        for (const auto& [name, record] : materialShaders_) {
            std::error_code canonicalError;
            const auto canonicalPath = std::filesystem::weakly_canonical(
                record.jsonPath, canonicalError);
            knownDefinitions.insert(
                !canonicalError
                ? canonicalPath
                : record.jsonPath.lexically_normal());
            bool changed = false;
            for (const DependencyStamp& dependency : record.dependencies) {
                std::error_code error;
                const auto writeTime = std::filesystem::last_write_time(
                    dependency.path, error);
                if (error || writeTime != dependency.writeTime) {
                    changed = true;
                    break;
                }
            }
            if (changed) {
                changedDefinitions.push_back(record.jsonPath);
            }
        }
    }
    std::vector<std::filesystem::path> newDefinitions;
    for (const auto& directory : directories) {
        std::error_code scanError;
        for (const auto& entry :
            std::filesystem::recursive_directory_iterator(
                directory, scanError)) {
            if (scanError || !entry.is_regular_file()) {
                continue;
            }
            const std::string fileName =
                PathToUtf8(entry.path().filename());
            if (!fileName.ends_with(".shader.json")) {
                continue;
            }
            std::error_code canonicalError;
            const auto canonicalPath = std::filesystem::weakly_canonical(
                entry.path(), canonicalError);
            const auto resolvedPath = !canonicalError
                ? canonicalPath : entry.path().lexically_normal();
            if (!knownDefinitions.contains(resolvedPath)) {
                newDefinitions.push_back(resolvedPath);
            }
        }
    }
    for (const auto& path : newDefinitions) {
        DX12Utility::Log(
            logStream,
            "Discovered new shader definition: " + PathToUtf8(path));
        LoadShaderDefinition(path, logStream);
    }
    for (const auto& path : changedDefinitions) {
        DX12Utility::Log(
            logStream,
            "Hot reloading shader definition: " + PathToUtf8(path));
        if (!LoadShaderDefinition(path, logStream, true)) {
            DX12Utility::Log(
                logStream,
                "Hot reload failed; previous PSO remains active.");
        }
    }
}

ID3D12PipelineState* ShaderManager::GetMaterialPipelineState(
    const std::string& shaderName) const {
    std::scoped_lock lock(mutex_);
    const auto shader = materialShaders_.find(shaderName);
    return shader != materialShaders_.end()
        ? shader->second.pipelineState.Get() : nullptr;
}

std::vector<ShaderParameterDefinition>
ShaderManager::GetParameterDefinitions(
    const std::string& shaderName) const {
    std::scoped_lock lock(mutex_);
    const auto shader = materialShaders_.find(shaderName);
    return shader != materialShaders_.end()
        ? shader->second.parameters
        : std::vector<ShaderParameterDefinition>{};
}

std::vector<std::string> ShaderManager::GetShaderNames() const {
    std::scoped_lock lock(mutex_);
    std::vector<std::string> names;
    names.reserve(materialShaders_.size());
    for (const auto& [name, record] : materialShaders_) {
        names.push_back(name);
    }
    std::ranges::sort(names);
    return names;
}

uint64_t ShaderManager::GetShaderGeneration(
    const std::string& shaderName) const {
    std::scoped_lock lock(mutex_);
    const auto shader = materialShaders_.find(shaderName);
    return shader != materialShaders_.end()
        ? shader->second.generation : 0;
}

void ShaderManager::ClearCache() {
    std::scoped_lock lock(mutex_);
    shaderCache_.clear();
}

size_t ShaderManager::GetCachedShaderCount() const {
    std::scoped_lock lock(mutex_);
    return shaderCache_.size();
}

bool ShaderManager::IsInitialized() const {
    std::scoped_lock lock(mutex_);
    return dxcUtils_ != nullptr &&
        dxcCompiler_ != nullptr &&
        includeHandler_ != nullptr;
}

size_t ShaderManager::ShaderKeyHash::operator()(
    const ShaderKey& key) const {
    const std::hash<std::wstring> hasher;
    size_t result = hasher(key.filePath);
    result ^= hasher(key.profile) +
        0x9e3779b9u + (result << 6) + (result >> 2);
    result ^= hasher(key.entryPoint) +
        0x9e3779b9u + (result << 6) + (result >> 2);
    return result;
}
