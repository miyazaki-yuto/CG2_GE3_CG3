#include "VisualScriptRuntime.h"

#include "GameObject.h"
#include "InputManager.h"
#include "RigidbodyComponent.h"
#include "Scene.h"
#include "TransformComponent.h"

#include <cmath>
#include <iostream>

namespace {

VisualScriptRuntime::Value MakeBoolean(bool value) {
    VisualScriptRuntime::Value result{};
    result.type = VisualScriptValueType::Boolean;
    result.boolean = value;
    return result;
}

VisualScriptRuntime::Value MakeFloat(float value) {
    VisualScriptRuntime::Value result{};
    result.type = VisualScriptValueType::Float;
    result.number = value;
    return result;
}

VisualScriptRuntime::Value MakeVector(const Vector3& value) {
    VisualScriptRuntime::Value result{};
    result.type = VisualScriptValueType::Vector3;
    result.vector = value;
    return result;
}

VisualScriptRuntime::Value MakeGameObject(GameObject* value) {
    VisualScriptRuntime::Value result{};
    result.type = VisualScriptValueType::GameObject;
    result.gameObject = value;
    return result;
}

} // namespace

bool VisualScriptRuntime::ExecuteEvent(
    const VisualScriptGraph& graph,
    VisualScriptNodeType eventType,
    GameObject& owner,
    InputManager* inputManager,
    float deltaTime,
    std::string& error,
    GameObject* eventGameObject) {
    int budget = kMaximumNodesPerEvent;
    error.clear();
    GameObject* previousEventGameObject = eventGameObject_;
    eventGameObject_ = eventGameObject;
    for (const VisualScriptNode& node : graph.GetNodes()) {
        if (node.type != eventType) {
            continue;
        }
        if (--budget < 0) {
            error = "1イベントの実行上限（1000ノード）を超えました。";
            break;
        }
        if (!ExecuteOutput(
            graph, node.id, 0, owner, inputManager,
            deltaTime, budget, error)) {
            break;
        }
    }
    eventGameObject_ = previousEventGameObject;
    lastExecutionCount_ = kMaximumNodesPerEvent - budget;
    return error.empty();
}

void VisualScriptRuntime::ResetVariables(const VisualScriptGraph& graph) {
    variables_.clear();
    for (const VisualScriptVariable& variable : graph.GetVariables()) {
        switch (variable.type) {
        case VisualScriptValueType::Boolean:
            variables_[variable.name] = MakeBoolean(variable.boolValue);
            break;
        case VisualScriptValueType::Float:
            variables_[variable.name] = MakeFloat(variable.floatValue);
            break;
        case VisualScriptValueType::Vector3:
            variables_[variable.name] = MakeVector(variable.vectorValue);
            break;
        default:
            break;
        }
    }
}

bool VisualScriptRuntime::ExecuteOutput(
    const VisualScriptGraph& graph,
    int sourceNodeId,
    int sourcePinIndex,
    GameObject& owner,
    InputManager* inputManager,
    float deltaTime,
    int& budget,
    std::string& error) {
    for (const VisualScriptLink& link : graph.GetLinks()) {
        if (link.sourceNodeId != sourceNodeId ||
            link.sourcePinIndex != sourcePinIndex) {
            continue;
        }
        const VisualScriptNode* target = graph.FindNode(link.targetNodeId);
        if (target == nullptr) {
            error = "実行リンクの接続先が見つかりません。";
            return false;
        }
        if (--budget < 0) {
            error = "1イベントの実行上限（1000ノード）を超えました。";
            return false;
        }
        return ExecuteNode(
            graph, *target, owner, inputManager, deltaTime, budget, error);
    }
    return true;
}

bool VisualScriptRuntime::ExecuteNode(
    const VisualScriptGraph& graph,
    const VisualScriptNode& node,
    GameObject& owner,
    InputManager* inputManager,
    float deltaTime,
    int& budget,
    std::string& error) {
    switch (node.type) {
    case VisualScriptNodeType::Branch: {
        Value condition;
        if (!ReadInput(
            graph, node, 1, MakeBoolean(false), owner, inputManager,
            deltaTime, budget, 0, condition, error)) {
            return false;
        }
        return ExecuteOutput(
            graph, node.id, condition.boolean ? 0 : 1,
            owner, inputManager, deltaTime, budget, error);
    }
    case VisualScriptNodeType::Sequence:
        return ExecuteOutput(
            graph, node.id, 0, owner, inputManager, deltaTime, budget, error) &&
            ExecuteOutput(
                graph, node.id, 1, owner, inputManager,
                deltaTime, budget, error);
    case VisualScriptNodeType::Translate: {
        Value movement;
        if (!ReadInput(
            graph, node, 1, MakeVector(node.vectorValue), owner, inputManager,
            deltaTime, budget, 0, movement, error)) {
            return false;
        }
        if (node.boolValue) {
            movement.vector = movement.vector * deltaTime;
        }
        owner.GetTransform().Translate(movement.vector);
        return ExecuteOutput(
            graph, node.id, 0, owner, inputManager, deltaTime, budget, error);
    }
    case VisualScriptNodeType::AddImpulse: {
        Value impulse;
        if (!ReadInput(
            graph, node, 1, MakeVector(node.vectorValue), owner, inputManager,
            deltaTime, budget, 0, impulse, error)) {
            return false;
        }
        RigidbodyComponent* rigidbody = owner.GetComponent<RigidbodyComponent>();
        if (rigidbody == nullptr) {
            error = "「瞬間的な力を加える」にはRigidbodyが必要です。";
            return false;
        }
        rigidbody->AddImpulse(impulse.vector);
        return ExecuteOutput(
            graph, node.id, 0, owner, inputManager, deltaTime, budget, error);
    }
    case VisualScriptNodeType::PrintFloat: {
        Value value;
        if (!ReadInput(
            graph, node, 1, MakeFloat(node.floatValue), owner, inputManager,
            deltaTime, budget, 0, value, error)) {
            return false;
        }
        std::cout << "[VisualScript] " << owner.GetName()
            << ": " << value.number << '\n';
        return ExecuteOutput(
            graph, node.id, 0, owner, inputManager, deltaTime, budget, error);
    }
    case VisualScriptNodeType::SetBooleanVariable:
    case VisualScriptNodeType::SetFloatVariable:
    case VisualScriptNodeType::SetVector3Variable: {
        const VisualScriptVariable* definition =
            graph.FindVariable(node.stringValue);
        if (definition == nullptr) {
            error = "設定する変数が見つかりません: " + node.stringValue;
            return false;
        }
        const VisualScriptValueType expectedType =
            node.type == VisualScriptNodeType::SetBooleanVariable
            ? VisualScriptValueType::Boolean
            : node.type == VisualScriptNodeType::SetFloatVariable
            ? VisualScriptValueType::Float
            : VisualScriptValueType::Vector3;
        if (definition->type != expectedType) {
            error = "変数と設定ノードの型が一致しません: " + node.stringValue;
            return false;
        }
        Value fallback;
        if (node.type == VisualScriptNodeType::SetBooleanVariable) {
            fallback = MakeBoolean(node.boolValue);
        } else if (node.type == VisualScriptNodeType::SetFloatVariable) {
            fallback = MakeFloat(node.floatValue);
        } else {
            fallback = MakeVector(node.vectorValue);
        }
        Value value;
        if (!ReadInput(
            graph, node, 1, fallback, owner, inputManager,
            deltaTime, budget, 0, value, error)) {
            return false;
        }
        variables_[node.stringValue] = value;
        return ExecuteOutput(
            graph, node.id, 0, owner, inputManager, deltaTime, budget, error);
    }
    case VisualScriptNodeType::TranslateGameObject: {
        Value target;
        Value movement;
        if (!ReadInput(
                graph, node, 1, MakeGameObject(&owner), owner, inputManager,
                deltaTime, budget, 0, target, error) ||
            !ReadInput(
                graph, node, 2, MakeVector(node.vectorValue), owner,
                inputManager, deltaTime, budget, 0, movement, error)) {
            return false;
        }
        if (target.gameObject == nullptr) {
            error = "移動対象のGameObjectが見つかりません。";
            return false;
        }
        if (node.boolValue) {
            movement.vector = movement.vector * deltaTime;
        }
        target.gameObject->GetTransform().Translate(movement.vector);
        return ExecuteOutput(
            graph, node.id, 0, owner, inputManager, deltaTime, budget, error);
    }
    case VisualScriptNodeType::SetGameObjectActive: {
        Value target;
        Value active;
        if (!ReadInput(
                graph, node, 1, MakeGameObject(&owner), owner, inputManager,
                deltaTime, budget, 0, target, error) ||
            !ReadInput(
                graph, node, 2, MakeBoolean(node.boolValue), owner,
                inputManager, deltaTime, budget, 0, active, error)) {
            return false;
        }
        if (target.gameObject == nullptr) {
            error = "有効状態を変えるGameObjectが見つかりません。";
            return false;
        }
        target.gameObject->SetActive(active.boolean);
        return ExecuteOutput(
            graph, node.id, 0, owner, inputManager, deltaTime, budget, error);
    }
    default:
        error = "データノードは実行線へ接続できません。";
        return false;
    }
}

bool VisualScriptRuntime::ReadInput(
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
    std::string& error) {
    const VisualScriptLink* link = graph.FindInputLink(node.id, inputPinIndex);
    if (link == nullptr) {
        result = fallback;
        return true;
    }
    const VisualScriptNode* source = graph.FindNode(link->sourceNodeId);
    if (source == nullptr) {
        error = "データリンクの接続元が見つかりません。";
        return false;
    }
    return EvaluateOutput(
        graph, *source, link->sourcePinIndex, owner, inputManager,
        deltaTime, budget, depth + 1, result, error);
}

bool VisualScriptRuntime::EvaluateOutput(
    const VisualScriptGraph& graph,
    const VisualScriptNode& node,
    int outputPinIndex,
    GameObject& owner,
    InputManager* inputManager,
    float deltaTime,
    int& budget,
    int depth,
    Value& result,
    std::string& error) {
    (void)outputPinIndex;
    if (depth > 64) {
        error = "データノードの参照が深すぎます。";
        return false;
    }
    if (--budget < 0) {
        error = "1イベントの実行上限（1000ノード）を超えました。";
        return false;
    }
    switch (node.type) {
    case VisualScriptNodeType::EventCollisionEnter:
    case VisualScriptNodeType::EventCollisionStay:
    case VisualScriptNodeType::EventCollisionExit:
        if (outputPinIndex != 1) {
            error = "衝突イベントの実行ピンは値として使用できません。";
            return false;
        }
        result = MakeGameObject(eventGameObject_);
        return true;
    case VisualScriptNodeType::KeyPressed:
        if (inputManager == nullptr) {
            error = "InputManagerが設定されていません。";
            return false;
        }
        result = MakeBoolean(inputManager->IsKeyPressed(node.key));
        return true;
    case VisualScriptNodeType::KeyTriggered:
        if (inputManager == nullptr) {
            error = "InputManagerが設定されていません。";
            return false;
        }
        result = MakeBoolean(inputManager->IsKeyTriggered(node.key));
        return true;
    case VisualScriptNodeType::BooleanAnd: {
        Value left;
        Value right;
        if (!ReadInput(
                graph, node, 0, MakeBoolean(node.boolValue), owner,
                inputManager, deltaTime, budget, depth, left, error) ||
            !ReadInput(
                graph, node, 1, MakeBoolean(node.boolValue), owner,
                inputManager, deltaTime, budget, depth, right, error)) {
            return false;
        }
        result = MakeBoolean(left.boolean && right.boolean);
        return true;
    }
    case VisualScriptNodeType::FloatValue:
        result = MakeFloat(node.floatValue);
        return true;
    case VisualScriptNodeType::Vector3Value:
        result = MakeVector(node.vectorValue);
        return true;
    case VisualScriptNodeType::DeltaTime:
        result = MakeFloat(deltaTime);
        return true;
    case VisualScriptNodeType::AddFloat:
    case VisualScriptNodeType::MultiplyFloat: {
        Value left;
        Value right;
        if (!ReadInput(
                graph, node, 0, MakeFloat(node.floatValue), owner,
                inputManager, deltaTime, budget, depth, left, error) ||
            !ReadInput(
                graph, node, 1, MakeFloat(node.floatValueB), owner,
                inputManager, deltaTime, budget, depth, right, error)) {
            return false;
        }
        result = MakeFloat(
            node.type == VisualScriptNodeType::AddFloat
            ? left.number + right.number
            : left.number * right.number);
        return true;
    }
    case VisualScriptNodeType::EqualFloat:
    case VisualScriptNodeType::LessFloat:
    case VisualScriptNodeType::GreaterFloat: {
        Value left;
        Value right;
        if (!ReadInput(
                graph, node, 0, MakeFloat(node.floatValue), owner,
                inputManager, deltaTime, budget, depth, left, error) ||
            !ReadInput(
                graph, node, 1, MakeFloat(node.floatValueB), owner,
                inputManager, deltaTime, budget, depth, right, error)) {
            return false;
        }
        bool comparison = false;
        if (node.type == VisualScriptNodeType::EqualFloat) {
            comparison = std::abs(left.number - right.number) <= 0.00001f;
        } else if (node.type == VisualScriptNodeType::LessFloat) {
            comparison = left.number < right.number;
        } else {
            comparison = left.number > right.number;
        }
        result = MakeBoolean(comparison);
        return true;
    }
    case VisualScriptNodeType::MakeVector3: {
        Value x;
        Value y;
        Value z;
        if (!ReadInput(
                graph, node, 0, MakeFloat(node.vectorValue.x), owner,
                inputManager, deltaTime, budget, depth, x, error) ||
            !ReadInput(
                graph, node, 1, MakeFloat(node.vectorValue.y), owner,
                inputManager, deltaTime, budget, depth, y, error) ||
            !ReadInput(
                graph, node, 2, MakeFloat(node.vectorValue.z), owner,
                inputManager, deltaTime, budget, depth, z, error)) {
            return false;
        }
        result = MakeVector({ x.number, y.number, z.number });
        return true;
    }
    case VisualScriptNodeType::IsGrounded: {
        const RigidbodyComponent* rigidbody =
            owner.GetComponent<RigidbodyComponent>();
        result = MakeBoolean(rigidbody != nullptr && rigidbody->IsGrounded());
        return true;
    }
    case VisualScriptNodeType::GetBooleanVariable:
    case VisualScriptNodeType::GetFloatVariable:
    case VisualScriptNodeType::GetVector3Variable: {
        const auto iterator = variables_.find(node.stringValue);
        if (iterator == variables_.end()) {
            error = "取得する変数が見つかりません: " + node.stringValue;
            return false;
        }
        const VisualScriptValueType expectedType =
            node.type == VisualScriptNodeType::GetBooleanVariable
            ? VisualScriptValueType::Boolean
            : node.type == VisualScriptNodeType::GetFloatVariable
            ? VisualScriptValueType::Float
            : VisualScriptValueType::Vector3;
        if (iterator->second.type != expectedType) {
            error = "変数と取得ノードの型が一致しません: " + node.stringValue;
            return false;
        }
        result = iterator->second;
        return true;
    }
    case VisualScriptNodeType::FindGameObject:
        result = MakeGameObject(
            owner.GetScene() != nullptr
            ? owner.GetScene()->FindGameObject(node.stringValue)
            : nullptr);
        return true;
    default:
        error = "実行ノードの出力は値として使用できません。";
        return false;
    }
}
