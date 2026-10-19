#include "VisualScriptGraph.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_set>

namespace {

using Pin = VisualScriptPinDefinition;
using Type = VisualScriptValueType;

const std::vector<Pin> kNoPins{};
const std::vector<Pin> kEventOutputs{ { "次", Type::Execution } };
const std::vector<Pin> kBooleanOutput{ { "値", Type::Boolean } };
const std::vector<Pin> kFloatOutput{ { "値", Type::Float } };
const std::vector<Pin> kVectorOutput{ { "値", Type::Vector3 } };
const std::vector<Pin> kGameObjectOutput{ { "GameObject", Type::GameObject } };
const std::vector<Pin> kCollisionOutputs{
    { "次", Type::Execution }, { "相手", Type::GameObject }
};
const std::vector<Pin> kBranchInputs{
    { "実行", Type::Execution }, { "条件", Type::Boolean }
};
const std::vector<Pin> kBranchOutputs{
    { "真", Type::Execution }, { "偽", Type::Execution }
};
const std::vector<Pin> kSequenceInputs{ { "実行", Type::Execution } };
const std::vector<Pin> kSequenceOutputs{
    { "次 1", Type::Execution }, { "次 2", Type::Execution }
};
const std::vector<Pin> kBooleanInputs{
    { "A", Type::Boolean }, { "B", Type::Boolean }
};
const std::vector<Pin> kFloatInputs{
    { "A", Type::Float }, { "B", Type::Float }
};
const std::vector<Pin> kMakeVectorInputs{
    { "X", Type::Float }, { "Y", Type::Float }, { "Z", Type::Float }
};
const std::vector<Pin> kActionVectorInputs{
    { "実行", Type::Execution }, { "ベクトル", Type::Vector3 }
};
const std::vector<Pin> kActionFloatInputs{
    { "実行", Type::Execution }, { "値", Type::Float }
};
const std::vector<Pin> kActionOutput{ { "次", Type::Execution } };
const std::vector<Pin> kSetBooleanInputs{
    { "実行", Type::Execution }, { "値", Type::Boolean }
};
const std::vector<Pin> kSetFloatInputs{
    { "実行", Type::Execution }, { "値", Type::Float }
};
const std::vector<Pin> kSetVectorInputs{
    { "実行", Type::Execution }, { "値", Type::Vector3 }
};
const std::vector<Pin> kTranslateGameObjectInputs{
    { "実行", Type::Execution },
    { "対象", Type::GameObject },
    { "ベクトル", Type::Vector3 }
};
const std::vector<Pin> kSetActiveInputs{
    { "実行", Type::Execution },
    { "対象", Type::GameObject },
    { "有効", Type::Boolean }
};

void WriteEscapedString(std::ostream& output, const std::string& value) {
    output.put('"');
    for (const char character : value) {
        if (character == '"' || character == '\\') {
            output.put('\\');
        }
        output.put(character);
    }
    output.put('"');
}

std::string UnescapeString(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    bool escaped = false;
    for (const char character : value) {
        if (escaped) {
            result.push_back(character);
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else {
            result.push_back(character);
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

    std::vector<std::string> objects;
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
                objects.push_back(
                    json.substr(objectStart, index - objectStart + 1));
                objectStart = std::string::npos;
            }
        } else if (character == ']' && depth == 0) {
            break;
        }
    }
    return objects;
}

bool ReadString(
    const std::string& object,
    const char* field,
    std::string& result) {
    const std::regex pattern(
        std::string("\"") + field + "\"\\s*:\\s*\"((?:\\\\.|[^\"])*)\"");
    std::smatch match;
    if (!std::regex_search(object, match, pattern)) {
        return false;
    }
    result = UnescapeString(match[1].str());
    return true;
}

bool ReadInt(const std::string& object, const char* field, int& result) {
    const std::regex pattern(
        std::string("\"") + field + "\"\\s*:\\s*(-?[0-9]+)");
    std::smatch match;
    if (!std::regex_search(object, match, pattern)) {
        return false;
    }
    try {
        result = std::stoi(match[1].str());
        return true;
    } catch (...) {
        return false;
    }
}

bool ReadFloat(const std::string& object, const char* field, float& result) {
    const std::regex pattern(
        std::string("\"") + field +
        "\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)");
    std::smatch match;
    if (!std::regex_search(object, match, pattern)) {
        return false;
    }
    try {
        result = std::stof(match[1].str());
        return std::isfinite(result);
    } catch (...) {
        return false;
    }
}

bool ReadBool(const std::string& object, const char* field, bool& result) {
    const std::regex pattern(
        std::string("\"") + field + "\"\\s*:\\s*(true|false)");
    std::smatch match;
    if (!std::regex_search(object, match, pattern)) {
        return false;
    }
    result = match[1].str() == "true";
    return true;
}

bool ReadVector3(
    const std::string& object,
    const char* field,
    Vector3& result) {
    const std::string number =
        "(-?[0-9]+(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)";
    const std::regex pattern(
        std::string("\"") + field + "\"\\s*:\\s*\\[\\s*" + number +
        "\\s*,\\s*" + number + "\\s*,\\s*" + number + "\\s*\\]");
    std::smatch match;
    if (!std::regex_search(object, match, pattern)) {
        return false;
    }
    try {
        result = {
            std::stof(match[1].str()),
            std::stof(match[2].str()),
            std::stof(match[3].str())
        };
        return std::isfinite(result.x) && std::isfinite(result.y) &&
            std::isfinite(result.z);
    } catch (...) {
        return false;
    }
}

} // namespace

void VisualScriptGraph::Clear() {
    nodes_.clear();
    links_.clear();
    variables_.clear();
    nextNodeId_ = 1;
    nextLinkId_ = 1;
}

void VisualScriptGraph::ResetToMovementSample() {
    Clear();
    const int updateId =
        AddNode(VisualScriptNodeType::EventUpdate, 40.0f, 80.0f).id;
    VisualScriptNode& key = AddNode(VisualScriptNodeType::KeyPressed, 40.0f, 330.0f);
    key.key = 0x57;
    const int keyId = key.id;
    const int branchId =
        AddNode(VisualScriptNodeType::Branch, 340.0f, 110.0f).id;
    VisualScriptNode& translate = AddNode(VisualScriptNodeType::Translate, 650.0f, 110.0f);
    translate.vectorValue = { 0.0f, 0.0f, 3.0f };
    translate.boolValue = true;
    const int translateId = translate.id;
    Connect(updateId, 0, branchId, 0);
    Connect(keyId, 0, branchId, 1);
    Connect(branchId, 0, translateId, 0);
}

VisualScriptNode& VisualScriptGraph::AddNode(
    VisualScriptNodeType type,
    float x,
    float y) {
    VisualScriptNode node{};
    node.id = nextNodeId_++;
    node.type = type;
    node.name = GetNodeDisplayName(type);
    node.x = x;
    node.y = y;
    if (type == VisualScriptNodeType::KeyTriggered) {
        node.key = 0x20;
    }
    if (type == VisualScriptNodeType::AddImpulse) {
        node.vectorValue = { 0.0f, 5.0f, 0.0f };
    }
    if (type == VisualScriptNodeType::GetBooleanVariable ||
        type == VisualScriptNodeType::SetBooleanVariable) {
        node.stringValue = "BoolVariable";
    } else if (type == VisualScriptNodeType::GetFloatVariable ||
        type == VisualScriptNodeType::SetFloatVariable) {
        node.stringValue = "FloatVariable";
    } else if (type == VisualScriptNodeType::GetVector3Variable ||
        type == VisualScriptNodeType::SetVector3Variable) {
        node.stringValue = "VectorVariable";
    }
    nodes_.push_back(std::move(node));
    return nodes_.back();
}

bool VisualScriptGraph::RemoveNode(int nodeId) {
    const auto node = std::find_if(
        nodes_.begin(), nodes_.end(),
        [nodeId](const VisualScriptNode& value) { return value.id == nodeId; });
    if (node == nodes_.end()) {
        return false;
    }
    nodes_.erase(node);
    std::erase_if(
        links_,
        [nodeId](const VisualScriptLink& link) {
            return link.sourceNodeId == nodeId || link.targetNodeId == nodeId;
        });
    return true;
}

bool VisualScriptGraph::Connect(
    int sourceNodeId,
    int sourcePinIndex,
    int targetNodeId,
    int targetPinIndex,
    std::string* error) {
    const VisualScriptNode* source = FindNode(sourceNodeId);
    const VisualScriptNode* target = FindNode(targetNodeId);
    if (source == nullptr || target == nullptr || sourceNodeId == targetNodeId) {
        if (error != nullptr) { *error = "接続先または接続元が正しくありません。"; }
        return false;
    }
    const auto& outputs = GetOutputPins(source->type);
    const auto& inputs = GetInputPins(target->type);
    if (sourcePinIndex < 0 || sourcePinIndex >= static_cast<int>(outputs.size()) ||
        targetPinIndex < 0 || targetPinIndex >= static_cast<int>(inputs.size())) {
        if (error != nullptr) { *error = "ピン番号が範囲外です。"; }
        return false;
    }
    if (outputs[sourcePinIndex].type != inputs[targetPinIndex].type) {
        if (error != nullptr) { *error = "異なる型のピンは接続できません。"; }
        return false;
    }
    if (WouldCreateCycle(sourceNodeId, targetNodeId)) {
        if (error != nullptr) { *error = "循環する接続は作成できません。"; }
        return false;
    }

    DisconnectInput(targetNodeId, targetPinIndex);
    if (outputs[sourcePinIndex].type == VisualScriptValueType::Execution) {
        std::erase_if(
            links_,
            [sourceNodeId, sourcePinIndex](const VisualScriptLink& link) {
                return link.sourceNodeId == sourceNodeId &&
                    link.sourcePinIndex == sourcePinIndex;
            });
    }
    links_.push_back({
        nextLinkId_++, sourceNodeId, sourcePinIndex,
        targetNodeId, targetPinIndex
    });
    return true;
}

bool VisualScriptGraph::DisconnectInput(int targetNodeId, int targetPinIndex) {
    const size_t previousSize = links_.size();
    std::erase_if(
        links_,
        [targetNodeId, targetPinIndex](const VisualScriptLink& link) {
            return link.targetNodeId == targetNodeId &&
                link.targetPinIndex == targetPinIndex;
        });
    return links_.size() != previousSize;
}

bool VisualScriptGraph::RemoveLink(int linkId) {
    const size_t previousSize = links_.size();
    std::erase_if(
        links_,
        [linkId](const VisualScriptLink& link) { return link.id == linkId; });
    return links_.size() != previousSize;
}

VisualScriptVariable& VisualScriptGraph::AddVariable(
    VisualScriptValueType type) {
    const char* prefix = type == VisualScriptValueType::Boolean
        ? "BoolVariable"
        : type == VisualScriptValueType::Vector3
        ? "VectorVariable"
        : "FloatVariable";
    std::string name = prefix;
    int suffix = 2;
    while (FindVariable(name) != nullptr) {
        name = std::string(prefix) + std::to_string(suffix++);
    }
    variables_.push_back({ name, type });
    return variables_.back();
}

bool VisualScriptGraph::RemoveVariable(size_t index) {
    if (index >= variables_.size()) {
        return false;
    }
    const std::string removedName = variables_[index].name;
    variables_.erase(variables_.begin() + static_cast<std::ptrdiff_t>(index));
    for (VisualScriptNode& node : nodes_) {
        if (node.stringValue == removedName) {
            node.stringValue.clear();
        }
    }
    return true;
}

VisualScriptVariable* VisualScriptGraph::FindVariable(
    const std::string& name) {
    const auto iterator = std::find_if(
        variables_.begin(), variables_.end(),
        [&name](const VisualScriptVariable& variable) {
            return variable.name == name;
        });
    return iterator == variables_.end() ? nullptr : &*iterator;
}

const VisualScriptVariable* VisualScriptGraph::FindVariable(
    const std::string& name) const {
    const auto iterator = std::find_if(
        variables_.begin(), variables_.end(),
        [&name](const VisualScriptVariable& variable) {
            return variable.name == name;
        });
    return iterator == variables_.end() ? nullptr : &*iterator;
}

VisualScriptNode* VisualScriptGraph::FindNode(int nodeId) {
    const auto iterator = std::find_if(
        nodes_.begin(), nodes_.end(),
        [nodeId](const VisualScriptNode& node) { return node.id == nodeId; });
    return iterator == nodes_.end() ? nullptr : &*iterator;
}

const VisualScriptNode* VisualScriptGraph::FindNode(int nodeId) const {
    const auto iterator = std::find_if(
        nodes_.begin(), nodes_.end(),
        [nodeId](const VisualScriptNode& node) { return node.id == nodeId; });
    return iterator == nodes_.end() ? nullptr : &*iterator;
}

const VisualScriptLink* VisualScriptGraph::FindInputLink(
    int targetNodeId,
    int targetPinIndex) const {
    const auto iterator = std::find_if(
        links_.begin(), links_.end(),
        [targetNodeId, targetPinIndex](const VisualScriptLink& link) {
            return link.targetNodeId == targetNodeId &&
                link.targetPinIndex == targetPinIndex;
        });
    return iterator == links_.end() ? nullptr : &*iterator;
}

bool VisualScriptGraph::WouldCreateCycle(
    int sourceNodeId,
    int targetNodeId) const {
    std::vector<int> pending{ targetNodeId };
    std::unordered_set<int> visited;
    while (!pending.empty()) {
        const int current = pending.back();
        pending.pop_back();
        if (current == sourceNodeId) {
            return true;
        }
        if (!visited.insert(current).second) {
            continue;
        }
        for (const VisualScriptLink& link : links_) {
            if (link.sourceNodeId == current) {
                pending.push_back(link.targetNodeId);
            }
        }
    }
    return false;
}

const char* VisualScriptGraph::GetNodeTypeName(VisualScriptNodeType type) {
    switch (type) {
    case VisualScriptNodeType::EventStart: return "EventStart";
    case VisualScriptNodeType::EventUpdate: return "EventUpdate";
    case VisualScriptNodeType::KeyPressed: return "KeyPressed";
    case VisualScriptNodeType::KeyTriggered: return "KeyTriggered";
    case VisualScriptNodeType::Branch: return "Branch";
    case VisualScriptNodeType::Sequence: return "Sequence";
    case VisualScriptNodeType::BooleanAnd: return "BooleanAnd";
    case VisualScriptNodeType::FloatValue: return "FloatValue";
    case VisualScriptNodeType::Vector3Value: return "Vector3Value";
    case VisualScriptNodeType::DeltaTime: return "DeltaTime";
    case VisualScriptNodeType::AddFloat: return "AddFloat";
    case VisualScriptNodeType::MultiplyFloat: return "MultiplyFloat";
    case VisualScriptNodeType::MakeVector3: return "MakeVector3";
    case VisualScriptNodeType::Translate: return "Translate";
    case VisualScriptNodeType::IsGrounded: return "IsGrounded";
    case VisualScriptNodeType::AddImpulse: return "AddImpulse";
    case VisualScriptNodeType::PrintFloat: return "PrintFloat";
    case VisualScriptNodeType::EventCollisionEnter: return "EventCollisionEnter";
    case VisualScriptNodeType::EventCollisionStay: return "EventCollisionStay";
    case VisualScriptNodeType::EventCollisionExit: return "EventCollisionExit";
    case VisualScriptNodeType::EqualFloat: return "EqualFloat";
    case VisualScriptNodeType::LessFloat: return "LessFloat";
    case VisualScriptNodeType::GreaterFloat: return "GreaterFloat";
    case VisualScriptNodeType::GetBooleanVariable: return "GetBooleanVariable";
    case VisualScriptNodeType::SetBooleanVariable: return "SetBooleanVariable";
    case VisualScriptNodeType::GetFloatVariable: return "GetFloatVariable";
    case VisualScriptNodeType::SetFloatVariable: return "SetFloatVariable";
    case VisualScriptNodeType::GetVector3Variable: return "GetVector3Variable";
    case VisualScriptNodeType::SetVector3Variable: return "SetVector3Variable";
    case VisualScriptNodeType::FindGameObject: return "FindGameObject";
    case VisualScriptNodeType::TranslateGameObject: return "TranslateGameObject";
    case VisualScriptNodeType::SetGameObjectActive: return "SetGameObjectActive";
    }
    return "Unknown";
}

const char* VisualScriptGraph::GetNodeDisplayName(VisualScriptNodeType type) {
    switch (type) {
    case VisualScriptNodeType::EventStart: return "開始したとき";
    case VisualScriptNodeType::EventUpdate: return "毎フレーム";
    case VisualScriptNodeType::KeyPressed: return "キーを押している";
    case VisualScriptNodeType::KeyTriggered: return "キーを押した瞬間";
    case VisualScriptNodeType::Branch: return "もし～なら";
    case VisualScriptNodeType::Sequence: return "順番に実行";
    case VisualScriptNodeType::BooleanAnd: return "両方とも真";
    case VisualScriptNodeType::FloatValue: return "数値";
    case VisualScriptNodeType::Vector3Value: return "ベクトル";
    case VisualScriptNodeType::DeltaTime: return "デルタタイム";
    case VisualScriptNodeType::AddFloat: return "数値を足す";
    case VisualScriptNodeType::MultiplyFloat: return "数値を掛ける";
    case VisualScriptNodeType::MakeVector3: return "ベクトルを作る";
    case VisualScriptNodeType::Translate: return "移動する";
    case VisualScriptNodeType::IsGrounded: return "地面にいる";
    case VisualScriptNodeType::AddImpulse: return "瞬間的な力を加える";
    case VisualScriptNodeType::PrintFloat: return "数値を出力";
    case VisualScriptNodeType::EventCollisionEnter: return "衝突したとき";
    case VisualScriptNodeType::EventCollisionStay: return "衝突中";
    case VisualScriptNodeType::EventCollisionExit: return "離れたとき";
    case VisualScriptNodeType::EqualFloat: return "数値が等しい";
    case VisualScriptNodeType::LessFloat: return "A < B";
    case VisualScriptNodeType::GreaterFloat: return "A > B";
    case VisualScriptNodeType::GetBooleanVariable: return "bool変数を取得";
    case VisualScriptNodeType::SetBooleanVariable: return "bool変数を設定";
    case VisualScriptNodeType::GetFloatVariable: return "float変数を取得";
    case VisualScriptNodeType::SetFloatVariable: return "float変数を設定";
    case VisualScriptNodeType::GetVector3Variable: return "Vector3変数を取得";
    case VisualScriptNodeType::SetVector3Variable: return "Vector3変数を設定";
    case VisualScriptNodeType::FindGameObject: return "GameObjectを検索";
    case VisualScriptNodeType::TranslateGameObject: return "対象を移動する";
    case VisualScriptNodeType::SetGameObjectActive: return "有効状態を設定";
    }
    return "不明なノード";
}

bool VisualScriptGraph::TryParseNodeType(
    const std::string& text,
    VisualScriptNodeType& type) {
    for (int value = static_cast<int>(VisualScriptNodeType::EventStart);
        value <= static_cast<int>(VisualScriptNodeType::SetGameObjectActive);
        ++value) {
        const auto candidate = static_cast<VisualScriptNodeType>(value);
        if (text == GetNodeTypeName(candidate)) {
            type = candidate;
            return true;
        }
    }
    return false;
}

const std::vector<VisualScriptPinDefinition>& VisualScriptGraph::GetInputPins(
    VisualScriptNodeType type) {
    switch (type) {
    case VisualScriptNodeType::Branch: return kBranchInputs;
    case VisualScriptNodeType::Sequence: return kSequenceInputs;
    case VisualScriptNodeType::BooleanAnd: return kBooleanInputs;
    case VisualScriptNodeType::AddFloat:
    case VisualScriptNodeType::MultiplyFloat: return kFloatInputs;
    case VisualScriptNodeType::MakeVector3: return kMakeVectorInputs;
    case VisualScriptNodeType::Translate:
    case VisualScriptNodeType::AddImpulse: return kActionVectorInputs;
    case VisualScriptNodeType::PrintFloat: return kActionFloatInputs;
    case VisualScriptNodeType::EqualFloat:
    case VisualScriptNodeType::LessFloat:
    case VisualScriptNodeType::GreaterFloat: return kFloatInputs;
    case VisualScriptNodeType::SetBooleanVariable: return kSetBooleanInputs;
    case VisualScriptNodeType::SetFloatVariable: return kSetFloatInputs;
    case VisualScriptNodeType::SetVector3Variable: return kSetVectorInputs;
    case VisualScriptNodeType::TranslateGameObject:
        return kTranslateGameObjectInputs;
    case VisualScriptNodeType::SetGameObjectActive: return kSetActiveInputs;
    default: return kNoPins;
    }
}

const std::vector<VisualScriptPinDefinition>& VisualScriptGraph::GetOutputPins(
    VisualScriptNodeType type) {
    switch (type) {
    case VisualScriptNodeType::EventStart:
    case VisualScriptNodeType::EventUpdate: return kEventOutputs;
    case VisualScriptNodeType::EventCollisionEnter:
    case VisualScriptNodeType::EventCollisionStay:
    case VisualScriptNodeType::EventCollisionExit: return kCollisionOutputs;
    case VisualScriptNodeType::KeyPressed:
    case VisualScriptNodeType::KeyTriggered:
    case VisualScriptNodeType::BooleanAnd:
    case VisualScriptNodeType::IsGrounded:
    case VisualScriptNodeType::EqualFloat:
    case VisualScriptNodeType::LessFloat:
    case VisualScriptNodeType::GreaterFloat:
    case VisualScriptNodeType::GetBooleanVariable: return kBooleanOutput;
    case VisualScriptNodeType::FloatValue:
    case VisualScriptNodeType::DeltaTime:
    case VisualScriptNodeType::AddFloat:
    case VisualScriptNodeType::MultiplyFloat: return kFloatOutput;
    case VisualScriptNodeType::GetFloatVariable: return kFloatOutput;
    case VisualScriptNodeType::Vector3Value:
    case VisualScriptNodeType::MakeVector3:
    case VisualScriptNodeType::GetVector3Variable: return kVectorOutput;
    case VisualScriptNodeType::FindGameObject: return kGameObjectOutput;
    case VisualScriptNodeType::Branch: return kBranchOutputs;
    case VisualScriptNodeType::Sequence: return kSequenceOutputs;
    case VisualScriptNodeType::Translate:
    case VisualScriptNodeType::AddImpulse:
    case VisualScriptNodeType::PrintFloat:
    case VisualScriptNodeType::SetBooleanVariable:
    case VisualScriptNodeType::SetFloatVariable:
    case VisualScriptNodeType::SetVector3Variable:
    case VisualScriptNodeType::TranslateGameObject:
    case VisualScriptNodeType::SetGameObjectActive: return kActionOutput;
    }
    return kNoPins;
}

const char* VisualScriptGraph::GetValueTypeName(VisualScriptValueType type) {
    switch (type) {
    case VisualScriptValueType::Boolean: return "Boolean";
    case VisualScriptValueType::Float: return "Float";
    case VisualScriptValueType::Vector3: return "Vector3";
    case VisualScriptValueType::GameObject: return "GameObject";
    case VisualScriptValueType::Execution: return "Execution";
    }
    return "Unknown";
}

bool VisualScriptGraph::TryParseValueType(
    const std::string& text,
    VisualScriptValueType& type) {
    if (text == "Boolean") { type = VisualScriptValueType::Boolean; return true; }
    if (text == "Float") { type = VisualScriptValueType::Float; return true; }
    if (text == "Vector3") { type = VisualScriptValueType::Vector3; return true; }
    if (text == "GameObject") { type = VisualScriptValueType::GameObject; return true; }
    if (text == "Execution") { type = VisualScriptValueType::Execution; return true; }
    return false;
}

bool VisualScriptGraph::Save(
    const std::string& filePath,
    std::string& error) const {
    if (filePath.empty()) {
        error = "保存先が空です。";
        return false;
    }
    const std::filesystem::path path(filePath);
    std::error_code filesystemError;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), filesystemError);
        if (filesystemError) {
            error = "保存先フォルダーを作成できません: " +
                filesystemError.message();
            return false;
        }
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        error = "Visual Scriptを開けません: " + path.string();
        return false;
    }
    output << std::setprecision(9);
    output << "{\n  \"formatVersion\": " << kFormatVersion << ",\n";
    output << "  \"variables\": [\n";
    for (size_t index = 0; index < variables_.size(); ++index) {
        const VisualScriptVariable& variable = variables_[index];
        output << "    { \"name\": ";
        WriteEscapedString(output, variable.name);
        output << ", \"type\": ";
        WriteEscapedString(output, GetValueTypeName(variable.type));
        output << ", \"boolValue\": "
            << (variable.boolValue ? "true" : "false")
            << ", \"floatValue\": " << variable.floatValue
            << ", \"vectorValue\": [" << variable.vectorValue.x << ", "
            << variable.vectorValue.y << ", " << variable.vectorValue.z
            << "] }";
        output << (index + 1 < variables_.size() ? ",\n" : "\n");
    }
    output << "  ],\n";
    output << "  \"nodes\": [\n";
    for (size_t index = 0; index < nodes_.size(); ++index) {
        const VisualScriptNode& node = nodes_[index];
        output << "    { \"id\": " << node.id << ", \"type\": ";
        WriteEscapedString(output, GetNodeTypeName(node.type));
        output << ", \"name\": ";
        WriteEscapedString(output, node.name);
        output << ", \"x\": " << node.x << ", \"y\": " << node.y
            << ", \"floatValue\": " << node.floatValue
            << ", \"floatValueB\": " << node.floatValueB
            << ", \"vectorValue\": [" << node.vectorValue.x << ", "
            << node.vectorValue.y << ", " << node.vectorValue.z << ']'
            << ", \"stringValue\": ";
        WriteEscapedString(output, node.stringValue);
        output
            << ", \"key\": " << node.key
            << ", \"boolValue\": "
            << (node.boolValue ? "true" : "false") << " }";
        output << (index + 1 < nodes_.size() ? ",\n" : "\n");
    }
    output << "  ],\n  \"links\": [\n";
    for (size_t index = 0; index < links_.size(); ++index) {
        const VisualScriptLink& link = links_[index];
        output << "    { \"id\": " << link.id
            << ", \"sourceNode\": " << link.sourceNodeId
            << ", \"sourcePin\": " << link.sourcePinIndex
            << ", \"targetNode\": " << link.targetNodeId
            << ", \"targetPin\": " << link.targetPinIndex << " }";
        output << (index + 1 < links_.size() ? ",\n" : "\n");
    }
    output << "  ]\n}\n";
    if (!output.good()) {
        error = "Visual Scriptの書き込みに失敗しました。";
        return false;
    }
    error.clear();
    return true;
}

bool VisualScriptGraph::Load(
    const std::string& filePath,
    std::string& error) {
    std::ifstream input(filePath, std::ios::binary);
    if (!input.is_open()) {
        error = "Visual Scriptを開けません: " + filePath;
        return false;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    const std::string json = stream.str();
    int version = 0;
    if (!ReadInt(json, "formatVersion", version) ||
        version < 1 || version > kFormatVersion) {
        error = "対応していないVisual Script形式です。";
        return false;
    }

    VisualScriptGraph loaded;
    std::set<std::string> variableNames;
    if (version >= 2) {
        for (const std::string& object :
            ExtractObjectArray(json, "variables")) {
            VisualScriptVariable variable{};
            std::string typeName;
            if (!ReadString(object, "name", variable.name) ||
                variable.name.empty() ||
                !variableNames.insert(variable.name).second ||
                !ReadString(object, "type", typeName) ||
                !TryParseValueType(typeName, variable.type) ||
                (variable.type != VisualScriptValueType::Boolean &&
                 variable.type != VisualScriptValueType::Float &&
                 variable.type != VisualScriptValueType::Vector3) ||
                !ReadBool(object, "boolValue", variable.boolValue) ||
                !ReadFloat(object, "floatValue", variable.floatValue) ||
                !ReadVector3(
                    object, "vectorValue", variable.vectorValue)) {
                error = "Visual Scriptの変数データが壊れています。";
                return false;
            }
            loaded.variables_.push_back(std::move(variable));
        }
    }
    std::set<int> nodeIds;
    for (const std::string& object : ExtractObjectArray(json, "nodes")) {
        VisualScriptNode node{};
        std::string typeName;
        if (!ReadInt(object, "id", node.id) || node.id <= 0 ||
            !nodeIds.insert(node.id).second ||
            !ReadString(object, "type", typeName) ||
            !TryParseNodeType(typeName, node.type) ||
            !ReadString(object, "name", node.name) ||
            !ReadFloat(object, "x", node.x) ||
            !ReadFloat(object, "y", node.y) ||
            !ReadFloat(object, "floatValue", node.floatValue) ||
            !ReadVector3(object, "vectorValue", node.vectorValue) ||
            !ReadInt(object, "key", node.key) ||
            !ReadBool(object, "boolValue", node.boolValue)) {
            error = "Visual Scriptのノードデータが壊れています。";
            return false;
        }
        node.floatValueB = node.floatValue;
        if (version >= 2) {
            if (!ReadFloat(object, "floatValueB", node.floatValueB) ||
                !ReadString(object, "stringValue", node.stringValue)) {
                error = "Visual Scriptの追加ノードデータが壊れています。";
                return false;
            }
        }
        if (node.key < 0 || node.key > 255) {
            error = "キー番号は0～255の範囲で指定してください。";
            return false;
        }
        loaded.nodes_.push_back(std::move(node));
        loaded.nextNodeId_ = (std::max)(loaded.nextNodeId_, loaded.nodes_.back().id + 1);
    }
    if (loaded.nodes_.empty()) {
        error = "Visual Scriptにノードがありません。";
        return false;
    }

    for (const std::string& object : ExtractObjectArray(json, "links")) {
        int storedId = 0;
        int sourceNode = 0;
        int sourcePin = 0;
        int targetNode = 0;
        int targetPin = 0;
        if (!ReadInt(object, "id", storedId) ||
            !ReadInt(object, "sourceNode", sourceNode) ||
            !ReadInt(object, "sourcePin", sourcePin) ||
            !ReadInt(object, "targetNode", targetNode) ||
            !ReadInt(object, "targetPin", targetPin)) {
            error = "Visual Scriptのリンクデータが壊れています。";
            return false;
        }
        std::string connectError;
        if (!loaded.Connect(
            sourceNode, sourcePin, targetNode, targetPin, &connectError)) {
            error = "Visual Scriptの接続が不正です: " + connectError;
            return false;
        }
        loaded.links_.back().id = storedId;
        loaded.nextLinkId_ = (std::max)(loaded.nextLinkId_, storedId + 1);
    }
    *this = std::move(loaded);
    error.clear();
    return true;
}
