#pragma once

#include "ShaderManager.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// JSONシェーダー定義に対応する、GameObjectごとのMaterialインスタンス。
class Material {
public:
    Material(ShaderManager* shaderManager, std::string shaderName);

    bool SetShader(std::string shaderName);
    const std::string& GetShaderName() const { return shaderName_; }

    bool SetFloat(const std::string& name, float value);
    bool SetFloat2(const std::string& name, float x, float y);
    bool SetFloat3(const std::string& name, float x, float y, float z);
    bool SetFloat4(
        const std::string& name, float x, float y, float z, float w);
    bool SetInt(const std::string& name, int32_t value);
    bool SetBool(const std::string& name, bool value);

    const ShaderParameterValue* GetParameter(const std::string& name) const;
    std::vector<ShaderParameterDefinition> GetParameterDefinitions() const;

    ID3D12PipelineState* GetPipelineState();
    void WriteParameterBuffer(void* destination, size_t destinationSize);

private:
    bool SetFloatValues(
        const std::string& name,
        ShaderParameterType expectedType,
        float x,
        float y,
        float z,
        float w);
    void SynchronizeDefinition();

    ShaderManager* shaderManager_ = nullptr;
    std::string shaderName_;
    uint64_t synchronizedGeneration_ = 0;
    std::unordered_map<std::string, ShaderParameterValue> parameters_;
};
