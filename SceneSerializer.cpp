#include "SceneSerializer.h"

#include "AssetManager.h"
#include "CameraComponent.h"
#include "Component.h"
#include "DebugCamera.h"
#include "GameObject.h"
#include "Graphics.h"
#include "InputManager.h"
#include "LightComponent.h"
#include "LightingManager.h"
#include "Material.h"
#include "Model.h"
#include "ModelRendererComponent.h"
#include "PrimitiveRendererComponent.h"
#include "PrefabInstanceComponent.h"
#include "RendererComponent.h"
#include "Scene.h"
#include "SpriteRendererComponent.h"
#include "TransformComponent.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr int kSceneFormatVersion = 2;

const char* LightingModeToString(LightingMode mode) {
    switch (mode) {
    case LightingMode::Lambert:
        return "Lambert";
    case LightingMode::HalfLambert:
        return "HalfLambert";
    case LightingMode::PBR:
        return "PBR";
    case LightingMode::Current:
    default:
        return "Current";
    }
}

bool ParseLightingMode(const std::string& text, LightingMode& mode) {
    if (text == "Lambert") {
        mode = LightingMode::Lambert;
        return true;
    }
    if (text == "HalfLambert") {
        mode = LightingMode::HalfLambert;
        return true;
    }
    if (text == "Current") {
        mode = LightingMode::Current;
        return true;
    }
    if (text == "PBR") {
        mode = LightingMode::PBR;
        return true;
    }
    return false;
}

const char* ToneMappingModeToString(ToneMappingMode mode) {
    switch (mode) {
    case ToneMappingMode::None:
        return "None";
    case ToneMappingMode::Reinhard:
        return "Reinhard";
    case ToneMappingMode::ACES:
    default:
        return "ACES";
    }
}

bool ParseToneMappingMode(
    const std::string& text,
    ToneMappingMode& mode) {
    if (text == "None") {
        mode = ToneMappingMode::None;
        return true;
    }
    if (text == "Reinhard") {
        mode = ToneMappingMode::Reinhard;
        return true;
    }
    if (text == "ACES") {
        mode = ToneMappingMode::ACES;
        return true;
    }
    return false;
}

const char* AntiAliasingModeToString(AntiAliasingMode mode) {
    switch (mode) {
    case AntiAliasingMode::None: return "None";
    case AntiAliasingMode::TAA: return "TAA";
    case AntiAliasingMode::MAA: return "MAA";
    case AntiAliasingMode::FXAA:
    default: return "FXAA";
    }
}

bool ParseAntiAliasingMode(
    const std::string& text,
    AntiAliasingMode& mode) {
    if (text == "None") { mode = AntiAliasingMode::None; return true; }
    if (text == "FXAA") { mode = AntiAliasingMode::FXAA; return true; }
    if (text == "TAA") { mode = AntiAliasingMode::TAA; return true; }
    if (text == "MAA") { mode = AntiAliasingMode::MAA; return true; }
    return false;
}

// 外部JSONライブラリを増やさずに学習できるよう、Scene用の小さなJSON値を用意する。
struct JsonValue {
    enum class Type {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object
    };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::unordered_map<std::string, JsonValue> object;

    const JsonValue* Find(const std::string& key) const {
        if (type != Type::Object) {
            return nullptr;
        }
        const auto iterator = object.find(key);
        return iterator == object.end() ? nullptr : &iterator->second;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text) {}

    bool Parse(JsonValue& result, std::string& error) {
        SkipWhitespace();
        if (!ParseValue(result, 0)) {
            error = error_;
            return false;
        }
        SkipWhitespace();
        if (position_ != text_.size()) {
            SetError("Unexpected characters after the root value.");
            error = error_;
            return false;
        }
        return true;
    }

private:
    static constexpr size_t kMaximumDepth = 128;

    void SkipWhitespace() {
        while (position_ < text_.size() &&
            std::isspace(static_cast<unsigned char>(text_[position_])) != 0) {
            ++position_;
        }
    }

    void SetError(const std::string& message) {
        if (error_.empty()) {
            error_ = message + " (byte " + std::to_string(position_) + ")";
        }
    }

    bool Consume(char expected) {
        if (position_ >= text_.size() || text_[position_] != expected) {
            SetError(std::string("Expected '") + expected + "'.");
            return false;
        }
        ++position_;
        return true;
    }

    bool ParseValue(JsonValue& result, size_t depth) {
        if (depth > kMaximumDepth) {
            SetError("JSON nesting is too deep.");
            return false;
        }
        SkipWhitespace();
        if (position_ >= text_.size()) {
            SetError("Unexpected end of JSON.");
            return false;
        }

        switch (text_[position_]) {
        case 'n': return ParseLiteral("null", JsonValue::Type::Null, result);
        case 't': return ParseLiteral("true", JsonValue::Type::Boolean, result, true);
        case 'f': return ParseLiteral("false", JsonValue::Type::Boolean, result, false);
        case '"':
            result.type = JsonValue::Type::String;
            return ParseString(result.string);
        case '[': return ParseArray(result, depth + 1);
        case '{': return ParseObject(result, depth + 1);
        default:
            if (text_[position_] == '-' ||
                std::isdigit(static_cast<unsigned char>(text_[position_])) != 0) {
                return ParseNumber(result);
            }
            SetError("Invalid JSON value.");
            return false;
        }
    }

    bool ParseLiteral(
        const char* literal,
        JsonValue::Type type,
        JsonValue& result,
        bool boolean = false) {
        const size_t length = std::char_traits<char>::length(literal);
        if (text_.compare(position_, length, literal) != 0) {
            SetError("Invalid JSON literal.");
            return false;
        }
        position_ += length;
        result.type = type;
        result.boolean = boolean;
        return true;
    }

    static void AppendUtf8(std::string& result, uint32_t codePoint) {
        if (codePoint <= 0x7f) {
            result.push_back(static_cast<char>(codePoint));
        } else if (codePoint <= 0x7ff) {
            result.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
            result.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        } else {
            result.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
            result.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
        }
    }

    bool ParseHex4(uint32_t& result) {
        if (position_ + 4 > text_.size()) {
            SetError("Incomplete Unicode escape.");
            return false;
        }
        result = 0;
        for (int index = 0; index < 4; ++index) {
            const char character = text_[position_++];
            result <<= 4;
            if (character >= '0' && character <= '9') {
                result |= static_cast<uint32_t>(character - '0');
            } else if (character >= 'a' && character <= 'f') {
                result |= static_cast<uint32_t>(character - 'a' + 10);
            } else if (character >= 'A' && character <= 'F') {
                result |= static_cast<uint32_t>(character - 'A' + 10);
            } else {
                SetError("Invalid Unicode escape.");
                return false;
            }
        }
        return true;
    }

    bool ParseString(std::string& result) {
        if (!Consume('"')) {
            return false;
        }
        result.clear();
        while (position_ < text_.size()) {
            const char character = text_[position_++];
            if (character == '"') {
                return true;
            }
            if (static_cast<unsigned char>(character) < 0x20) {
                SetError("Control character in JSON string.");
                return false;
            }
            if (character != '\\') {
                result.push_back(character);
                continue;
            }
            if (position_ >= text_.size()) {
                SetError("Incomplete string escape.");
                return false;
            }
            const char escaped = text_[position_++];
            switch (escaped) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u': {
                uint32_t codePoint = 0;
                if (!ParseHex4(codePoint)) {
                    return false;
                }
                // SceneWriterはUTF-8を直接保存する。外部JSONの基本的な\uXXXXにも対応する。
                if (codePoint >= 0xd800 && codePoint <= 0xdfff) {
                    SetError("UTF-16 surrogate escapes are not supported.");
                    return false;
                }
                AppendUtf8(result, codePoint);
                break;
            }
            default:
                SetError("Invalid string escape.");
                return false;
            }
        }
        SetError("Unterminated JSON string.");
        return false;
    }

    bool ParseNumber(JsonValue& result) {
        const size_t start = position_;
        if (text_[position_] == '-') {
            ++position_;
        }
        if (position_ >= text_.size()) {
            SetError("Incomplete number.");
            return false;
        }
        if (text_[position_] == '0') {
            ++position_;
        } else if (std::isdigit(static_cast<unsigned char>(text_[position_])) != 0) {
            while (position_ < text_.size() &&
                std::isdigit(static_cast<unsigned char>(text_[position_])) != 0) {
                ++position_;
            }
        } else {
            SetError("Invalid number.");
            return false;
        }
        if (position_ < text_.size() && text_[position_] == '.') {
            ++position_;
            const size_t fractionStart = position_;
            while (position_ < text_.size() &&
                std::isdigit(static_cast<unsigned char>(text_[position_])) != 0) {
                ++position_;
            }
            if (fractionStart == position_) {
                SetError("Invalid number fraction.");
                return false;
            }
        }
        if (position_ < text_.size() &&
            (text_[position_] == 'e' || text_[position_] == 'E')) {
            ++position_;
            if (position_ < text_.size() &&
                (text_[position_] == '+' || text_[position_] == '-')) {
                ++position_;
            }
            const size_t exponentStart = position_;
            while (position_ < text_.size() &&
                std::isdigit(static_cast<unsigned char>(text_[position_])) != 0) {
                ++position_;
            }
            if (exponentStart == position_) {
                SetError("Invalid number exponent.");
                return false;
            }
        }

        const std::string numberText = text_.substr(start, position_ - start);
        char* end = nullptr;
        const double value = std::strtod(numberText.c_str(), &end);
        if (end == nullptr || *end != '\0' || !std::isfinite(value)) {
            SetError("Number is outside the supported range.");
            return false;
        }
        result.type = JsonValue::Type::Number;
        result.number = value;
        return true;
    }

    bool ParseArray(JsonValue& result, size_t depth) {
        if (!Consume('[')) {
            return false;
        }
        result.type = JsonValue::Type::Array;
        result.array.clear();
        SkipWhitespace();
        if (position_ < text_.size() && text_[position_] == ']') {
            ++position_;
            return true;
        }
        while (true) {
            JsonValue element;
            if (!ParseValue(element, depth)) {
                return false;
            }
            result.array.push_back(std::move(element));
            SkipWhitespace();
            if (position_ < text_.size() && text_[position_] == ']') {
                ++position_;
                return true;
            }
            if (!Consume(',')) {
                return false;
            }
            SkipWhitespace();
        }
    }

    bool ParseObject(JsonValue& result, size_t depth) {
        if (!Consume('{')) {
            return false;
        }
        result.type = JsonValue::Type::Object;
        result.object.clear();
        SkipWhitespace();
        if (position_ < text_.size() && text_[position_] == '}') {
            ++position_;
            return true;
        }
        while (true) {
            std::string key;
            if (!ParseString(key)) {
                return false;
            }
            SkipWhitespace();
            if (!Consume(':')) {
                return false;
            }
            JsonValue value;
            if (!ParseValue(value, depth)) {
                return false;
            }
            result.object[std::move(key)] = std::move(value);
            SkipWhitespace();
            if (position_ < text_.size() && text_[position_] == '}') {
                ++position_;
                return true;
            }
            if (!Consume(',')) {
                return false;
            }
            SkipWhitespace();
        }
    }

    const std::string& text_;
    size_t position_ = 0;
    std::string error_;
};

std::filesystem::path MakePathFromUtf8(const std::string& text) {
    const auto* first = reinterpret_cast<const char8_t*>(text.data());
    const std::u8string utf8(first, first + text.size());
    return std::filesystem::path(utf8);
}

void WriteIndent(std::ostream& output, int indent) {
    for (int index = 0; index < indent; ++index) {
        output.put(' ');
    }
}

void WriteString(std::ostream& output, const std::string& text) {
    static constexpr char kHex[] = "0123456789abcdef";
    output.put('"');
    for (const unsigned char character : text) {
        switch (character) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20) {
                output << "\\u00"
                    << kHex[(character >> 4) & 0x0f]
                    << kHex[character & 0x0f];
            } else {
                output.put(static_cast<char>(character));
            }
            break;
        }
    }
    output.put('"');
}

void WriteVector2(std::ostream& output, const Vector2& value) {
    output << '[' << value.x << ", " << value.y << ']';
}

void WriteVector3(std::ostream& output, const Vector3& value) {
    output << '[' << value.x << ", " << value.y << ", " << value.z << ']';
}

void WriteVector4(std::ostream& output, const Vector4& value) {
    output << '[' << value.x << ", " << value.y << ", "
        << value.z << ", " << value.w << ']';
}

void WriteColor4(std::ostream& output, const Color4& value) {
    output << '[' << value.r << ", " << value.g << ", "
        << value.b << ", " << value.a << ']';
}

void WriteUVTransform(
    std::ostream& output,
    const UVTransform& transform,
    int indent) {
    output << "{\n";
    WriteIndent(output, indent + 2);
    output << "\"scale\": ";
    WriteVector2(output, transform.scale);
    output << ",\n";
    WriteIndent(output, indent + 2);
    output << "\"rotation\": " << transform.rotate << ",\n";
    WriteIndent(output, indent + 2);
    output << "\"position\": ";
    WriteVector2(output, transform.translate);
    output << '\n';
    WriteIndent(output, indent);
    output << '}';
}

bool IsSerializableComponent(const Component& component) {
    return dynamic_cast<const ModelRendererComponent*>(&component) != nullptr ||
        dynamic_cast<const SpriteRendererComponent*>(&component) != nullptr ||
        dynamic_cast<const PrimitiveRendererComponent*>(&component) != nullptr ||
        dynamic_cast<const PrefabInstanceComponent*>(&component) != nullptr ||
        dynamic_cast<const LightComponent*>(&component) != nullptr ||
        dynamic_cast<const CameraComponent*>(&component) != nullptr;
}

void WriteRendererCommon(
    std::ostream& output,
    const RendererComponent& renderer,
    int indent) {
    output << ",\n";
    WriteIndent(output, indent);
    output << "\"renderOrder\": " << renderer.GetRenderOrder();
}

void WriteComponent(
    std::ostream& output,
    const Component& component,
    int indent) {
    WriteIndent(output, indent);
    output << "{\n";
    WriteIndent(output, indent + 2);

    if (const auto* modelRenderer =
        dynamic_cast<const ModelRendererComponent*>(&component)) {
        output << "\"type\": \"ModelRenderer\",\n";
        WriteIndent(output, indent + 2);
        output << "\"enabled\": " << (modelRenderer->IsEnabled() ? "true" : "false");
        WriteRendererCommon(output, *modelRenderer, indent + 2);
        output << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"color\": ";
        WriteVector4(output, modelRenderer->GetColor());
        output << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"uvTransform\": ";
        WriteUVTransform(output, modelRenderer->GetUVTransform(), indent + 2);
        output << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"lighting\": "
            << (modelRenderer->IsLightingEnabled() ? "true" : "false") << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"modelGuid\": ";
        WriteString(output, modelRenderer->GetModelGuid());
        output << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"skySphere\": "
            << (modelRenderer->GetModel() != nullptr &&
                modelRenderer->GetModel()->IsSkySphere() ? "true" : "false")
            << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"fallbackTextureGuid\": ";
        WriteString(output, modelRenderer->GetFallbackTextureGuid());
        output << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"materialTextureOverrides\": [";
        bool wroteMaterialTexture = false;
        const Model* model = modelRenderer->GetModel();

        // MTLと同じ状態のSlotは保存せず、Inspectorで上書きしたものだけを書き出す。
        // Material名とIndexを両方持たせ、MTLの並びが変わった場合にも復元しやすくする。
        if (model != nullptr) {
            for (uint32_t materialIndex = 0;
                materialIndex < model->GetMaterialCount();
                ++materialIndex) {
                const std::string& textureGuid =
                    modelRenderer->GetMaterialTextureGuid(materialIndex);
                const ModelMaterial* material = model->GetMaterial(materialIndex);
                if (textureGuid.empty() || material == nullptr) {
                    continue;
                }
                output << (wroteMaterialTexture ? ",\n" : "\n");
                WriteIndent(output, indent + 4);
                output << "{\"materialIndex\": " << materialIndex
                    << ", \"materialName\": ";
                WriteString(output, material->name);
                output << ", \"textureGuid\": ";
                WriteString(output, textureGuid);
                output << '}';
                wroteMaterialTexture = true;
            }
        }
        if (wroteMaterialTexture) {
            output << '\n';
            WriteIndent(output, indent + 2);
        }
        output << "],\n";
        WriteIndent(output, indent + 2);
        output << "\"materialNormalTextureOverrides\": [";
        bool wroteMaterialNormalTexture = false;

        // Normal Mapも色Textureと同じく、MTLから変更したSlotだけをSceneへ保存する。
        // 方向データなので、読み込み側ではsRGB変換を行わないLinear Textureとして復元する。
        if (model != nullptr) {
            for (uint32_t materialIndex = 0;
                materialIndex < model->GetMaterialCount();
                ++materialIndex) {
                const std::string& textureGuid =
                    modelRenderer->GetMaterialNormalTextureGuid(materialIndex);
                const ModelMaterial* material = model->GetMaterial(materialIndex);
                if (textureGuid.empty() || material == nullptr) {
                    continue;
                }
                output << (wroteMaterialNormalTexture ? ",\n" : "\n");
                WriteIndent(output, indent + 4);
                output << "{\"materialIndex\": " << materialIndex
                    << ", \"materialName\": ";
                WriteString(output, material->name);
                output << ", \"textureGuid\": ";
                WriteString(output, textureGuid);
                output << '}';
                wroteMaterialNormalTexture = true;
            }
        }
        if (wroteMaterialNormalTexture) {
            output << '\n';
            WriteIndent(output, indent + 2);
        }
        output << "],\n";
        WriteIndent(output, indent + 2);
        output << "\"materialPbrOverrides\": [";
        bool wroteMaterialPbr = false;
        if (model != nullptr) {
            for (uint32_t materialIndex = 0;
                materialIndex < model->GetMaterialCount();
                ++materialIndex) {
                const ModelMaterial* material =
                    model->GetMaterial(materialIndex);
                if (material == nullptr ||
                    !modelRenderer->IsMaterialPbrOverridden(materialIndex)) {
                    continue;
                }
                output << (wroteMaterialPbr ? ",\n" : "\n");
                WriteIndent(output, indent + 4);
                output << "{\"materialIndex\": " << materialIndex
                    << ", \"materialName\": ";
                WriteString(output, material->name);
                output << ", \"metallic\": "
                    << modelRenderer->GetMaterialMetallic(materialIndex)
                    << ", \"roughness\": "
                    << modelRenderer->GetMaterialRoughness(materialIndex)
                    << '}';
                wroteMaterialPbr = true;
            }
        }
        if (wroteMaterialPbr) {
            output << '\n';
            WriteIndent(output, indent + 2);
        }
        output << "],\n";
        WriteIndent(output, indent + 2);
        output << "\"materialUvOverrides\": [";
        bool wroteMaterialUV = false;

        // UVも個別設定がONのSlotだけを保存する。
        // OFFのSlotは読み込み後もModel Renderer全体のUVを使うため、JSONを小さく保てる。
        if (model != nullptr) {
            for (uint32_t materialIndex = 0;
                materialIndex < model->GetMaterialCount();
                ++materialIndex) {
                const ModelMaterial* material = model->GetMaterial(materialIndex);
                if (material == nullptr ||
                    !modelRenderer->IsMaterialUVTransformOverridden(
                        materialIndex)) {
                    continue;
                }
                output << (wroteMaterialUV ? ",\n" : "\n");
                WriteIndent(output, indent + 4);
                output << "{\n";
                WriteIndent(output, indent + 6);
                output << "\"materialIndex\": " << materialIndex << ",\n";
                WriteIndent(output, indent + 6);
                output << "\"materialName\": ";
                WriteString(output, material->name);
                output << ",\n";
                WriteIndent(output, indent + 6);
                output << "\"uvTransform\": ";
                WriteUVTransform(
                    output,
                    modelRenderer->GetMaterialUVTransform(materialIndex),
                    indent + 6);
                output << '\n';
                WriteIndent(output, indent + 4);
                output << '}';
                wroteMaterialUV = true;
            }
        }
        if (wroteMaterialUV) {
            output << '\n';
            WriteIndent(output, indent + 2);
        }
        output << "],\n";
        WriteIndent(output, indent + 2);
        output << "\"shaderMaterials\": [";
        bool wroteShaderMaterial = false;
        if (model != nullptr) {
            for (uint32_t materialIndex = 0;
                materialIndex < model->GetMaterialCount();
                ++materialIndex) {
                const std::shared_ptr<Material> shaderMaterial =
                    modelRenderer->GetShaderMaterial(materialIndex);
                const ModelMaterial* modelMaterial =
                    model->GetMaterial(materialIndex);
                if (shaderMaterial == nullptr || modelMaterial == nullptr) {
                    continue;
                }
                output << (wroteShaderMaterial ? ",\n" : "\n");
                WriteIndent(output, indent + 4);
                output << "{\"materialIndex\": " << materialIndex
                    << ", \"materialName\": ";
                WriteString(output, modelMaterial->name);
                output << ", \"shader\": ";
                WriteString(output, shaderMaterial->GetShaderName());
                output << ", \"parameters\": [";
                bool wroteParameter = false;
                for (const ShaderParameterDefinition& definition :
                    shaderMaterial->GetParameterDefinitions()) {
                    const ShaderParameterValue* value =
                        shaderMaterial->GetParameter(definition.name);
                    if (value == nullptr) {
                        continue;
                    }
                    output << (wroteParameter ? ", " : "");
                    output << "{\"name\": ";
                    WriteString(output, definition.name);
                    output << ", \"type\": "
                        << static_cast<int>(definition.type)
                        << ", \"values\": ["
                        << value->floats[0] << ", "
                        << value->floats[1] << ", "
                        << value->floats[2] << ", "
                        << value->floats[3] << "], \"integer\": "
                        << value->integer << '}';
                    wroteParameter = true;
                }
                output << "]}";
                wroteShaderMaterial = true;
            }
        }
        if (wroteShaderMaterial) {
            output << '\n';
            WriteIndent(output, indent + 2);
        }
        output << "]\n";
    } else if (const auto* spriteRenderer =
        dynamic_cast<const SpriteRendererComponent*>(&component)) {
        output << "\"type\": \"SpriteRenderer\",\n";
        WriteIndent(output, indent + 2);
        output << "\"enabled\": " << (spriteRenderer->IsEnabled() ? "true" : "false");
        WriteRendererCommon(output, *spriteRenderer, indent + 2);
        output << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"color\": ";
        WriteVector4(output, spriteRenderer->GetColor());
        output << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"uvTransform\": ";
        WriteUVTransform(output, spriteRenderer->GetUVTransform(), indent + 2);
        output << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"textureGuid\": ";
        WriteString(output, spriteRenderer->GetTextureGuid());
        output << '\n';
    } else if (const auto* primitiveRenderer =
        dynamic_cast<const PrimitiveRendererComponent*>(&component)) {
        output << "\"type\": \"PrimitiveRenderer\",\n";
        WriteIndent(output, indent + 2);
        output << "\"enabled\": " << (primitiveRenderer->IsEnabled() ? "true" : "false");
        WriteRendererCommon(output, *primitiveRenderer, indent + 2);
        output << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"primitiveType\": ";
        WriteString(output,
            primitiveRenderer->GetPrimitiveType() ==
            PrimitiveRendererComponent::PrimitiveType::Triangle
            ? "Triangle" : "Sphere");
        output << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"color\": ";
        WriteVector4(output, primitiveRenderer->GetColor());
        output << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"uvTransform\": ";
        WriteUVTransform(output, primitiveRenderer->GetUVTransform(), indent + 2);
        output << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"textureGuid\": ";
        WriteString(output, primitiveRenderer->GetTextureGuid());
        output << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"triangleVertices\": [\n";
        const auto& vertices = primitiveRenderer->GetTriangleVertices();
        for (size_t index = 0; index < vertices.size(); ++index) {
            const TextureVertexData& vertex = vertices[index];
            WriteIndent(output, indent + 4);
            output << "{ \"position\": ";
            WriteVector4(output, vertex.position);
            output << ", \"uv\": ";
            WriteVector2(output, vertex.texcoord);
            output << ", \"normal\": ";
            WriteVector3(output, vertex.normal);
            output << " }" << (index + 1 < vertices.size() ? "," : "") << '\n';
        }
        WriteIndent(output, indent + 2);
        output << "]\n";
    } else if (const auto* prefab =
        dynamic_cast<const PrefabInstanceComponent*>(&component)) {
        output << "\"type\": \"PrefabInstance\",\n";
        WriteIndent(output, indent + 2);
        output << "\"enabled\": "
            << (prefab->IsEnabled() ? "true" : "false") << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"prefabGuid\": ";
        WriteString(output, prefab->GetPrefabGuid());
        output << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"autoUpdate\": "
            << (prefab->IsAutoUpdateEnabled() ? "true" : "false") << '\n';
    } else if (const auto* light =
        dynamic_cast<const LightComponent*>(&component)) {
        output << "\"type\": \"Light\",\n";
        WriteIndent(output, indent + 2);
        output << "\"enabled\": " << (light->IsEnabled() ? "true" : "false") << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"lightType\": ";
        WriteString(output,
            light->GetLightType() == LightComponent::LightType::Directional
            ? "Directional" : "Point");
        output << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"lightEnabled\": "
            << (light->IsLightEnabled() ? "true" : "false") << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"color\": ";
        WriteColor4(output, light->GetColor());
        output << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"intensity\": " << light->GetIntensity() << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"radius\": " << light->GetRadius() << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"decay\": " << light->GetDecay() << '\n';
    } else if (const auto* camera =
        dynamic_cast<const CameraComponent*>(&component)) {
        output << "\"type\": \"Camera\",\n";
        WriteIndent(output, indent + 2);
        output << "\"enabled\": " << (camera->IsEnabled() ? "true" : "false") << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"inputEnabled\": "
            << (camera->IsInputEnabled() ? "true" : "false") << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"target\": ";
        WriteVector3(output, camera->GetTarget());
        output << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"yaw\": " << camera->GetYaw() << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"pitch\": " << camera->GetPitch() << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"distance\": " << camera->GetDistance() << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"orthographic\": "
            << (camera->IsOrthographic() ? "true" : "false") << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"fovY\": " << camera->GetFovY() << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"nearClip\": " << camera->GetNearClip() << ",\n";
        WriteIndent(output, indent + 2);
        output << "\"farClip\": " << camera->GetFarClip() << '\n';
    }

    WriteIndent(output, indent);
    output << '}';
}

bool SetReadError(
    std::string& error,
    const std::string& field,
    const std::string& expected) {
    error = "Field '" + field + "' must be " + expected + ".";
    return false;
}

bool ReadString(
    const JsonValue& object,
    const char* key,
    std::string& result,
    std::string& error) {
    const JsonValue* value = object.Find(key);
    if (value == nullptr || value->type != JsonValue::Type::String) {
        return SetReadError(error, key, "a string");
    }
    result = value->string;
    return true;
}

// version 2はGUID、version 1はファイルパスを持つ。
// 古いSceneを一度読み込んで保存すれば、自動的にGUID形式へ移行される。
bool ReadAssetReference(
    const JsonValue& object,
    const char* guidKey,
    const char* legacyPathKey,
    std::string& result,
    std::string& error) {
    if (object.Find(guidKey) != nullptr) {
        return ReadString(object, guidKey, result, error);
    }
    return ReadString(object, legacyPathKey, result, error);
}

bool ReadBool(
    const JsonValue& object,
    const char* key,
    bool& result,
    std::string& error) {
    const JsonValue* value = object.Find(key);
    if (value == nullptr || value->type != JsonValue::Type::Boolean) {
        return SetReadError(error, key, "a boolean");
    }
    result = value->boolean;
    return true;
}

bool ReadFloat(
    const JsonValue& object,
    const char* key,
    float& result,
    std::string& error) {
    const JsonValue* value = object.Find(key);
    if (value == nullptr || value->type != JsonValue::Type::Number ||
        value->number < -(std::numeric_limits<float>::max)() ||
        value->number > (std::numeric_limits<float>::max)()) {
        return SetReadError(error, key, "a finite float");
    }
    result = static_cast<float>(value->number);
    return true;
}

bool ReadInt(
    const JsonValue& object,
    const char* key,
    int& result,
    std::string& error) {
    const JsonValue* value = object.Find(key);
    if (value == nullptr || value->type != JsonValue::Type::Number ||
        std::floor(value->number) != value->number ||
        value->number < static_cast<double>((std::numeric_limits<int>::min)()) ||
        value->number > static_cast<double>((std::numeric_limits<int>::max)())) {
        return SetReadError(error, key, "an integer");
    }
    result = static_cast<int>(value->number);
    return true;
}

bool ReadIdValue(
    const JsonValue& value,
    GameObject::Id& result,
    std::string& error,
    const char* fieldName) {
    if (value.type != JsonValue::Type::Number ||
        std::floor(value.number) != value.number || value.number <= 0.0 ||
        value.number > static_cast<double>((std::numeric_limits<GameObject::Id>::max)())) {
        return SetReadError(error, fieldName, "a positive integer ID");
    }
    result = static_cast<GameObject::Id>(value.number);
    return true;
}

bool ReadVector2Value(
    const JsonValue& value,
    Vector2& result,
    std::string& error,
    const char* fieldName) {
    if (value.type != JsonValue::Type::Array || value.array.size() != 2) {
        return SetReadError(error, fieldName, "an array of 2 numbers");
    }
    for (const JsonValue& element : value.array) {
        if (element.type != JsonValue::Type::Number ||
            element.number < -(std::numeric_limits<float>::max)() ||
            element.number > (std::numeric_limits<float>::max)()) {
            return SetReadError(error, fieldName, "an array of 2 finite numbers");
        }
    }
    result = {
        static_cast<float>(value.array[0].number),
        static_cast<float>(value.array[1].number)
    };
    return true;
}

bool ReadVector3Value(
    const JsonValue& value,
    Vector3& result,
    std::string& error,
    const char* fieldName) {
    if (value.type != JsonValue::Type::Array || value.array.size() != 3) {
        return SetReadError(error, fieldName, "an array of 3 numbers");
    }
    for (const JsonValue& element : value.array) {
        if (element.type != JsonValue::Type::Number ||
            element.number < -(std::numeric_limits<float>::max)() ||
            element.number > (std::numeric_limits<float>::max)()) {
            return SetReadError(error, fieldName, "an array of 3 finite numbers");
        }
    }
    result = {
        static_cast<float>(value.array[0].number),
        static_cast<float>(value.array[1].number),
        static_cast<float>(value.array[2].number)
    };
    return true;
}

bool ReadVector4Value(
    const JsonValue& value,
    Vector4& result,
    std::string& error,
    const char* fieldName) {
    if (value.type != JsonValue::Type::Array || value.array.size() != 4) {
        return SetReadError(error, fieldName, "an array of 4 numbers");
    }
    for (const JsonValue& element : value.array) {
        if (element.type != JsonValue::Type::Number ||
            element.number < -(std::numeric_limits<float>::max)() ||
            element.number > (std::numeric_limits<float>::max)()) {
            return SetReadError(error, fieldName, "an array of 4 finite numbers");
        }
    }
    result = {
        static_cast<float>(value.array[0].number),
        static_cast<float>(value.array[1].number),
        static_cast<float>(value.array[2].number),
        static_cast<float>(value.array[3].number)
    };
    return true;
}

bool ReadColor4Value(
    const JsonValue& value,
    Color4& result,
    std::string& error,
    const char* fieldName) {
    Vector4 vector{};
    if (!ReadVector4Value(value, vector, error, fieldName)) {
        return false;
    }
    result = { vector.x, vector.y, vector.z, vector.w };
    return true;
}

bool ReadUVTransform(
    const JsonValue& object,
    UVTransform& result,
    std::string& error) {
    const JsonValue* scale = object.Find("scale");
    const JsonValue* position = object.Find("position");
    if (object.type != JsonValue::Type::Object || scale == nullptr ||
        position == nullptr ||
        !ReadVector2Value(*scale, result.scale, error, "uvTransform.scale") ||
        !ReadFloat(object, "rotation", result.rotate, error) ||
        !ReadVector2Value(
            *position, result.translate, error, "uvTransform.position")) {
        return false;
    }
    return true;
}

bool ReadTransform(
    const JsonValue& object,
    TransformData& result,
    std::string& error) {
    const JsonValue* scale = object.Find("scale");
    const JsonValue* rotation = object.Find("rotation");
    const JsonValue* position = object.Find("position");
    if (object.type != JsonValue::Type::Object || scale == nullptr ||
        rotation == nullptr || position == nullptr ||
        !ReadVector3Value(*scale, result.scale, error, "transform.scale") ||
        !ReadVector3Value(
            *rotation, result.rotate, error, "transform.rotation") ||
        !ReadVector3Value(
            *position, result.translate, error, "transform.position")) {
        return false;
    }
    return true;
}

bool ValidateComponent(const JsonValue& component, std::string& error) {
    if (component.type != JsonValue::Type::Object) {
        return SetReadError(error, "components[]", "an object");
    }
    std::string type;
    bool enabled = false;
    if (!ReadString(component, "type", type, error) ||
        !ReadBool(component, "enabled", enabled, error)) {
        return false;
    }
    (void)enabled;

    if (type == "ModelRenderer" || type == "SpriteRenderer" ||
        type == "PrimitiveRenderer") {
        int renderOrder = 0;
        Vector4 color{};
        UVTransform uv{};
        const JsonValue* colorValue = component.Find("color");
        const JsonValue* uvValue = component.Find("uvTransform");
        std::string assetReference;
        if (!ReadInt(component, "renderOrder", renderOrder, error) ||
            colorValue == nullptr || uvValue == nullptr ||
            !ReadVector4Value(*colorValue, color, error, "color") ||
            !ReadUVTransform(*uvValue, uv, error)) {
            return false;
        }
        (void)renderOrder;
        (void)color;
        (void)uv;

        if (type == "ModelRenderer") {
            bool lighting = false;
            bool skySphere = false;
            std::string modelReference;
            if (!ReadBool(component, "lighting", lighting, error) ||
                !ReadAssetReference(
                    component, "modelGuid", "modelPath",
                    modelReference, error) ||
                !ReadBool(component, "skySphere", skySphere, error) ||
                !ReadAssetReference(
                    component, "fallbackTextureGuid",
                    "fallbackTexturePath", assetReference, error)) {
                return false;
            }

            const JsonValue* overrides =
                component.Find("materialTextureOverrides");
            if (overrides != nullptr) {
                if (overrides->type != JsonValue::Type::Array) {
                    return SetReadError(
                        error, "materialTextureOverrides", "an array");
                }
                for (const JsonValue& overrideValue : overrides->array) {
                    int materialIndex = -1;
                    std::string materialName;
                    std::string textureGuid;
                    if (overrideValue.type != JsonValue::Type::Object ||
                        !ReadInt(
                            overrideValue,
                            "materialIndex",
                            materialIndex,
                            error) ||
                        materialIndex < 0 ||
                        !ReadString(
                            overrideValue,
                            "materialName",
                            materialName,
                            error) ||
                        !ReadString(
                            overrideValue,
                            "textureGuid",
                            textureGuid,
                            error)) {
                        if (error.empty()) {
                            error = "materialTextureOverrides[] is invalid.";
                        }
                        return false;
                    }
                }
            }

            const JsonValue* normalOverrides =
                component.Find("materialNormalTextureOverrides");
            if (normalOverrides != nullptr) {
                if (normalOverrides->type != JsonValue::Type::Array) {
                    return SetReadError(
                        error, "materialNormalTextureOverrides", "an array");
                }
                for (const JsonValue& overrideValue : normalOverrides->array) {
                    int materialIndex = -1;
                    std::string materialName;
                    std::string textureGuid;
                    if (overrideValue.type != JsonValue::Type::Object ||
                        !ReadInt(
                            overrideValue,
                            "materialIndex",
                            materialIndex,
                            error) ||
                        materialIndex < 0 ||
                        !ReadString(
                            overrideValue,
                            "materialName",
                            materialName,
                            error) ||
                        !ReadString(
                            overrideValue,
                            "textureGuid",
                            textureGuid,
                            error)) {
                        if (error.empty()) {
                            error =
                                "materialNormalTextureOverrides[] is invalid.";
                        }
                        return false;
                    }
                }
            }

            const JsonValue* pbrOverrides =
                component.Find("materialPbrOverrides");
            if (pbrOverrides != nullptr) {
                if (pbrOverrides->type != JsonValue::Type::Array) {
                    return SetReadError(
                        error, "materialPbrOverrides", "an array");
                }
                for (const JsonValue& overrideValue : pbrOverrides->array) {
                    int materialIndex = -1;
                    std::string materialName;
                    float metallic = 0.0f;
                    float roughness = 0.5f;
                    if (overrideValue.type != JsonValue::Type::Object ||
                        !ReadInt(
                            overrideValue,
                            "materialIndex",
                            materialIndex,
                            error) ||
                        materialIndex < 0 ||
                        !ReadString(
                            overrideValue,
                            "materialName",
                            materialName,
                            error) ||
                        !ReadFloat(
                            overrideValue,
                            "metallic",
                            metallic,
                            error) ||
                        !ReadFloat(
                            overrideValue,
                            "roughness",
                            roughness,
                            error) ||
                        metallic < 0.0f || metallic > 1.0f ||
                        roughness < 0.04f || roughness > 1.0f) {
                        if (error.empty()) {
                            error = "materialPbrOverrides[] is invalid.";
                        }
                        return false;
                    }
                }
            }

            const JsonValue* uvOverrides =
                component.Find("materialUvOverrides");

            // Material別UVが存在しない旧Sceneもそのまま読み込めるよう、
            // この項目自体は必須にせず、存在する場合だけ内容を厳密に検証する。
            if (uvOverrides == nullptr) {
                return true;
            }
            if (uvOverrides->type != JsonValue::Type::Array) {
                return SetReadError(
                    error, "materialUvOverrides", "an array");
            }
            for (const JsonValue& overrideValue : uvOverrides->array) {
                int materialIndex = -1;
                std::string materialName;
                UVTransform materialUV{};
                const JsonValue* materialUVValue =
                    overrideValue.Find("uvTransform");
                if (overrideValue.type != JsonValue::Type::Object ||
                    !ReadInt(
                        overrideValue, "materialIndex", materialIndex, error) ||
                    materialIndex < 0 ||
                    !ReadString(
                        overrideValue, "materialName", materialName, error) ||
                    materialUVValue == nullptr ||
                    !ReadUVTransform(
                        *materialUVValue, materialUV, error)) {
                    if (error.empty()) {
                        error = "materialUvOverrides[] is invalid.";
                    }
                    return false;
                }
            }
            return true;
        }
        if (!ReadAssetReference(
            component, "textureGuid", "texturePath",
            assetReference, error)) {
            return false;
        }
        if (type == "PrimitiveRenderer") {
            std::string primitiveType;
            const JsonValue* vertices = component.Find("triangleVertices");
            if (!ReadString(
                component, "primitiveType", primitiveType, error) ||
                (primitiveType != "Triangle" && primitiveType != "Sphere") ||
                vertices == nullptr || vertices->type != JsonValue::Type::Array ||
                vertices->array.size() !=
                PrimitiveRendererComponent::kTriangleVertexCount) {
                error = "PrimitiveRenderer contains invalid primitive or vertices.";
                return false;
            }
            for (const JsonValue& vertex : vertices->array) {
                if (vertex.type != JsonValue::Type::Object) {
                    error = "triangleVertices[] must be an object.";
                    return false;
                }
                Vector4 position{};
                Vector2 uvValueResult{};
                Vector3 normal{};
                const JsonValue* positionValue = vertex.Find("position");
                const JsonValue* vertexUv = vertex.Find("uv");
                const JsonValue* normalValue = vertex.Find("normal");
                if (positionValue == nullptr || vertexUv == nullptr ||
                    normalValue == nullptr ||
                    !ReadVector4Value(
                        *positionValue, position, error, "vertex.position") ||
                    !ReadVector2Value(*vertexUv, uvValueResult, error, "vertex.uv") ||
                    !ReadVector3Value(
                        *normalValue, normal, error, "vertex.normal")) {
                    return false;
                }
            }
        }
        return true;
    }

    if (type == "PrefabInstance") {
        std::string prefabGuid;
        bool autoUpdate = true;
        return ReadString(component, "prefabGuid", prefabGuid, error) &&
            ReadBool(component, "autoUpdate", autoUpdate, error);
    }

    if (type == "Light") {
        std::string lightType;
        bool lightEnabled = false;
        Color4 color{};
        float value = 0.0f;
        const JsonValue* colorValue = component.Find("color");
        return ReadString(component, "lightType", lightType, error) &&
            (lightType == "Directional" || lightType == "Point") &&
            ReadBool(component, "lightEnabled", lightEnabled, error) &&
            colorValue != nullptr &&
            ReadColor4Value(*colorValue, color, error, "color") &&
            ReadFloat(component, "intensity", value, error) &&
            ReadFloat(component, "radius", value, error) &&
            ReadFloat(component, "decay", value, error);
    }

    if (type == "Camera") {
        bool valueBool = false;
        float valueFloat = 0.0f;
        Vector3 target{};
        const JsonValue* targetValue = component.Find("target");
        return ReadBool(component, "inputEnabled", valueBool, error) &&
            targetValue != nullptr &&
            ReadVector3Value(*targetValue, target, error, "target") &&
            ReadFloat(component, "yaw", valueFloat, error) &&
            ReadFloat(component, "pitch", valueFloat, error) &&
            ReadFloat(component, "distance", valueFloat, error) &&
            ReadBool(component, "orthographic", valueBool, error) &&
            ReadFloat(component, "fovY", valueFloat, error) &&
            ReadFloat(component, "nearClip", valueFloat, error) &&
            ReadFloat(component, "farClip", valueFloat, error);
    }

    // 未知Componentは将来バージョンとの互換性のため、読み込み時に警告して飛ばす。
    return true;
}

bool ValidateDocument(const JsonValue& root, std::string& error) {
    if (root.type != JsonValue::Type::Object) {
        error = "Scene JSON root must be an object.";
        return false;
    }
    int version = 0;
    std::string format;
    std::string sceneName;
    if (!ReadString(root, "format", format, error) ||
        format != "CG2Scene" ||
        !ReadInt(root, "version", version, error) ||
        (version != 1 && version != kSceneFormatVersion)) {
        error = "Unsupported Scene JSON format or version.";
        return false;
    }
    if (!ReadString(root, "name", sceneName, error)) {
        return false;
    }
    const JsonValue* gameObjects = root.Find("gameObjects");
    const JsonValue* lighting = root.Find("lighting");
    if (gameObjects == nullptr || gameObjects->type != JsonValue::Type::Array) {
        return SetReadError(error, "gameObjects", "an array");
    }
    if (lighting == nullptr || lighting->type != JsonValue::Type::Object) {
        return SetReadError(error, "lighting", "an object");
    }
    if (const JsonValue* lightingMode = lighting->Find("mode")) {
        LightingMode parsedMode = LightingMode::Current;
        if (lightingMode->type != JsonValue::Type::String ||
            !ParseLightingMode(lightingMode->string, parsedMode)) {
            error =
                "lighting.mode must be Lambert, HalfLambert, Current, or PBR.";
            return false;
        }
    }
    float lightingValue = 0.0f;
    if (!ReadFloat(*lighting, "specularStrength", lightingValue, error) ||
        !ReadFloat(*lighting, "specularShininess", lightingValue, error)) {
        return false;
    }
    if (lighting->Find("environmentEnabled") != nullptr) {
        bool environmentEnabled = false;
        if (!ReadBool(
                *lighting, "environmentEnabled", environmentEnabled, error)) {
            return false;
        }
    }
    if (lighting->Find("environmentIntensity") != nullptr &&
        !ReadFloat(
            *lighting, "environmentIntensity", lightingValue, error)) {
        return false;
    }
    if (lighting->Find("environmentRotation") != nullptr &&
        !ReadFloat(
            *lighting, "environmentRotation", lightingValue, error)) {
        return false;
    }
    if (lighting->Find("environmentTextureGuid") != nullptr) {
        std::string environmentTextureGuid;
        if (!ReadString(
                *lighting,
                "environmentTextureGuid",
                environmentTextureGuid,
                error)) {
            return false;
        }
    }
    if (const JsonValue* toneMapping =
        lighting->Find("toneMapping")) {
        ToneMappingMode parsedToneMapping = ToneMappingMode::ACES;
        if (toneMapping->type != JsonValue::Type::String ||
            !ParseToneMappingMode(
                toneMapping->string, parsedToneMapping)) {
            error =
                "lighting.toneMapping must be None, Reinhard, or ACES.";
            return false;
        }
    }
    if (lighting->Find("exposure") != nullptr &&
        !ReadFloat(*lighting, "exposure", lightingValue, error)) {
        return false;
    }
    if (const JsonValue* antiAliasing =
        lighting->Find("antiAliasing")) {
        AntiAliasingMode parsedMode = AntiAliasingMode::FXAA;
        if (antiAliasing->type != JsonValue::Type::String ||
            !ParseAntiAliasingMode(antiAliasing->string, parsedMode)) {
            error = "lighting.antiAliasing must be None, FXAA, TAA, or MAA.";
            return false;
        }
    }
    bool postEnabled = false;
    if (lighting->Find("ssaoEnabled") != nullptr &&
        !ReadBool(*lighting, "ssaoEnabled", postEnabled, error)) {
        return false;
    }
    if (lighting->Find("bloomEnabled") != nullptr &&
        !ReadBool(*lighting, "bloomEnabled", postEnabled, error)) {
        return false;
    }
    if (lighting->Find("ssaoStrength") != nullptr &&
        !ReadFloat(*lighting, "ssaoStrength", lightingValue, error)) {
        return false;
    }
    if (lighting->Find("bloomIntensity") != nullptr &&
        !ReadFloat(*lighting, "bloomIntensity", lightingValue, error)) {
        return false;
    }
    if (lighting->Find("bloomThreshold") != nullptr &&
        !ReadFloat(*lighting, "bloomThreshold", lightingValue, error)) {
        return false;
    }

    std::unordered_set<GameObject::Id> ids;
    std::unordered_map<GameObject::Id, GameObject::Id> parents;
    size_t cameraCount = 0;
    for (const JsonValue& object : gameObjects->array) {
        if (object.type != JsonValue::Type::Object) {
            return SetReadError(error, "gameObjects[]", "an object");
        }
        const JsonValue* idValue = object.Find("id");
        const JsonValue* parentValue = object.Find("parentId");
        const JsonValue* transformValue = object.Find("transform");
        const JsonValue* components = object.Find("components");
        GameObject::Id id = 0;
        std::string name;
        bool active = false;
        TransformData transform{};
        if (idValue == nullptr || !ReadIdValue(*idValue, id, error, "id") ||
            !ReadString(object, "name", name, error) ||
            !ReadBool(object, "active", active, error) ||
            transformValue == nullptr ||
            !ReadTransform(*transformValue, transform, error) ||
            components == nullptr || components->type != JsonValue::Type::Array) {
            if (error.empty()) {
                error = "GameObject contains invalid fields.";
            }
            return false;
        }
        if (!ids.insert(id).second) {
            error = "Duplicate GameObject ID: " + std::to_string(id);
            return false;
        }
        if (parentValue == nullptr) {
            return SetReadError(error, "parentId", "null or an ID");
        }
        if (parentValue->type == JsonValue::Type::Null) {
            parents[id] = 0;
        } else {
            GameObject::Id parentId = 0;
            if (!ReadIdValue(*parentValue, parentId, error, "parentId")) {
                return false;
            }
            parents[id] = parentId;
        }
        for (const JsonValue& component : components->array) {
            if (!ValidateComponent(component, error)) {
                return false;
            }
            std::string type;
            if (ReadString(component, "type", type, error) && type == "Camera") {
                ++cameraCount;
            }
        }
    }
    if (cameraCount > 1) {
        error = "Only one Camera component is currently supported.";
        return false;
    }
    for (const auto& [id, parentId] : parents) {
        if (parentId != 0 && ids.find(parentId) == ids.end()) {
            error = "Parent ID does not exist: " + std::to_string(parentId);
            return false;
        }
        std::unordered_set<GameObject::Id> chain;
        GameObject::Id current = id;
        while (current != 0) {
            if (!chain.insert(current).second) {
                error = "Hierarchy cycle detected at ID " + std::to_string(current);
                return false;
            }
            current = parents[current];
        }
    }
    return true;
}

void AppendWarning(std::string& warnings, const std::string& warning) {
    if (!warnings.empty()) {
        warnings += '\n';
    }
    warnings += "Warning: " + warning;
}

AssetGuid ResolveAssetGuid(
    const JsonValue& source,
    const char* guidKey,
    const char* legacyPathKey,
    AssetType type,
    AssetManager* assetManager,
    std::string& warnings) {
    const JsonValue* guidValue = source.Find(guidKey);
    const JsonValue* pathValue = source.Find(legacyPathKey);
    if (guidValue != nullptr) {
        return guidValue->string;
    }
    if (pathValue == nullptr || pathValue->string.empty()) {
        return {};
    }
    if (assetManager == nullptr) {
        AppendWarning(warnings, "AssetManager is not available.");
        return {};
    }

    // version 1のパス参照をImportし、対応する.metaとGUIDを作る。
    std::string importError;
    const AssetGuid guid = type == AssetType::Model
        ? assetManager->ImportModel(pathValue->string, &importError)
        : assetManager->ImportTexture(pathValue->string, &importError);
    if (guid.empty()) {
        AppendWarning(warnings, importError);
    }
    return guid;
}

struct LoadedTextureAsset {
    int textureHandle = -1;
    AssetGuid guid;
};

LoadedTextureAsset LoadTextureOrDefault(
    const JsonValue& source,
    const char* guidKey,
    const char* legacyPathKey,
    AssetManager* assetManager,
    int defaultTextureHandle,
    const AssetGuid& defaultTextureGuid,
    std::string& warnings) {
    const AssetGuid guid = ResolveAssetGuid(
        source, guidKey, legacyPathKey,
        AssetType::Texture, assetManager, warnings);
    if (guid.empty() || assetManager == nullptr) {
        return { defaultTextureHandle, defaultTextureGuid };
    }

    std::string loadError;
    const int textureHandle = assetManager->LoadTexture(guid, &loadError);
    if (textureHandle < 0) {
        AppendWarning(warnings, loadError);
        return { defaultTextureHandle, defaultTextureGuid };
    }
    return { textureHandle, guid };
}

struct CameraRestoreState {
    bool valid = false;
    GameObject::Id ownerId = 0;
    Vector3 target{};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float distance = 8.0f;
    bool orthographic = false;
    float fovY = 0.45f;
    float nearClip = 0.1f;
    float farClip = 1000.0f;
};

bool AddComponentFromJson(
    const JsonValue& source,
    GameObject& owner,
    AssetManager* assetManager,
    Graphics* graphics,
    InputManager* inputManager,
    int defaultTextureHandle,
    const AssetGuid& defaultTextureGuid,
    CameraRestoreState& cameraState,
    std::string& warnings,
    std::string& error) {
    std::string type;
    bool enabled = true;
    if (!ReadString(source, "type", type, error) ||
        !ReadBool(source, "enabled", enabled, error)) {
        return false;
    }

    if (type == "ModelRenderer") {
        int renderOrder = 0;
        Vector4 color{};
        UVTransform uv{};
        bool lighting = true;
        bool skySphere = false;
        const JsonValue* colorValue = source.Find("color");
        const JsonValue* uvValue = source.Find("uvTransform");
        ReadInt(source, "renderOrder", renderOrder, error);
        ReadVector4Value(*colorValue, color, error, "color");
        ReadUVTransform(*uvValue, uv, error);
        ReadBool(source, "lighting", lighting, error);
        ReadBool(source, "skySphere", skySphere, error);

        const AssetGuid modelGuid = ResolveAssetGuid(
            source, "modelGuid", "modelPath",
            AssetType::Model, assetManager, warnings);
        if (assetManager == nullptr || modelGuid.empty()) {
            AppendWarning(warnings, "Model GUID is empty or unresolved.");
            return true;
        }
        std::string modelError;
        std::shared_ptr<Model> model = assetManager->LoadModel(
            modelGuid, skySphere, &modelError);
        if (model == nullptr) {
            AppendWarning(warnings, modelError);
            return true;
        }
        const LoadedTextureAsset texture = LoadTextureOrDefault(
            source,
            "fallbackTextureGuid",
            "fallbackTexturePath",
            assetManager,
            defaultTextureHandle,
            defaultTextureGuid,
            warnings);
        auto* component = owner.AddComponent<ModelRendererComponent>(
            std::move(model),
            texture.textureHandle,
            modelGuid,
            texture.guid);
        component->SetUVTransform(uv);
        if (const JsonValue* overrides =
            source.Find("materialTextureOverrides")) {
            for (const JsonValue& overrideValue : overrides->array) {
                int savedMaterialIndex = -1;
                std::string materialName;
                AssetGuid textureGuid;
                ReadInt(
                    overrideValue,
                    "materialIndex",
                    savedMaterialIndex,
                    error);
                ReadString(
                    overrideValue,
                    "materialName",
                    materialName,
                    error);
                ReadString(
                    overrideValue,
                    "textureGuid",
                    textureGuid,
                    error);

                // まずMaterial名で探し、名前が見つからない場合だけ保存時Indexを使う。
                // MTL内のnewmtl順が入れ替わっても、同じ名前なら正しいSlotへ復元できる。
                int materialIndex = component->GetModel()->FindMaterialIndex(
                    materialName);
                if (materialIndex < 0 && savedMaterialIndex >= 0 &&
                    static_cast<uint32_t>(savedMaterialIndex) <
                    component->GetModel()->GetMaterialCount()) {
                    materialIndex = savedMaterialIndex;
                }
                if (materialIndex < 0) {
                    AppendWarning(
                        warnings,
                        "Material texture slot could not be found: " +
                        materialName);
                    continue;
                }

                std::string textureError;
                const int textureHandle =
                    assetManager->LoadTexture(textureGuid, &textureError);
                if (textureHandle < 0) {
                    AppendWarning(warnings, textureError);
                    continue;
                }
                component->SetMaterialTextureAsset(
                    static_cast<uint32_t>(materialIndex),
                    textureHandle,
                    textureGuid);
            }
        }
        if (const JsonValue* overrides =
            source.Find("materialNormalTextureOverrides")) {
            for (const JsonValue& overrideValue : overrides->array) {
                int savedMaterialIndex = -1;
                std::string materialName;
                AssetGuid textureGuid;
                ReadInt(
                    overrideValue,
                    "materialIndex",
                    savedMaterialIndex,
                    error);
                ReadString(
                    overrideValue,
                    "materialName",
                    materialName,
                    error);
                ReadString(
                    overrideValue,
                    "textureGuid",
                    textureGuid,
                    error);

                int materialIndex = component->GetModel()->FindMaterialIndex(
                    materialName);
                if (materialIndex < 0 && savedMaterialIndex >= 0 &&
                    static_cast<uint32_t>(savedMaterialIndex) <
                    component->GetModel()->GetMaterialCount()) {
                    materialIndex = savedMaterialIndex;
                }
                if (materialIndex < 0) {
                    AppendWarning(
                        warnings,
                        "Material normal map slot could not be found: " +
                        materialName);
                    continue;
                }

                std::string textureError;
                const int textureHandle =
                    assetManager->LoadLinearTexture(textureGuid, &textureError);
                if (textureHandle < 0) {
                    AppendWarning(warnings, textureError);
                    continue;
                }
                component->SetMaterialNormalTextureAsset(
                    static_cast<uint32_t>(materialIndex),
                    textureHandle,
                    textureGuid);
            }
        }
        if (const JsonValue* overrides =
            source.Find("materialPbrOverrides")) {
            for (const JsonValue& overrideValue : overrides->array) {
                int savedMaterialIndex = -1;
                std::string materialName;
                float metallic = 0.0f;
                float roughness = 0.5f;
                ReadInt(
                    overrideValue,
                    "materialIndex",
                    savedMaterialIndex,
                    error);
                ReadString(
                    overrideValue,
                    "materialName",
                    materialName,
                    error);
                ReadFloat(
                    overrideValue, "metallic", metallic, error);
                ReadFloat(
                    overrideValue, "roughness", roughness, error);

                int materialIndex = component->GetModel()->FindMaterialIndex(
                    materialName);
                if (materialIndex < 0 && savedMaterialIndex >= 0 &&
                    static_cast<uint32_t>(savedMaterialIndex) <
                    component->GetModel()->GetMaterialCount()) {
                    materialIndex = savedMaterialIndex;
                }
                if (materialIndex < 0) {
                    AppendWarning(
                        warnings,
                        "Material PBR slot could not be found: " +
                        materialName);
                    continue;
                }
                const uint32_t resolvedIndex =
                    static_cast<uint32_t>(materialIndex);
                component->SetMaterialPbrOverridden(resolvedIndex, true);
                component->SetMaterialMetallic(resolvedIndex, metallic);
                component->SetMaterialRoughness(resolvedIndex, roughness);
            }
        }
        if (const JsonValue* uvOverrides =
            source.Find("materialUvOverrides")) {
            for (const JsonValue& overrideValue : uvOverrides->array) {
                int savedMaterialIndex = -1;
                std::string materialName;
                UVTransform materialUV{};
                ReadInt(
                    overrideValue,
                    "materialIndex",
                    savedMaterialIndex,
                    error);
                ReadString(
                    overrideValue,
                    "materialName",
                    materialName,
                    error);
                const JsonValue* materialUVValue =
                    overrideValue.Find("uvTransform");
                ReadUVTransform(*materialUVValue, materialUV, error);

                // Textureと同じルールでSlotを解決し、対応するMaterialだけへUVを戻す。
                int materialIndex = component->GetModel()->FindMaterialIndex(
                    materialName);
                if (materialIndex < 0 && savedMaterialIndex >= 0 &&
                    static_cast<uint32_t>(savedMaterialIndex) <
                    component->GetModel()->GetMaterialCount()) {
                    materialIndex = savedMaterialIndex;
                }
                if (materialIndex < 0) {
                    AppendWarning(
                        warnings,
                        "Material UV slot could not be found: " +
                        materialName);
                    continue;
                }

                component->SetMaterialUVTransformOverridden(
                    static_cast<uint32_t>(materialIndex), true);
                component->SetMaterialUVTransform(
                    static_cast<uint32_t>(materialIndex), materialUV);
            }
        }
        if (const JsonValue* shaderMaterials =
            source.Find("shaderMaterials");
            shaderMaterials != nullptr &&
            shaderMaterials->type == JsonValue::Type::Array &&
            graphics != nullptr) {
            for (const JsonValue& savedMaterial : shaderMaterials->array) {
                int savedMaterialIndex = -1;
                std::string materialName;
                std::string shaderName;
                if (!ReadInt(
                        savedMaterial,
                        "materialIndex",
                        savedMaterialIndex,
                        error) ||
                    !ReadString(
                        savedMaterial,
                        "materialName",
                        materialName,
                        error) ||
                    !ReadString(
                        savedMaterial,
                        "shader",
                        shaderName,
                        error)) {
                    AppendWarning(
                        warnings,
                        "Shader Material entry is invalid.");
                    error.clear();
                    continue;
                }
                int materialIndex =
                    component->GetModel()->FindMaterialIndex(materialName);
                if (materialIndex < 0 &&
                    savedMaterialIndex >= 0 &&
                    static_cast<uint32_t>(savedMaterialIndex) <
                        component->GetModel()->GetMaterialCount()) {
                    materialIndex = savedMaterialIndex;
                }
                std::shared_ptr<Material> shaderMaterial =
                    graphics->CreateMaterial(shaderName);
                if (materialIndex < 0 || shaderMaterial == nullptr) {
                    AppendWarning(
                        warnings,
                        "Shader Material could not be restored: " +
                            shaderName);
                    continue;
                }
                const JsonValue* parameters =
                    savedMaterial.Find("parameters");
                if (parameters != nullptr &&
                    parameters->type == JsonValue::Type::Array) {
                    for (const JsonValue& savedParameter :
                        parameters->array) {
                        std::string parameterName;
                        int parameterType = 0;
                        int integer = 0;
                        const JsonValue* values =
                            savedParameter.Find("values");
                        if (!ReadString(
                                savedParameter,
                                "name",
                                parameterName,
                                error) ||
                            !ReadInt(
                                savedParameter,
                                "type",
                                parameterType,
                                error) ||
                            !ReadInt(
                                savedParameter,
                                "integer",
                                integer,
                                error) ||
                            values == nullptr ||
                            values->type != JsonValue::Type::Array ||
                            values->array.size() != 4) {
                            error.clear();
                            continue;
                        }
                        float value[4]{};
                        bool valuesValid = true;
                        for (size_t index = 0; index < 4; ++index) {
                            if (values->array[index].type !=
                                JsonValue::Type::Number) {
                                valuesValid = false;
                                break;
                            }
                            value[index] = static_cast<float>(
                                values->array[index].number);
                        }
                        if (!valuesValid) {
                            continue;
                        }
                        switch (static_cast<ShaderParameterType>(
                            parameterType)) {
                        case ShaderParameterType::Float:
                            shaderMaterial->SetFloat(
                                parameterName, value[0]);
                            break;
                        case ShaderParameterType::Float2:
                            shaderMaterial->SetFloat2(
                                parameterName, value[0], value[1]);
                            break;
                        case ShaderParameterType::Float3:
                            shaderMaterial->SetFloat3(
                                parameterName,
                                value[0],
                                value[1],
                                value[2]);
                            break;
                        case ShaderParameterType::Float4:
                            shaderMaterial->SetFloat4(
                                parameterName,
                                value[0],
                                value[1],
                                value[2],
                                value[3]);
                            break;
                        case ShaderParameterType::Int:
                            shaderMaterial->SetInt(
                                parameterName, integer);
                            break;
                        case ShaderParameterType::Bool:
                            shaderMaterial->SetBool(
                                parameterName, integer != 0);
                            break;
                        }
                    }
                }
                component->SetShaderMaterial(
                    static_cast<uint32_t>(materialIndex),
                    std::move(shaderMaterial));
            }
        }
        component->SetRenderOrder(renderOrder);
        component->SetColor(color);
        component->SetUVTransform(uv);
        component->SetLightingEnabled(lighting);
        component->SetEnabled(enabled);
        return true;
    }

    if (type == "SpriteRenderer") {
        int renderOrder = 0;
        Vector4 color{};
        UVTransform uv{};
        const JsonValue* colorValue = source.Find("color");
        const JsonValue* uvValue = source.Find("uvTransform");
        ReadInt(source, "renderOrder", renderOrder, error);
        ReadVector4Value(*colorValue, color, error, "color");
        ReadUVTransform(*uvValue, uv, error);
        const LoadedTextureAsset texture = LoadTextureOrDefault(
            source,
            "textureGuid",
            "texturePath",
            assetManager,
            defaultTextureHandle,
            defaultTextureGuid,
            warnings);
        auto* component = owner.AddComponent<SpriteRendererComponent>(
            graphics != nullptr ? graphics->GetSprite() : nullptr,
            texture.textureHandle,
            texture.guid);
        component->SetRenderOrder(renderOrder);
        component->SetColor(color);
        component->SetUVTransform(uv);
        component->SetEnabled(enabled);
        return true;
    }

    if (type == "PrimitiveRenderer") {
        int renderOrder = 0;
        Vector4 color{};
        UVTransform uv{};
        std::string primitiveType;
        const JsonValue* colorValue = source.Find("color");
        const JsonValue* uvValue = source.Find("uvTransform");
        const JsonValue* verticesValue = source.Find("triangleVertices");
        ReadInt(source, "renderOrder", renderOrder, error);
        ReadVector4Value(*colorValue, color, error, "color");
        ReadUVTransform(*uvValue, uv, error);
        ReadString(source, "primitiveType", primitiveType, error);
        const LoadedTextureAsset texture = LoadTextureOrDefault(
            source,
            "textureGuid",
            "texturePath",
            assetManager,
            defaultTextureHandle,
            defaultTextureGuid,
            warnings);
        auto* component = owner.AddComponent<PrimitiveRendererComponent>(
            graphics != nullptr ? graphics->GetPrimitiveDrawer() : nullptr,
            primitiveType == "Triangle"
            ? PrimitiveRendererComponent::PrimitiveType::Triangle
            : PrimitiveRendererComponent::PrimitiveType::Sphere,
            texture.textureHandle,
            texture.guid);
        component->SetRenderOrder(renderOrder);
        component->SetColor(color);
        component->SetUVTransform(uv);

        PrimitiveRendererComponent::TriangleVertices vertices{};
        for (size_t index = 0; index < vertices.size(); ++index) {
            const JsonValue& vertex = verticesValue->array[index];
            ReadVector4Value(
                *vertex.Find("position"), vertices[index].position,
                error, "vertex.position");
            ReadVector2Value(
                *vertex.Find("uv"), vertices[index].texcoord,
                error, "vertex.uv");
            ReadVector3Value(
                *vertex.Find("normal"), vertices[index].normal,
                error, "vertex.normal");
        }
        component->SetTriangleVertices(vertices);
        component->SetEnabled(enabled);
        return true;
    }

    if (type == "PrefabInstance") {
        std::string prefabGuid;
        bool autoUpdate = true;
        ReadString(source, "prefabGuid", prefabGuid, error);
        ReadBool(source, "autoUpdate", autoUpdate, error);
        auto* component = owner.AddComponent<PrefabInstanceComponent>(
            std::move(prefabGuid), autoUpdate);
        component->SetEnabled(enabled);
        return true;
    }

    if (type == "Light") {
        std::string lightType;
        bool lightEnabled = true;
        Color4 color{};
        float intensity = 1.0f;
        float radius = 10.0f;
        float decay = 2.0f;
        ReadString(source, "lightType", lightType, error);
        ReadBool(source, "lightEnabled", lightEnabled, error);
        ReadColor4Value(*source.Find("color"), color, error, "color");
        ReadFloat(source, "intensity", intensity, error);
        ReadFloat(source, "radius", radius, error);
        ReadFloat(source, "decay", decay, error);
        auto* component = owner.AddComponent<LightComponent>(
            graphics != nullptr ? graphics->GetLightingManager() : nullptr,
            lightType == "Directional"
            ? LightComponent::LightType::Directional
            : LightComponent::LightType::Point);
        component->SetColor(color);
        component->SetIntensity(intensity);
        component->SetRadius(radius);
        component->SetDecay(decay);
        component->SetLightEnabled(lightEnabled);
        component->ApplyLight();
        component->SetEnabled(enabled);
        return true;
    }

    if (type == "Camera") {
        bool inputEnabled = true;
        ReadBool(source, "inputEnabled", inputEnabled, error);
        auto* component = owner.AddComponent<CameraComponent>(
            graphics != nullptr ? graphics->GetDebugCamera() : nullptr,
            inputManager,
            graphics != nullptr ? graphics->GetLightingManager() : nullptr);
        component->SetInputEnabled(inputEnabled);
        component->SetEnabled(enabled);

        cameraState.valid = true;
        cameraState.ownerId = owner.GetId();
        ReadVector3Value(
            *source.Find("target"), cameraState.target, error, "target");
        ReadFloat(source, "yaw", cameraState.yaw, error);
        ReadFloat(source, "pitch", cameraState.pitch, error);
        ReadFloat(source, "distance", cameraState.distance, error);
        ReadBool(source, "orthographic", cameraState.orthographic, error);
        ReadFloat(source, "fovY", cameraState.fovY, error);
        ReadFloat(source, "nearClip", cameraState.nearClip, error);
        ReadFloat(source, "farClip", cameraState.farClip, error);
        component->SetCameraState(
            cameraState.target,
            cameraState.yaw,
            cameraState.pitch,
            cameraState.distance,
            cameraState.orthographic,
            cameraState.fovY,
            cameraState.nearClip,
            cameraState.farClip);
        return true;
    }

    AppendWarning(warnings, "Unknown Component was skipped: " + type);
    return true;
}

} // namespace

void SceneSerializer::Initialize(
    AssetManager* assetManager,
    Graphics* graphics,
    InputManager* inputManager,
    int defaultTextureHandle,
    std::string defaultTextureGuid) {
    assetManager_ = assetManager;
    graphics_ = graphics;
    inputManager_ = inputManager;
    defaultTextureHandle_ = defaultTextureHandle;
    defaultTextureGuid_ = std::move(defaultTextureGuid);
}

bool SceneSerializer::SerializeToString(
    const Scene& scene,
    std::string& json,
    std::string& resultMessage) const {
    return SerializeToStringInternal(scene, nullptr, json, resultMessage);
}

bool SceneSerializer::SerializeHierarchyToString(
    const Scene& scene,
    const GameObject& hierarchyRoot,
    std::string& json,
    std::string& resultMessage) const {
    if (hierarchyRoot.GetScene() != &scene ||
        hierarchyRoot.IsPendingDestroy()) {
        resultMessage = "Prefab root does not belong to the Scene.";
        return false;
    }
    return SerializeToStringInternal(
        scene, &hierarchyRoot, json, resultMessage);
}

bool SceneSerializer::SerializeToStringInternal(
    const Scene& scene,
    const GameObject* hierarchyRoot,
    std::string& json,
    std::string& resultMessage) const {
    std::ostringstream output;
    output << std::setprecision(9);

    const LightingManager* lightingManager =
        graphics_ != nullptr ? graphics_->GetLightingManager() : nullptr;

    output << "{\n  \"format\": \"CG2Scene\",\n";
    output << "  \"version\": " << kSceneFormatVersion << ",\n";
    output << "  \"name\": ";
    WriteString(output, scene.GetName());
    output << ",\n  \"lighting\": {\n";
    output << "    \"mode\": ";
    WriteString(
        output,
        LightingModeToString(
            lightingManager != nullptr
                ? lightingManager->GetLightingMode()
                : LightingMode::Current));
    output << ",\n";
    output << "    \"specularStrength\": "
        << (lightingManager != nullptr
            ? lightingManager->GetSpecularStrength() : 1.0f) << ",\n";
    output << "    \"specularShininess\": "
        << (lightingManager != nullptr
            ? lightingManager->GetSpecularShininess() : 32.0f) << ",\n";
    output << "    \"environmentEnabled\": "
        << (lightingManager != nullptr &&
            lightingManager->IsEnvironmentEnabled() ? "true" : "false")
        << ",\n";
    output << "    \"environmentIntensity\": "
        << (lightingManager != nullptr
            ? lightingManager->GetEnvironmentIntensity() : 0.25f) << ",\n";
    output << "    \"environmentRotation\": "
        << (lightingManager != nullptr
            ? lightingManager->GetEnvironmentRotation() : 0.0f) << ",\n";
    output << "    \"environmentTextureGuid\": ";
    WriteString(
        output,
        lightingManager != nullptr
            ? lightingManager->GetEnvironmentTextureGuid()
            : std::string{});
    output << ",\n";
    output << "    \"toneMapping\": ";
    WriteString(
        output,
        graphics_ != nullptr
            ? ToneMappingModeToString(graphics_->GetToneMappingMode())
            : "ACES");
    output << ",\n";
    output << "    \"exposure\": "
        << (graphics_ != nullptr ? graphics_->GetExposure() : 0.0f)
        << ",\n";
    output << "    \"antiAliasing\": ";
    WriteString(
        output,
        graphics_ != nullptr
            ? AntiAliasingModeToString(graphics_->GetAntiAliasingMode())
            : "FXAA");
    output << ",\n";
    output << "    \"ssaoEnabled\": "
        << (graphics_ != nullptr && graphics_->IsSsaoEnabled()
            ? "true" : "false") << ",\n";
    output << "    \"ssaoStrength\": "
        << (graphics_ != nullptr ? graphics_->GetSsaoStrength() : 0.7f)
        << ",\n";
    output << "    \"bloomEnabled\": "
        << (graphics_ != nullptr && graphics_->IsBloomEnabled()
            ? "true" : "false") << ",\n";
    output << "    \"bloomIntensity\": "
        << (graphics_ != nullptr ? graphics_->GetBloomIntensity() : 0.18f)
        << ",\n";
    output << "    \"bloomThreshold\": "
        << (graphics_ != nullptr ? graphics_->GetBloomThreshold() : 1.0f)
        << "\n";
    output << "  },\n  \"gameObjects\": [\n";

    // Destroy予約済みのGameObjectは次のUpdateで消えるため、履歴や保存対象に含めない。
    std::vector<const GameObject*> gameObjects;
    for (const std::unique_ptr<GameObject>& gameObject :
        scene.GetGameObjects()) {
        bool belongsToHierarchy = hierarchyRoot == nullptr;
        for (const GameObject* current = gameObject.get();
            !belongsToHierarchy && current != nullptr;
            current = current->GetParent()) {
            belongsToHierarchy = current == hierarchyRoot;
        }
        if (!gameObject->IsPendingDestroy() && belongsToHierarchy) {
            gameObjects.push_back(gameObject.get());
        }
    }
    for (size_t objectIndex = 0;
        objectIndex < gameObjects.size(); ++objectIndex) {
        const GameObject& gameObject = *gameObjects[objectIndex];
        const TransformData& transform =
            gameObject.GetTransform().GetLocalTransform();
        output << "    {\n      \"id\": " << gameObject.GetId() << ",\n";
        output << "      \"name\": ";
        WriteString(output, gameObject.GetName());
        output << ",\n      \"active\": "
            << (gameObject.IsActive() ? "true" : "false") << ",\n";
        output << "      \"parentId\": ";
        if (gameObject.GetParent() != nullptr &&
            &gameObject != hierarchyRoot) {
            output << gameObject.GetParent()->GetId();
        } else {
            output << "null";
        }
        output << ",\n      \"transform\": {\n";
        output << "        \"scale\": ";
        WriteVector3(output, transform.scale);
        output << ",\n        \"rotation\": ";
        WriteVector3(output, transform.rotate);
        output << ",\n        \"position\": ";
        WriteVector3(output, transform.translate);
        output << "\n      },\n      \"components\": [\n";

        std::vector<const Component*> components;
        for (const std::unique_ptr<Component>& component :
            gameObject.GetComponents()) {
            if (IsSerializableComponent(*component)) {
                components.push_back(component.get());
            }
        }
        for (size_t componentIndex = 0;
            componentIndex < components.size(); ++componentIndex) {
            WriteComponent(
                output, *components[componentIndex], 8);
            output << (componentIndex + 1 < components.size() ? "," : "")
                << '\n';
        }
        output << "      ]\n    }"
            << (objectIndex + 1 < gameObjects.size() ? "," : "") << '\n';
    }
    output << "  ]\n}\n";

    if (!output.good()) {
        resultMessage = "An error occurred while serializing the Scene.";
        return false;
    }
    json = output.str();
    resultMessage = "Scene captured in memory.";
    return true;
}

bool SceneSerializer::Save(
    const Scene& scene,
    const std::string& filePath,
    std::string& resultMessage) const {
    if (filePath.empty()) {
        resultMessage = "Scene file path is empty.";
        return false;
    }

    std::string json;
    if (!SerializeToString(scene, json, resultMessage)) {
        return false;
    }

    const std::filesystem::path outputPath = MakePathFromUtf8(filePath);
    std::error_code errorCode;
    if (outputPath.has_parent_path()) {
        std::filesystem::create_directories(
            outputPath.parent_path(), errorCode);
        if (errorCode) {
            resultMessage = "Could not create the Scene directory.";
            return false;
        }
    }

    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        resultMessage = "Could not open the Scene file for writing.";
        return false;
    }
    output.write(json.data(), static_cast<std::streamsize>(json.size()));
    if (!output.good()) {
        resultMessage = "An error occurred while writing the Scene file.";
        return false;
    }

    resultMessage = "Scene saved: " + filePath;
    return true;
}

bool SceneSerializer::DeserializeFromString(
    Scene& scene,
    const std::string& json,
    std::string& resultMessage,
    bool applySceneSettings) const {
    if (json.empty()) {
        resultMessage = "Scene restore failed: snapshot is empty.";
        return false;
    }

    JsonValue root;
    std::string error;
    JsonParser parser(json);
    if (!parser.Parse(root, error) || !ValidateDocument(root, error)) {
        resultMessage = "Scene load failed: " + error;
        return false;
    }

    std::string sceneName;
    ReadString(root, "name", sceneName, error);
    const JsonValue& sourceObjects = *root.Find("gameObjects");
    Scene loadedScene(sceneName);
    std::unordered_map<GameObject::Id, GameObject*> objectsById;
    std::string warnings;

    // 1. 全GameObjectをID付きで先に作り、親IDを解決できる状態にする。
    for (const JsonValue& sourceObject : sourceObjects.array) {
        GameObject::Id id = 0;
        ReadIdValue(*sourceObject.Find("id"), id, error, "id");
        std::string name;
        bool active = true;
        ReadString(sourceObject, "name", name, error);
        ReadBool(sourceObject, "active", active, error);
        GameObject* gameObject = loadedScene.CreateGameObjectWithId(id, name);
        if (gameObject == nullptr) {
            resultMessage = "Scene load failed: invalid GameObject ID.";
            return false;
        }
        gameObject->SetActive(active);
        objectsById[id] = gameObject;
    }

    CameraRestoreState cameraState;
    // 2. Componentと設定値を復元する。構造検証済みなのでGPU生成だけを行う。
    for (size_t objectIndex = 0;
        objectIndex < sourceObjects.array.size(); ++objectIndex) {
        const JsonValue& sourceObject = sourceObjects.array[objectIndex];
        GameObject::Id id = 0;
        ReadIdValue(*sourceObject.Find("id"), id, error, "id");
        GameObject& gameObject = *objectsById[id];
        const JsonValue& components = *sourceObject.Find("components");
        for (const JsonValue& component : components.array) {
            if (!AddComponentFromJson(
                component,
                gameObject,
                assetManager_,
                graphics_,
                inputManager_,
                defaultTextureHandle_,
                defaultTextureGuid_,
                cameraState,
                warnings,
                error)) {
                resultMessage = "Scene load failed: " + error;
                return false;
            }
        }
    }

    // CameraComponent::AwakeがTransformを更新するため、Component生成後に保存値を戻す。
    for (const JsonValue& sourceObject : sourceObjects.array) {
        GameObject::Id id = 0;
        TransformData transform{};
        ReadIdValue(*sourceObject.Find("id"), id, error, "id");
        ReadTransform(*sourceObject.Find("transform"), transform, error);
        objectsById[id]->GetTransform().GetLocalTransform() = transform;
    }

    // 3. 全IDが揃ってから親子関係を復元する。
    for (const JsonValue& sourceObject : sourceObjects.array) {
        const JsonValue& parentValue = *sourceObject.Find("parentId");
        if (parentValue.type == JsonValue::Type::Null) {
            continue;
        }
        GameObject::Id id = 0;
        GameObject::Id parentId = 0;
        ReadIdValue(*sourceObject.Find("id"), id, error, "id");
        ReadIdValue(parentValue, parentId, error, "parentId");
        if (!objectsById[id]->SetParent(objectsById[parentId])) {
            resultMessage = "Scene load failed: hierarchy could not be restored.";
            return false;
        }
    }

    // 古いSceneのModelなどを破棄する前に、直前フレームまでのGPU使用を完了させる。
    if (graphics_ != nullptr) {
        graphics_->FlushGpu();
    }
    scene.ReplaceWith(std::move(loadedScene));

    if (applySceneSettings) {
        const JsonValue& lighting = *root.Find("lighting");
        LightingMode lightingMode = LightingMode::Current;
        float specularStrength = 1.0f;
        float specularShininess = 32.0f;
        if (const JsonValue* modeValue = lighting.Find("mode")) {
            ParseLightingMode(modeValue->string, lightingMode);
        }
        ReadFloat(lighting, "specularStrength", specularStrength, error);
        ReadFloat(lighting, "specularShininess", specularShininess, error);
        if (graphics_ != nullptr && graphics_->GetLightingManager() != nullptr) {
            LightingManager* lightingManager =
                graphics_->GetLightingManager();
            lightingManager->SetLightingMode(lightingMode);
            lightingManager->SetSpecularStrength(specularStrength);
            lightingManager->SetSpecularShininess(specularShininess);

            // 環境光の項目がない旧Sceneでは、起動時に設定したIBLをそのまま使う。
            bool environmentEnabled = lightingManager->IsEnvironmentEnabled();
            float environmentIntensity =
                lightingManager->GetEnvironmentIntensity();
            float environmentRotation =
                lightingManager->GetEnvironmentRotation();
            AssetGuid environmentTextureGuid =
                lightingManager->GetEnvironmentTextureGuid();
            if (lighting.Find("environmentEnabled") != nullptr) {
                ReadBool(
                    lighting,
                    "environmentEnabled",
                    environmentEnabled,
                    error);
            }
            if (lighting.Find("environmentIntensity") != nullptr) {
                ReadFloat(
                    lighting,
                    "environmentIntensity",
                    environmentIntensity,
                    error);
            }
            if (lighting.Find("environmentRotation") != nullptr) {
                ReadFloat(
                    lighting,
                    "environmentRotation",
                    environmentRotation,
                    error);
            }
            if (lighting.Find("environmentTextureGuid") != nullptr) {
                ReadString(
                    lighting,
                    "environmentTextureGuid",
                    environmentTextureGuid,
                    error);
                std::string textureError;
                const int environmentTextureHandle =
                    environmentTextureGuid.empty() || assetManager_ == nullptr
                    ? -1
                    : assetManager_->LoadTexture(
                        environmentTextureGuid, &textureError);
                lightingManager->SetEnvironmentTextureAsset(
                    environmentTextureHandle,
                    environmentTextureGuid);
                if (environmentTextureHandle < 0 && !textureError.empty()) {
                    AppendWarning(warnings, textureError);
                }
            }
            lightingManager->SetEnvironmentIntensity(environmentIntensity);
            lightingManager->SetEnvironmentRotation(environmentRotation);
            lightingManager->SetEnvironmentEnabled(environmentEnabled);
        }
        if (graphics_ != nullptr) {
            ToneMappingMode toneMappingMode =
                graphics_->GetToneMappingMode();
            float exposure = graphics_->GetExposure();
            if (const JsonValue* toneMapping =
                lighting.Find("toneMapping")) {
                ParseToneMappingMode(
                    toneMapping->string, toneMappingMode);
            }
            if (lighting.Find("exposure") != nullptr) {
                ReadFloat(lighting, "exposure", exposure, error);
            }
            graphics_->SetToneMappingMode(toneMappingMode);
            graphics_->SetExposure(exposure);
            AntiAliasingMode antiAliasingMode =
                graphics_->GetAntiAliasingMode();
            if (const JsonValue* antiAliasing =
                lighting.Find("antiAliasing")) {
                ParseAntiAliasingMode(
                    antiAliasing->string, antiAliasingMode);
            }
            bool ssaoEnabled = graphics_->IsSsaoEnabled();
            bool bloomEnabled = graphics_->IsBloomEnabled();
            float ssaoStrength = graphics_->GetSsaoStrength();
            float bloomIntensity = graphics_->GetBloomIntensity();
            float bloomThreshold = graphics_->GetBloomThreshold();
            if (lighting.Find("ssaoEnabled") != nullptr) {
                ReadBool(lighting, "ssaoEnabled", ssaoEnabled, error);
            }
            if (lighting.Find("ssaoStrength") != nullptr) {
                ReadFloat(lighting, "ssaoStrength", ssaoStrength, error);
            }
            if (lighting.Find("bloomEnabled") != nullptr) {
                ReadBool(lighting, "bloomEnabled", bloomEnabled, error);
            }
            if (lighting.Find("bloomIntensity") != nullptr) {
                ReadFloat(lighting, "bloomIntensity", bloomIntensity, error);
            }
            if (lighting.Find("bloomThreshold") != nullptr) {
                ReadFloat(lighting, "bloomThreshold", bloomThreshold, error);
            }
            graphics_->SetAntiAliasingMode(antiAliasingMode);
            graphics_->SetSsaoEnabled(ssaoEnabled);
            graphics_->SetSsaoStrength(ssaoStrength);
            graphics_->SetBloomEnabled(bloomEnabled);
            graphics_->SetBloomIntensity(bloomIntensity);
            graphics_->SetBloomThreshold(bloomThreshold);
        }
    }

    // 古いSceneのライトが登録解除された後なので、読み込んだライトの枠を確定する。
    for (const std::unique_ptr<GameObject>& gameObject : scene.GetGameObjects()) {
        for (const std::unique_ptr<Component>& component :
            gameObject->GetComponents()) {
            if (auto* light = dynamic_cast<LightComponent*>(component.get())) {
                light->ApplyLight();
            }
        }
    }

    resultMessage = "Scene restored from memory.";
    if (!warnings.empty()) {
        resultMessage += "\n" + warnings;
    }
    return true;
}

bool SceneSerializer::Load(
    Scene& scene,
    const std::string& filePath,
    std::string& resultMessage) const {
    if (filePath.empty()) {
        resultMessage = "Scene file path is empty.";
        return false;
    }

    std::ifstream input(MakePathFromUtf8(filePath), std::ios::binary);
    if (!input.is_open()) {
        resultMessage = "Could not open the Scene file: " + filePath;
        return false;
    }
    std::ostringstream text;
    text << input.rdbuf();
    if (!input.good() && !input.eof()) {
        resultMessage = "An error occurred while reading the Scene file.";
        return false;
    }

    if (!DeserializeFromString(scene, text.str(), resultMessage)) {
        return false;
    }

    // メモリ復元メッセージをファイル読み込み向けへ置き換え、警告部分は残す。
    const size_t warningPosition = resultMessage.find('\n');
    const std::string warnings = warningPosition != std::string::npos
        ? resultMessage.substr(warningPosition)
        : std::string{};
    resultMessage = "Scene loaded: " + filePath + warnings;
    return true;
}
