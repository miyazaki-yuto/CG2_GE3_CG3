#include "Material.h"

#include <cstring>
#include <utility>

Material::Material(
    ShaderManager* shaderManager,
    std::string shaderName)
    : shaderManager_(shaderManager) {
    SetShader(std::move(shaderName));
}

bool Material::SetShader(std::string shaderName) {
    if (shaderManager_ == nullptr ||
        shaderManager_->GetMaterialPipelineState(shaderName) == nullptr) {
        return false;
    }
    shaderName_ = std::move(shaderName);
    synchronizedGeneration_ = 0;
    SynchronizeDefinition();
    return true;
}

bool Material::SetFloat(const std::string& name, float value) {
    return SetFloatValues(
        name, ShaderParameterType::Float, value, 0.0f, 0.0f, 0.0f);
}

bool Material::SetFloat2(const std::string& name, float x, float y) {
    return SetFloatValues(
        name, ShaderParameterType::Float2, x, y, 0.0f, 0.0f);
}

bool Material::SetFloat3(
    const std::string& name, float x, float y, float z) {
    return SetFloatValues(
        name, ShaderParameterType::Float3, x, y, z, 0.0f);
}

bool Material::SetFloat4(
    const std::string& name, float x, float y, float z, float w) {
    return SetFloatValues(
        name, ShaderParameterType::Float4, x, y, z, w);
}

bool Material::SetFloatValues(
    const std::string& name,
    ShaderParameterType expectedType,
    float x,
    float y,
    float z,
    float w) {
    SynchronizeDefinition();
    auto parameter = parameters_.find(name);
    if (parameter == parameters_.end() ||
        parameter->second.type != expectedType) {
        return false;
    }
    parameter->second.floats = { x, y, z, w };
    return true;
}

bool Material::SetInt(const std::string& name, int32_t value) {
    SynchronizeDefinition();
    auto parameter = parameters_.find(name);
    if (parameter == parameters_.end() ||
        parameter->second.type != ShaderParameterType::Int) {
        return false;
    }
    parameter->second.integer = value;
    return true;
}

bool Material::SetBool(const std::string& name, bool value) {
    SynchronizeDefinition();
    auto parameter = parameters_.find(name);
    if (parameter == parameters_.end() ||
        parameter->second.type != ShaderParameterType::Bool) {
        return false;
    }
    parameter->second.integer = value ? 1 : 0;
    return true;
}

const ShaderParameterValue* Material::GetParameter(
    const std::string& name) const {
    const auto parameter = parameters_.find(name);
    return parameter != parameters_.end() ? &parameter->second : nullptr;
}

std::vector<ShaderParameterDefinition>
Material::GetParameterDefinitions() const {
    return shaderManager_ != nullptr
        ? shaderManager_->GetParameterDefinitions(shaderName_)
        : std::vector<ShaderParameterDefinition>{};
}

ID3D12PipelineState* Material::GetPipelineState() {
    SynchronizeDefinition();
    return shaderManager_ != nullptr
        ? shaderManager_->GetMaterialPipelineState(shaderName_)
        : nullptr;
}

void Material::WriteParameterBuffer(
    void* destination,
    size_t destinationSize) {
    if (destination == nullptr || destinationSize == 0) {
        return;
    }
    std::memset(destination, 0, destinationSize);
    SynchronizeDefinition();

    for (const ShaderParameterDefinition& definition :
        GetParameterDefinitions()) {
        const auto value = parameters_.find(definition.name);
        if (value == parameters_.end() ||
            definition.offset >= destinationSize) {
            continue;
        }

        std::byte* target =
            static_cast<std::byte*>(destination) + definition.offset;
        const size_t remainingSize = destinationSize - definition.offset;
        if (definition.type == ShaderParameterType::Int ||
            definition.type == ShaderParameterType::Bool) {
            if (remainingSize >= sizeof(int32_t)) {
                std::memcpy(
                    target, &value->second.integer, sizeof(int32_t));
            }
            continue;
        }

        size_t componentCount = 1;
        if (definition.type == ShaderParameterType::Float2) {
            componentCount = 2;
        } else if (definition.type == ShaderParameterType::Float3) {
            componentCount = 3;
        } else if (definition.type == ShaderParameterType::Float4) {
            componentCount = 4;
        }
        const size_t byteCount = sizeof(float) * componentCount;
        if (remainingSize >= byteCount) {
            std::memcpy(
                target, value->second.floats.data(), byteCount);
        }
    }
}

void Material::SynchronizeDefinition() {
    if (shaderManager_ == nullptr || shaderName_.empty()) {
        return;
    }
    const uint64_t generation =
        shaderManager_->GetShaderGeneration(shaderName_);
    if (generation == 0 || generation == synchronizedGeneration_) {
        return;
    }

    const auto oldParameters = parameters_;
    parameters_.clear();
    for (const ShaderParameterDefinition& definition :
        shaderManager_->GetParameterDefinitions(shaderName_)) {
        const auto oldValue = oldParameters.find(definition.name);
        if (oldValue != oldParameters.end() &&
            oldValue->second.type == definition.type) {
            parameters_[definition.name] = oldValue->second;
        } else {
            parameters_[definition.name] = definition.defaultValue;
        }
    }
    synchronizedGeneration_ = generation;
}
