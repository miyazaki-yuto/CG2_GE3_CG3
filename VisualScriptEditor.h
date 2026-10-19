#pragma once

#include "VisualScriptGraph.h"

#include <string>
#include <vector>

class GameObject;
class VisualScriptComponent;

class VisualScriptEditor {
public:
    void Draw(GameObject* selectedGameObject);

private:
    void DrawNode(
        VisualScriptComponent& component,
        VisualScriptNode& node,
        float originX,
        float originY);
    void DrawInputSelector(
        VisualScriptGraph& graph,
        const VisualScriptNode& targetNode,
        int inputPinIndex);
    void DrawAddNodeMenu(VisualScriptGraph& graph);
    void DrawVariables(VisualScriptGraph& graph);
    void PushUndo(const VisualScriptGraph& graph);
    bool Undo(VisualScriptGraph& graph);
    bool Redo(VisualScriptGraph& graph);
    void CopySelectedNode(const VisualScriptGraph& graph);
    void PasteNode(VisualScriptGraph& graph);

    int selectedNodeId_ = 0;
    int pendingDeleteNodeId_ = 0;
    int draggingSourceNodeId_ = 0;
    int draggingSourcePinIndex_ = 0;
    uint64_t currentGameObjectId_ = 0;
    bool hasClipboardNode_ = false;
    bool nodeDragChanged_ = false;
    VisualScriptNode clipboardNode_{};
    VisualScriptGraph nodeDragStartGraph_{};
    std::vector<VisualScriptGraph> undoStack_;
    std::vector<VisualScriptGraph> redoStack_;
    std::string status_;
};
