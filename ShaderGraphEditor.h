#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class Graphics;

class ShaderGraphEditor {
public:
    void Initialize(Graphics* graphics);
    void Draw();

private:
    enum class NodeType {
        Texture,
        Color,
        Float,
        Multiply,
        Add,
        Output
    };

    struct Node {
        int id = 0;
        NodeType type = NodeType::Color;
        std::string name;
        float x = 0.0f;
        float y = 0.0f;
        std::array<float, 4> value{ 1.0f, 1.0f, 1.0f, 1.0f };
        int inputA = 0;
        int inputB = 0;
    };

    void ResetGraph();
    void AddNode(NodeType type);
    void DeleteNode(int nodeId);
    void DrawNode(Node& node, float originX, float originY);
    void DrawNodeSockets(Node& node, float originX, float originY);
    void DrawInputSelector(
        const char* label,
        int targetNodeId,
        int& sourceId);
    bool LoadGraph(const std::filesystem::path& path);
    bool SaveAndCompile();
    std::string BuildExpression(
        int nodeId,
        std::vector<int>& recursionStack,
        bool& valid) const;
    bool DependsOn(
        int nodeId,
        int dependencyId,
        std::vector<int>& visited) const;
    const Node* FindNode(int nodeId) const;
    const char* GetNodeTypeName(NodeType type) const;
    bool TryParseNodeType(
        const std::string& name,
        NodeType& type) const;

    Graphics* graphics_ = nullptr;
    std::vector<Node> nodes_;
    int nextNodeId_ = 1;
    int selectedNodeId_ = 0;
    int draggingSourceNodeId_ = 0;
    int pendingDeleteNodeId_ = 0;
    std::array<char, 64> graphName_{};
    std::string status_;
};
