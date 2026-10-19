#pragma once

#include "Component.h"
#include "VisualScriptGraph.h"
#include "VisualScriptRuntime.h"

#include <string>

class InputManager;

class VisualScriptComponent final : public Component {
public:
    explicit VisualScriptComponent(
        InputManager* inputManager = nullptr,
        std::string scriptPath = {});
    ~VisualScriptComponent() override = default;

    VisualScriptComponent(const VisualScriptComponent&) = delete;
    VisualScriptComponent& operator=(const VisualScriptComponent&) = delete;

    void Awake() override;
    void Start() override;
    void Update(float deltaTime) override;
    void OnCollisionEnter(ColliderComponent& other) override;
    void OnCollisionStay(ColliderComponent& other) override;
    void OnCollisionExit(ColliderComponent& other) override;

    const std::string& GetScriptPath() const { return scriptPath_; }
    void SetScriptPath(std::string scriptPath) {
        scriptPath_ = std::move(scriptPath);
    }
    VisualScriptGraph& GetGraph() { return graph_; }
    const VisualScriptGraph& GetGraph() const { return graph_; }
    const std::string& GetLastError() const { return lastError_; }
    int GetLastExecutionCount() const {
        return runtime_.GetLastExecutionCount();
    }

    bool LoadGraph();
    bool SaveGraph();
    void ResetToMovementSample();

private:
    InputManager* inputManager_ = nullptr;
    std::string scriptPath_;
    VisualScriptGraph graph_;
    VisualScriptRuntime runtime_;
    std::string lastError_;
};
