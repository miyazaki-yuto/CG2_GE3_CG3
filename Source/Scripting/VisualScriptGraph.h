#pragma once

#include "Vector3.h"

#include <cstdint>
#include <string>
#include <vector>

enum class VisualScriptValueType {
    Execution,
    Boolean,
    Float,
    Vector3,
    GameObject
};

enum class VisualScriptNodeType {
    EventStart,
    EventUpdate,
    KeyPressed,
    KeyTriggered,
    Branch,
    Sequence,
    BooleanAnd,
    FloatValue,
    Vector3Value,
    DeltaTime,
    AddFloat,
    MultiplyFloat,
    MakeVector3,
    Translate,
    IsGrounded,
    AddImpulse,
    PrintFloat,
    EventCollisionEnter,
    EventCollisionStay,
    EventCollisionExit,
    EqualFloat,
    LessFloat,
    GreaterFloat,
    GetBooleanVariable,
    SetBooleanVariable,
    GetFloatVariable,
    SetFloatVariable,
    GetVector3Variable,
    SetVector3Variable,
    FindGameObject,
    TranslateGameObject,
    SetGameObjectActive
};

struct VisualScriptVariable {
    std::string name;
    VisualScriptValueType type = VisualScriptValueType::Float;
    bool boolValue = false;
    float floatValue = 0.0f;
    Vector3 vectorValue{};
};

struct VisualScriptPinDefinition {
    const char* name = "";
    VisualScriptValueType type = VisualScriptValueType::Execution;
};

struct VisualScriptNode {
    int id = 0;
    VisualScriptNodeType type = VisualScriptNodeType::EventUpdate;
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
    float floatValue = 0.0f;
    float floatValueB = 0.0f;
    Vector3 vectorValue{};
    std::string stringValue;
    int key = 0x57;
    bool boolValue = true;
};

struct VisualScriptLink {
    int id = 0;
    int sourceNodeId = 0;
    int sourcePinIndex = 0;
    int targetNodeId = 0;
    int targetPinIndex = 0;
};

class VisualScriptGraph {
public:
    static constexpr int kFormatVersion = 2;

    void ResetToMovementSample();
    void Clear();

    VisualScriptNode& AddNode(
        VisualScriptNodeType type,
        float x = 80.0f,
        float y = 80.0f);
    bool RemoveNode(int nodeId);
    bool Connect(
        int sourceNodeId,
        int sourcePinIndex,
        int targetNodeId,
        int targetPinIndex,
        std::string* error = nullptr);
    bool DisconnectInput(int targetNodeId, int targetPinIndex);
    bool RemoveLink(int linkId);
    VisualScriptVariable& AddVariable(VisualScriptValueType type);
    bool RemoveVariable(size_t index);
    VisualScriptVariable* FindVariable(const std::string& name);
    const VisualScriptVariable* FindVariable(const std::string& name) const;

    VisualScriptNode* FindNode(int nodeId);
    const VisualScriptNode* FindNode(int nodeId) const;
    const VisualScriptLink* FindInputLink(
        int targetNodeId,
        int targetPinIndex) const;

    const std::vector<VisualScriptNode>& GetNodes() const { return nodes_; }
    std::vector<VisualScriptNode>& GetNodes() { return nodes_; }
    const std::vector<VisualScriptLink>& GetLinks() const { return links_; }
    const std::vector<VisualScriptVariable>& GetVariables() const {
        return variables_;
    }
    std::vector<VisualScriptVariable>& GetVariables() { return variables_; }

    bool Save(const std::string& filePath, std::string& error) const;
    bool Load(const std::string& filePath, std::string& error);

    static const char* GetNodeTypeName(VisualScriptNodeType type);
    static const char* GetNodeDisplayName(VisualScriptNodeType type);
    static bool TryParseNodeType(
        const std::string& text,
        VisualScriptNodeType& type);
    static const std::vector<VisualScriptPinDefinition>& GetInputPins(
        VisualScriptNodeType type);
    static const std::vector<VisualScriptPinDefinition>& GetOutputPins(
        VisualScriptNodeType type);
    static const char* GetValueTypeName(VisualScriptValueType type);
    static bool TryParseValueType(
        const std::string& text,
        VisualScriptValueType& type);

private:
    bool WouldCreateCycle(int sourceNodeId, int targetNodeId) const;

    std::vector<VisualScriptNode> nodes_;
    std::vector<VisualScriptLink> links_;
    std::vector<VisualScriptVariable> variables_;
    int nextNodeId_ = 1;
    int nextLinkId_ = 1;
};
