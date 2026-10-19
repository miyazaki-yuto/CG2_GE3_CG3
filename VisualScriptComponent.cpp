#include "VisualScriptComponent.h"

#include "GameObject.h"
#include "ColliderComponent.h"

#include <utility>

VisualScriptComponent::VisualScriptComponent(
    InputManager* inputManager,
    std::string scriptPath)
    : inputManager_(inputManager), scriptPath_(std::move(scriptPath)) {
    graph_.ResetToMovementSample();
}

void VisualScriptComponent::Awake() {
    if (!scriptPath_.empty()) {
        LoadGraph();
    }
}

void VisualScriptComponent::Start() {
    if (GetOwner() == nullptr) {
        return;
    }
    runtime_.ResetVariables(graph_);
    runtime_.ExecuteEvent(
        graph_, VisualScriptNodeType::EventStart, *GetOwner(), inputManager_,
        0.0f, lastError_);
}

void VisualScriptComponent::OnCollisionEnter(ColliderComponent& other) {
    if (GetOwner() == nullptr) {
        return;
    }
    runtime_.ExecuteEvent(
        graph_, VisualScriptNodeType::EventCollisionEnter,
        *GetOwner(), inputManager_, 0.0f, lastError_, other.GetOwner());
}

void VisualScriptComponent::OnCollisionStay(ColliderComponent& other) {
    if (GetOwner() == nullptr) {
        return;
    }
    runtime_.ExecuteEvent(
        graph_, VisualScriptNodeType::EventCollisionStay,
        *GetOwner(), inputManager_, 0.0f, lastError_, other.GetOwner());
}

void VisualScriptComponent::OnCollisionExit(ColliderComponent& other) {
    if (GetOwner() == nullptr) {
        return;
    }
    runtime_.ExecuteEvent(
        graph_, VisualScriptNodeType::EventCollisionExit,
        *GetOwner(), inputManager_, 0.0f, lastError_, other.GetOwner());
}

void VisualScriptComponent::Update(float deltaTime) {
    if (GetOwner() == nullptr) {
        return;
    }
    runtime_.ExecuteEvent(
        graph_, VisualScriptNodeType::EventUpdate, *GetOwner(), inputManager_,
        deltaTime, lastError_);
}

bool VisualScriptComponent::LoadGraph() {
    if (scriptPath_.empty()) {
        lastError_ = "Visual Scriptのファイルパスが空です。";
        return false;
    }
    if (!graph_.Load(scriptPath_, lastError_)) {
        return false;
    }
    runtime_.ResetVariables(graph_);
    return true;
}

bool VisualScriptComponent::SaveGraph() {
    if (scriptPath_.empty()) {
        lastError_ = "Visual Scriptのファイルパスが空です。";
        return false;
    }
    return graph_.Save(scriptPath_, lastError_);
}

void VisualScriptComponent::ResetToMovementSample() {
    graph_.ResetToMovementSample();
    runtime_.ResetVariables(graph_);
    lastError_.clear();
}
