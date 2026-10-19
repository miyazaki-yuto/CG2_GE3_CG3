#pragma once

#include "VisualScriptGraph.h"

#include <string>
#include <unordered_map>

class GameObject;
class InputManager;

class VisualScriptRuntime {
public:
    static constexpr int kMaximumNodesPerEvent = 1000;

    bool ExecuteEvent(
        const VisualScriptGraph& graph,
        VisualScriptNodeType eventType,
        GameObject& owner,
        InputManager* inputManager,
        float deltaTime,
        std::string& error,
        GameObject* eventGameObject = nullptr);
    void ResetVariables(const VisualScriptGraph& graph);

    int GetLastExecutionCount() const { return lastExecutionCount_; }

    struct Value {
        VisualScriptValueType type = VisualScriptValueType::Float;
        bool boolean = false;
        float number = 0.0f;
        Vector3 vector{};
        GameObject* gameObject = nullptr;
    };

private:

    bool ExecuteOutput(
        const VisualScriptGraph& graph,
        int sourceNodeId,
        int sourcePinIndex,
        GameObject& owner,
        InputManager* inputManager,
        float deltaTime,
        int& budget,
        std::string& error);
    bool ExecuteNode(
        const VisualScriptGraph& graph,
        const VisualScriptNode& node,
        GameObject& owner,
        InputManager* inputManager,
        float deltaTime,
        int& budget,
        std::string& error);
    bool ReadInput(
        const VisualScriptGraph& graph,
        const VisualScriptNode& node,
        int inputPinIndex,
        const Value& fallback,
        GameObject& owner,
        InputManager* inputManager,
        float deltaTime,
        int& budget,
        int depth,
        Value& result,
        std::string& error);
    bool EvaluateOutput(
        const VisualScriptGraph& graph,
        const VisualScriptNode& node,
        int outputPinIndex,
        GameObject& owner,
        InputManager* inputManager,
        float deltaTime,
        int& budget,
        int depth,
        Value& result,
        std::string& error);

    int lastExecutionCount_ = 0;
    GameObject* eventGameObject_ = nullptr;
    std::unordered_map<std::string, Value> variables_;
};
