#include "VisualScriptEditor.h"

#include "GameObject.h"
#include "VisualScriptComponent.h"

#include <algorithm>
#include <array>
#include <cstdio>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

namespace {

#ifdef USE_IMGUI
constexpr float kNodeWidth = 260.0f;
constexpr float kNodeHeight = 245.0f;
constexpr float kPinStartY = 55.0f;
constexpr float kPinSpacing = 27.0f;

ImU32 GetPinColor(VisualScriptValueType type) {
    switch (type) {
    case VisualScriptValueType::Execution: return IM_COL32(245, 245, 245, 255);
    case VisualScriptValueType::Boolean: return IM_COL32(220, 80, 80, 255);
    case VisualScriptValueType::Float: return IM_COL32(90, 210, 130, 255);
    case VisualScriptValueType::Vector3: return IM_COL32(245, 195, 65, 255);
    case VisualScriptValueType::GameObject: return IM_COL32(190, 100, 235, 255);
    }
    return IM_COL32_WHITE;
}

ImVec2 GetInputPosition(
    const VisualScriptNode& node,
    int pinIndex,
    float originX,
    float originY) {
    return {
        originX + node.x,
        originY + node.y + kPinStartY + pinIndex * kPinSpacing
    };
}

ImVec2 GetOutputPosition(
    const VisualScriptNode& node,
    int pinIndex,
    float originX,
    float originY) {
    return {
        originX + node.x + kNodeWidth,
        originY + node.y + kPinStartY + pinIndex * kPinSpacing
    };
}
#endif

const char* GetKeyName(int key, std::array<char, 32>& buffer) {
    if (key >= 'A' && key <= 'Z') {
        buffer[0] = static_cast<char>(key);
        buffer[1] = '\0';
        return buffer.data();
    }
    if (key >= '0' && key <= '9') {
        buffer[0] = static_cast<char>(key);
        buffer[1] = '\0';
        return buffer.data();
    }
    if (key == 0x20) {
        return "Space";
    }
    std::snprintf(buffer.data(), buffer.size(), "VK 0x%02X", key);
    return buffer.data();
}

} // namespace

void VisualScriptEditor::Draw(GameObject* selectedGameObject) {
#ifdef USE_IMGUI
    if (!ImGui::Begin("ビジュアルスクリプト###Visual Script")) {
        ImGui::End();
        return;
    }
    VisualScriptComponent* component = selectedGameObject != nullptr
        ? selectedGameObject->GetComponent<VisualScriptComponent>()
        : nullptr;
    if (component == nullptr) {
        ImGui::TextWrapped(
            "Visual Scriptコンポーネントを持つGameObjectを選択してください。");
        ImGui::End();
        return;
    }
    if (currentGameObjectId_ != selectedGameObject->GetId()) {
        currentGameObjectId_ = selectedGameObject->GetId();
        selectedNodeId_ = 0;
        pendingDeleteNodeId_ = 0;
        draggingSourceNodeId_ = 0;
        undoStack_.clear();
        redoStack_.clear();
        status_.clear();
    }

    VisualScriptGraph& graph = component->GetGraph();
    ImGui::Text("対象: %s", selectedGameObject->GetName().c_str());
    ImGui::SameLine();
    if (ImGui::Button("保存###SaveVisualScript")) {
        status_ = component->SaveGraph()
            ? "Visual Scriptを保存しました。"
            : component->GetLastError();
    }
    ImGui::SameLine();
    if (ImGui::Button("再読み込み###ReloadVisualScript")) {
        status_ = component->LoadGraph()
            ? "Visual Scriptを再読み込みしました。"
            : component->GetLastError();
        if (component->GetLastError().empty()) {
            undoStack_.clear();
            redoStack_.clear();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("移動サンプルに戻す###ResetVisualScript")) {
        PushUndo(graph);
        component->ResetToMovementSample();
        selectedNodeId_ = 0;
        status_ = "Wキー移動サンプルを作成しました。保存すると確定します。";
    }
    ImGui::SameLine();
    if (ImGui::Button("ノードを追加###AddVisualScriptNode")) {
        ImGui::OpenPopup("AddVisualScriptNodePopup");
    }
    DrawAddNodeMenu(graph);
    ImGui::SameLine();
    ImGui::BeginDisabled(selectedNodeId_ == 0);
    if (ImGui::Button("選択ノードを削除###DeleteVisualScriptNode")) {
        pendingDeleteNodeId_ = selectedNodeId_;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(undoStack_.empty());
    if (ImGui::Button("元に戻す###UndoVisualScript")) {
        Undo(graph);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(redoStack_.empty());
    if (ImGui::Button("やり直す###RedoVisualScript")) {
        Redo(graph);
    }
    ImGui::EndDisabled();

    ImGui::TextDisabled(
        "白=実行、赤=bool、緑=float、黄=Vector3、紫=GameObject。"
        "出力の丸から入力の丸へドラッグして接続できます。");
    DrawVariables(graph);
    if (!status_.empty()) {
        ImGui::TextWrapped("%s", status_.c_str());
    }
    if (!component->GetLastError().empty()) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
            "実行状態: %s", component->GetLastError().c_str());
    } else {
        ImGui::TextDisabled(
            "前回の実行ノード数: %d", component->GetLastExecutionCount());
    }

    ImGui::BeginChild(
        "VisualScriptCanvas", ImVec2(0.0f, 0.0f), true,
        ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    constexpr float canvasWidth = 1800.0f;
    constexpr float canvasHeight = 1100.0f;
    drawList->AddRectFilled(
        origin, ImVec2(origin.x + canvasWidth, origin.y + canvasHeight),
        IM_COL32(26, 28, 32, 255));
    for (float x = 0.0f; x < canvasWidth; x += 32.0f) {
        drawList->AddLine(
            ImVec2(origin.x + x, origin.y),
            ImVec2(origin.x + x, origin.y + canvasHeight),
            IM_COL32(43, 46, 52, 255));
    }
    for (float y = 0.0f; y < canvasHeight; y += 32.0f) {
        drawList->AddLine(
            ImVec2(origin.x, origin.y + y),
            ImVec2(origin.x + canvasWidth, origin.y + y),
            IM_COL32(43, 46, 52, 255));
    }

    for (const VisualScriptLink& link : graph.GetLinks()) {
        const VisualScriptNode* source = graph.FindNode(link.sourceNodeId);
        const VisualScriptNode* target = graph.FindNode(link.targetNodeId);
        if (source == nullptr || target == nullptr) {
            continue;
        }
        const auto& outputPins = VisualScriptGraph::GetOutputPins(source->type);
        if (link.sourcePinIndex < 0 ||
            link.sourcePinIndex >= static_cast<int>(outputPins.size())) {
            continue;
        }
        const ImVec2 start = GetOutputPosition(
            *source, link.sourcePinIndex, origin.x, origin.y);
        const ImVec2 end = GetInputPosition(
            *target, link.targetPinIndex, origin.x, origin.y);
        drawList->AddBezierCubic(
            start, ImVec2(start.x + 75.0f, start.y),
            ImVec2(end.x - 75.0f, end.y), end,
            GetPinColor(outputPins[link.sourcePinIndex].type), 3.0f);
    }

    if (const VisualScriptNode* draggingSource =
        graph.FindNode(draggingSourceNodeId_)) {
        const auto& outputPins =
            VisualScriptGraph::GetOutputPins(draggingSource->type);
        if (draggingSourcePinIndex_ >= 0 &&
            draggingSourcePinIndex_ < static_cast<int>(outputPins.size())) {
            const ImVec2 start = GetOutputPosition(
                *draggingSource, draggingSourcePinIndex_, origin.x, origin.y);
            const ImVec2 end = ImGui::GetMousePos();
            drawList->AddBezierCubic(
                start, ImVec2(start.x + 75.0f, start.y),
                ImVec2(end.x - 75.0f, end.y), end,
                GetPinColor(outputPins[draggingSourcePinIndex_].type), 3.0f);
        }
    }

    for (VisualScriptNode& node : graph.GetNodes()) {
        DrawNode(*component, node, origin.x, origin.y);
    }
    const bool canvasFocused = ImGui::IsWindowFocused(
        ImGuiFocusedFlags_RootAndChildWindows);
    if (canvasFocused && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            pendingDeleteNodeId_ = selectedNodeId_;
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
            CopySelectedNode(graph);
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)) {
            PasteNode(graph);
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            Undo(graph);
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
            Redo(graph);
        }
    }
    if (draggingSourceNodeId_ != 0 && ImGui::IsMouseReleased(0)) {
        draggingSourceNodeId_ = 0;
        status_ = "接続をキャンセルしました。";
    }
    if (pendingDeleteNodeId_ != 0) {
        const int nodeId = pendingDeleteNodeId_;
        pendingDeleteNodeId_ = 0;
        if (graph.FindNode(nodeId) != nullptr) {
            PushUndo(graph);
        }
        if (graph.RemoveNode(nodeId)) {
            selectedNodeId_ = 0;
            status_ = "ノードと接続を削除しました。";
        }
    }
    ImGui::SetCursorScreenPos(
        ImVec2(origin.x + canvasWidth, origin.y + canvasHeight));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
    ImGui::EndChild();
    ImGui::End();
#else
    (void)selectedGameObject;
#endif
}

void VisualScriptEditor::DrawNode(
    VisualScriptComponent& component,
    VisualScriptNode& node,
    float originX,
    float originY) {
#ifdef USE_IMGUI
    ImGui::PushID(node.id);
    const ImVec2 position(originX + node.x, originY + node.y);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 headerColor =
        node.type == VisualScriptNodeType::EventStart ||
        node.type == VisualScriptNodeType::EventUpdate ||
        node.type == VisualScriptNodeType::EventCollisionEnter ||
        node.type == VisualScriptNodeType::EventCollisionStay ||
        node.type == VisualScriptNodeType::EventCollisionExit
        ? IM_COL32(120, 70, 165, 255)
        : IM_COL32(45, 115, 170, 255);
    drawList->AddRectFilled(
        position, ImVec2(position.x + kNodeWidth, position.y + kNodeHeight),
        IM_COL32(55, 59, 67, 250), 7.0f);
    drawList->AddRectFilled(
        position, ImVec2(position.x + kNodeWidth, position.y + 30.0f),
        headerColor, 7.0f);
    drawList->AddRect(
        position, ImVec2(position.x + kNodeWidth, position.y + kNodeHeight),
        selectedNodeId_ == node.id
        ? IM_COL32(255, 170, 55, 255)
        : IM_COL32(100, 106, 116, 255),
        7.0f, 0, selectedNodeId_ == node.id ? 2.5f : 1.0f);
    drawList->AddText(
        ImVec2(position.x + 9.0f, position.y + 7.0f),
        IM_COL32_WHITE, node.name.c_str());

    ImGui::SetCursorScreenPos(position);
    ImGui::InvisibleButton("NodeHeader", ImVec2(kNodeWidth, 30.0f));
    if (ImGui::IsItemActivated()) {
        nodeDragStartGraph_ = component.GetGraph();
        nodeDragChanged_ = false;
    }
    if (ImGui::IsItemClicked()) {
        selectedNodeId_ = node.id;
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        node.x = (std::max)(0.0f, node.x + delta.x);
        node.y = (std::max)(0.0f, node.y + delta.y);
        nodeDragChanged_ = true;
    }
    if (ImGui::IsItemDeactivated() && nodeDragChanged_) {
        PushUndo(nodeDragStartGraph_);
        nodeDragChanged_ = false;
    }
    if (ImGui::BeginPopupContextItem("VisualScriptNodeContext")) {
        selectedNodeId_ = node.id;
        if (ImGui::MenuItem("ノードを削除")) {
            pendingDeleteNodeId_ = node.id;
        }
        ImGui::EndPopup();
    }

    VisualScriptGraph& graph = component.GetGraph();
    const auto& inputPins = VisualScriptGraph::GetInputPins(node.type);
    const auto& outputPins = VisualScriptGraph::GetOutputPins(node.type);
    for (int index = 0; index < static_cast<int>(inputPins.size()); ++index) {
        const ImVec2 pin = GetInputPosition(node, index, originX, originY);
        drawList->AddCircleFilled(pin, 6.0f, GetPinColor(inputPins[index].type));
        ImGui::PushID(index);
        ImGui::SetCursorScreenPos(ImVec2(pin.x - 8.0f, pin.y - 8.0f));
        ImGui::InvisibleButton("InputSocket", ImVec2(16.0f, 16.0f));
        const bool inputHovered = ImGui::IsItemHovered(
            ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (inputHovered && ImGui::IsMouseReleased(0) &&
            draggingSourceNodeId_ != 0) {
            std::string error;
            const VisualScriptGraph before = graph;
            if (graph.Connect(
                draggingSourceNodeId_, draggingSourcePinIndex_,
                node.id, index, &error)) {
                PushUndo(before);
                status_ = "ノードを接続しました。";
            } else {
                status_ = error;
            }
            draggingSourceNodeId_ = 0;
        }
        if (inputHovered && ImGui::IsMouseClicked(1)) {
            if (graph.FindInputLink(node.id, index) != nullptr) {
                PushUndo(graph);
                graph.DisconnectInput(node.id, index);
                status_ = "接続を解除しました。";
            }
        }
        ImGui::PopID();
        ImGui::SetCursorScreenPos(ImVec2(pin.x + 9.0f, pin.y - 10.0f));
        ImGui::SetNextItemWidth(145.0f);
        DrawInputSelector(graph, node, index);
    }
    for (int index = 0; index < static_cast<int>(outputPins.size()); ++index) {
        const ImVec2 pin = GetOutputPosition(node, index, originX, originY);
        drawList->AddCircleFilled(pin, 6.0f, GetPinColor(outputPins[index].type));
        const ImVec2 textSize = ImGui::CalcTextSize(outputPins[index].name);
        drawList->AddText(
            ImVec2(pin.x - textSize.x - 10.0f, pin.y - 8.0f),
            IM_COL32(225, 225, 225, 255), outputPins[index].name);
        ImGui::PushID(index);
        ImGui::SetCursorScreenPos(ImVec2(pin.x - 8.0f, pin.y - 8.0f));
        ImGui::InvisibleButton("OutputSocket", ImVec2(16.0f, 16.0f));
        if (ImGui::IsItemActivated()) {
            draggingSourceNodeId_ = node.id;
            draggingSourcePinIndex_ = index;
            selectedNodeId_ = node.id;
        }
        ImGui::PopID();
    }

    const float propertyY = position.y + 65.0f +
        static_cast<float>((std::max)(inputPins.size(), outputPins.size())) *
        kPinSpacing;
    const auto setPropertyCursor = [&](float offsetY) {
        // SetCursorScreenPosの次の項目が改行すると、ImGuiはChildの左端へ
        // X座標を戻す。ノード内の各行へ座標を明示して枠外への移動を防ぐ。
        ImGui::SetCursorScreenPos(
            ImVec2(position.x + 10.0f, propertyY + offsetY));
    };
    constexpr float propertyWidth = kNodeWidth - 20.0f;
    const auto recordActivation = [&](const VisualScriptGraph& before) {
        if (ImGui::IsItemActivated()) {
            PushUndo(before);
        }
    };
    const auto drawVariableSelector = [&](VisualScriptValueType type) {
        setPropertyCursor(0.0f);
        ImGui::SetNextItemWidth(propertyWidth);
        const char* preview = node.stringValue.empty()
            ? "変数を選択"
            : node.stringValue.c_str();
        if (ImGui::BeginCombo("###VariableName", preview)) {
            for (const VisualScriptVariable& variable : graph.GetVariables()) {
                if (variable.type != type) {
                    continue;
                }
                const bool selected = variable.name == node.stringValue;
                if (ImGui::Selectable(variable.name.c_str(), selected)) {
                    PushUndo(graph);
                    node.stringValue = variable.name;
                }
            }
            ImGui::EndCombo();
        }
    };
    switch (node.type) {
    case VisualScriptNodeType::KeyPressed:
    case VisualScriptNodeType::KeyTriggered: {
        std::array<char, 32> keyName{};
        setPropertyCursor(0.0f);
        ImGui::Text("キー: %s", GetKeyName(node.key, keyName));
        setPropertyCursor(24.0f);
        ImGui::SetNextItemWidth(propertyWidth);
        const VisualScriptGraph before = graph;
        if (ImGui::InputInt("###KeyCode", &node.key)) {
            node.key = std::clamp(node.key, 0, 255);
        }
        recordActivation(before);
        break;
    }
    case VisualScriptNodeType::FloatValue:
    case VisualScriptNodeType::PrintFloat: {
        setPropertyCursor(0.0f);
        ImGui::TextUnformatted("既定値");
        setPropertyCursor(22.0f);
        ImGui::SetNextItemWidth(propertyWidth);
        const VisualScriptGraph before = graph;
        ImGui::DragFloat("###FloatValue", &node.floatValue, 0.05f);
        recordActivation(before);
        break;
    }
    case VisualScriptNodeType::Vector3Value:
    case VisualScriptNodeType::MakeVector3:
    case VisualScriptNodeType::AddImpulse: {
        setPropertyCursor(0.0f);
        ImGui::TextUnformatted("既定値");
        setPropertyCursor(22.0f);
        ImGui::SetNextItemWidth(propertyWidth);
        const VisualScriptGraph before = graph;
        ImGui::DragFloat3("###VectorValue", &node.vectorValue.x, 0.05f);
        recordActivation(before);
        break;
    }
    case VisualScriptNodeType::Translate:
    case VisualScriptNodeType::TranslateGameObject: {
        setPropertyCursor(0.0f);
        ImGui::TextUnformatted("移動量");
        setPropertyCursor(22.0f);
        ImGui::SetNextItemWidth(propertyWidth);
        const VisualScriptGraph beforeMovement = graph;
        ImGui::DragFloat3("###Movement", &node.vectorValue.x, 0.05f);
        recordActivation(beforeMovement);
        setPropertyCursor(50.0f);
        const VisualScriptGraph beforePerSecond = graph;
        ImGui::Checkbox("毎秒の移動量###PerSecond", &node.boolValue);
        recordActivation(beforePerSecond);
        break;
    }
    case VisualScriptNodeType::BooleanAnd: {
        setPropertyCursor(0.0f);
        const VisualScriptGraph before = graph;
        ImGui::Checkbox("未接続時の値###BoolValue", &node.boolValue);
        recordActivation(before);
        break;
    }
    case VisualScriptNodeType::EqualFloat:
    case VisualScriptNodeType::LessFloat:
    case VisualScriptNodeType::GreaterFloat:
    case VisualScriptNodeType::AddFloat:
    case VisualScriptNodeType::MultiplyFloat: {
        setPropertyCursor(0.0f);
        ImGui::TextUnformatted("未接続時の A / B");
        setPropertyCursor(22.0f);
        ImGui::SetNextItemWidth(propertyWidth);
        float values[2] = { node.floatValue, node.floatValueB };
        const VisualScriptGraph before = graph;
        if (ImGui::DragFloat2("###FloatDefaults", values, 0.05f)) {
            node.floatValue = values[0];
            node.floatValueB = values[1];
        }
        recordActivation(before);
        break;
    }
    case VisualScriptNodeType::GetBooleanVariable:
    case VisualScriptNodeType::SetBooleanVariable:
        drawVariableSelector(VisualScriptValueType::Boolean);
        break;
    case VisualScriptNodeType::GetFloatVariable:
    case VisualScriptNodeType::SetFloatVariable:
        drawVariableSelector(VisualScriptValueType::Float);
        break;
    case VisualScriptNodeType::GetVector3Variable:
    case VisualScriptNodeType::SetVector3Variable:
        drawVariableSelector(VisualScriptValueType::Vector3);
        break;
    case VisualScriptNodeType::FindGameObject: {
        setPropertyCursor(0.0f);
        ImGui::TextUnformatted("検索する名前");
        std::array<char, 128> name{};
        std::copy_n(
            node.stringValue.data(),
            (std::min)(node.stringValue.size(), name.size() - 1),
            name.data());
        setPropertyCursor(22.0f);
        ImGui::SetNextItemWidth(propertyWidth);
        const VisualScriptGraph before = graph;
        if (ImGui::InputText("###GameObjectName", name.data(), name.size())) {
            node.stringValue = name.data();
        }
        recordActivation(before);
        break;
    }
    case VisualScriptNodeType::SetGameObjectActive: {
        setPropertyCursor(0.0f);
        const VisualScriptGraph before = graph;
        ImGui::Checkbox("未接続時は有効###ActiveValue", &node.boolValue);
        recordActivation(before);
        break;
    }
    default:
        setPropertyCursor(0.0f);
        ImGui::TextDisabled("設定項目なし");
        break;
    }
    ImGui::PopID();
#else
    (void)component;
    (void)node;
    (void)originX;
    (void)originY;
#endif
}

void VisualScriptEditor::DrawInputSelector(
    VisualScriptGraph& graph,
    const VisualScriptNode& targetNode,
    int inputPinIndex) {
#ifdef USE_IMGUI
    const auto& inputPins = VisualScriptGraph::GetInputPins(targetNode.type);
    if (inputPinIndex < 0 ||
        inputPinIndex >= static_cast<int>(inputPins.size())) {
        return;
    }
    const VisualScriptLink* current =
        graph.FindInputLink(targetNode.id, inputPinIndex);
    std::string preview = inputPins[inputPinIndex].name;
    if (current != nullptr) {
        if (const VisualScriptNode* source = graph.FindNode(current->sourceNodeId)) {
            const auto& outputs = VisualScriptGraph::GetOutputPins(source->type);
            if (current->sourcePinIndex >= 0 &&
                current->sourcePinIndex < static_cast<int>(outputs.size())) {
                preview += " ← " + source->name + "." +
                    outputs[current->sourcePinIndex].name;
            }
        }
    } else {
        preview += " (未接続)";
    }

    ImGui::PushID(inputPinIndex);
    if (ImGui::BeginCombo("###InputSource", preview.c_str())) {
        if (ImGui::Selectable("未接続", current == nullptr)) {
            if (current != nullptr) {
                PushUndo(graph);
            }
            graph.DisconnectInput(targetNode.id, inputPinIndex);
        }
        for (const VisualScriptNode& source : graph.GetNodes()) {
            if (source.id == targetNode.id) {
                continue;
            }
            const auto& outputs = VisualScriptGraph::GetOutputPins(source.type);
            for (int outputIndex = 0;
                outputIndex < static_cast<int>(outputs.size());
                ++outputIndex) {
                if (outputs[outputIndex].type != inputPins[inputPinIndex].type) {
                    continue;
                }
                const std::string label = source.name + " / " +
                    outputs[outputIndex].name + "###" +
                    std::to_string(source.id) + ":" +
                    std::to_string(outputIndex);
                const bool selected = current != nullptr &&
                    current->sourceNodeId == source.id &&
                    current->sourcePinIndex == outputIndex;
                if (ImGui::Selectable(label.c_str(), selected)) {
                    std::string error;
                    const VisualScriptGraph before = graph;
                    if (!graph.Connect(
                        source.id, outputIndex, targetNode.id,
                        inputPinIndex, &error)) {
                        status_ = error;
                    } else {
                        PushUndo(before);
                        status_ = "ノードを接続しました。";
                    }
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
#else
    (void)graph;
    (void)targetNode;
    (void)inputPinIndex;
#endif
}

void VisualScriptEditor::DrawVariables(VisualScriptGraph& graph) {
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader("変数###VisualScriptVariables")) {
        return;
    }
    if (ImGui::Button("boolを追加###AddBoolVariable")) {
        PushUndo(graph);
        graph.AddVariable(VisualScriptValueType::Boolean);
    }
    ImGui::SameLine();
    if (ImGui::Button("floatを追加###AddFloatVariable")) {
        PushUndo(graph);
        graph.AddVariable(VisualScriptValueType::Float);
    }
    ImGui::SameLine();
    if (ImGui::Button("Vector3を追加###AddVectorVariable")) {
        PushUndo(graph);
        graph.AddVariable(VisualScriptValueType::Vector3);
    }

    size_t removeIndex = graph.GetVariables().size();
    for (size_t index = 0; index < graph.GetVariables().size(); ++index) {
        VisualScriptVariable& variable = graph.GetVariables()[index];
        ImGui::PushID(static_cast<int>(index));
        std::array<char, 96> name{};
        std::copy_n(
            variable.name.data(),
            (std::min)(variable.name.size(), name.size() - 1),
            name.data());
        ImGui::SetNextItemWidth(170.0f);
        const VisualScriptGraph beforeName = graph;
        const std::string oldName = variable.name;
        if (ImGui::InputText("###VariableName", name.data(), name.size())) {
            const std::string requestedName = name.data();
            const VisualScriptVariable* duplicate = graph.FindVariable(requestedName);
            if (!requestedName.empty() &&
                (duplicate == nullptr || duplicate == &variable)) {
                variable.name = requestedName;
                for (VisualScriptNode& node : graph.GetNodes()) {
                    if (node.stringValue == oldName) {
                        node.stringValue = requestedName;
                    }
                }
            } else {
                status_ = "変数名は空にできず、同じ名前も使用できません。";
            }
        }
        if (ImGui::IsItemActivated()) {
            PushUndo(beforeName);
        }
        ImGui::SameLine();
        const VisualScriptGraph beforeValue = graph;
        switch (variable.type) {
        case VisualScriptValueType::Boolean:
            ImGui::Checkbox("###VariableBool", &variable.boolValue);
            break;
        case VisualScriptValueType::Float:
            ImGui::SetNextItemWidth(130.0f);
            ImGui::DragFloat("###VariableFloat", &variable.floatValue, 0.05f);
            break;
        case VisualScriptValueType::Vector3:
            ImGui::SetNextItemWidth(260.0f);
            ImGui::DragFloat3(
                "###VariableVector", &variable.vectorValue.x, 0.05f);
            break;
        default:
            ImGui::TextDisabled("未対応の型");
            break;
        }
        if (ImGui::IsItemActivated()) {
            PushUndo(beforeValue);
        }
        ImGui::SameLine();
        ImGui::TextDisabled(
            "%s", VisualScriptGraph::GetValueTypeName(variable.type));
        ImGui::SameLine();
        if (ImGui::SmallButton("削除")) {
            removeIndex = index;
        }
        ImGui::PopID();
    }
    if (removeIndex < graph.GetVariables().size()) {
        PushUndo(graph);
        graph.RemoveVariable(removeIndex);
        status_ = "変数を削除しました。参照ノードは未選択状態になりました。";
    }
#else
    (void)graph;
#endif
}

void VisualScriptEditor::PushUndo(const VisualScriptGraph& graph) {
    constexpr size_t kMaximumHistory = 100;
    undoStack_.push_back(graph);
    if (undoStack_.size() > kMaximumHistory) {
        undoStack_.erase(undoStack_.begin());
    }
    redoStack_.clear();
}

bool VisualScriptEditor::Undo(VisualScriptGraph& graph) {
    if (undoStack_.empty()) {
        return false;
    }
    redoStack_.push_back(graph);
    graph = std::move(undoStack_.back());
    undoStack_.pop_back();
    draggingSourceNodeId_ = 0;
    if (graph.FindNode(selectedNodeId_) == nullptr) {
        selectedNodeId_ = 0;
    }
    status_ = "Visual Scriptの操作を元に戻しました。";
    return true;
}

bool VisualScriptEditor::Redo(VisualScriptGraph& graph) {
    if (redoStack_.empty()) {
        return false;
    }
    undoStack_.push_back(graph);
    graph = std::move(redoStack_.back());
    redoStack_.pop_back();
    draggingSourceNodeId_ = 0;
    if (graph.FindNode(selectedNodeId_) == nullptr) {
        selectedNodeId_ = 0;
    }
    status_ = "Visual Scriptの操作をやり直しました。";
    return true;
}

void VisualScriptEditor::CopySelectedNode(const VisualScriptGraph& graph) {
    const VisualScriptNode* selected = graph.FindNode(selectedNodeId_);
    if (selected == nullptr) {
        return;
    }
    clipboardNode_ = *selected;
    hasClipboardNode_ = true;
    status_ = "選択ノードをコピーしました。";
}

void VisualScriptEditor::PasteNode(VisualScriptGraph& graph) {
    if (!hasClipboardNode_) {
        return;
    }
    PushUndo(graph);
    VisualScriptNode& pasted = graph.AddNode(
        clipboardNode_.type,
        clipboardNode_.x + 30.0f,
        clipboardNode_.y + 30.0f);
    const int newId = pasted.id;
    pasted = clipboardNode_;
    pasted.id = newId;
    pasted.x += 30.0f;
    pasted.y += 30.0f;
    clipboardNode_ = pasted;
    selectedNodeId_ = pasted.id;
    status_ = "ノードを貼り付けました。接続は複製されません。";
}

void VisualScriptEditor::DrawAddNodeMenu(VisualScriptGraph& graph) {
#ifdef USE_IMGUI
    if (!ImGui::BeginPopup("AddVisualScriptNodePopup")) {
        return;
    }
    struct Entry {
        const char* category;
        VisualScriptNodeType type;
    };
    constexpr Entry entries[] = {
        { "イベント", VisualScriptNodeType::EventStart },
        { "イベント", VisualScriptNodeType::EventUpdate },
        { "イベント", VisualScriptNodeType::EventCollisionEnter },
        { "イベント", VisualScriptNodeType::EventCollisionStay },
        { "イベント", VisualScriptNodeType::EventCollisionExit },
        { "入力", VisualScriptNodeType::KeyPressed },
        { "入力", VisualScriptNodeType::KeyTriggered },
        { "制御", VisualScriptNodeType::Branch },
        { "制御", VisualScriptNodeType::Sequence },
        { "論理", VisualScriptNodeType::BooleanAnd },
        { "比較", VisualScriptNodeType::EqualFloat },
        { "比較", VisualScriptNodeType::LessFloat },
        { "比較", VisualScriptNodeType::GreaterFloat },
        { "値", VisualScriptNodeType::FloatValue },
        { "値", VisualScriptNodeType::Vector3Value },
        { "値", VisualScriptNodeType::DeltaTime },
        { "計算", VisualScriptNodeType::AddFloat },
        { "計算", VisualScriptNodeType::MultiplyFloat },
        { "計算", VisualScriptNodeType::MakeVector3 },
        { "変数", VisualScriptNodeType::GetBooleanVariable },
        { "変数", VisualScriptNodeType::SetBooleanVariable },
        { "変数", VisualScriptNodeType::GetFloatVariable },
        { "変数", VisualScriptNodeType::SetFloatVariable },
        { "変数", VisualScriptNodeType::GetVector3Variable },
        { "変数", VisualScriptNodeType::SetVector3Variable },
        { "GameObject", VisualScriptNodeType::Translate },
        { "GameObject", VisualScriptNodeType::FindGameObject },
        { "GameObject", VisualScriptNodeType::TranslateGameObject },
        { "GameObject", VisualScriptNodeType::SetGameObjectActive },
        { "物理", VisualScriptNodeType::IsGrounded },
        { "物理", VisualScriptNodeType::AddImpulse },
        { "デバッグ", VisualScriptNodeType::PrintFloat }
    };
    const char* previousCategory = nullptr;
    for (const Entry& entry : entries) {
        if (previousCategory == nullptr ||
            std::string(previousCategory) != entry.category) {
            if (previousCategory != nullptr) {
                ImGui::EndMenu();
            }
            if (!ImGui::BeginMenu(entry.category)) {
                previousCategory = nullptr;
                continue;
            }
            previousCategory = entry.category;
        }
        if (ImGui::MenuItem(VisualScriptGraph::GetNodeDisplayName(entry.type))) {
            PushUndo(graph);
            VisualScriptNode& node = graph.AddNode(entry.type, 120.0f, 120.0f);
            selectedNodeId_ = node.id;
            status_ = "ノードを追加しました。";
        }
    }
    if (previousCategory != nullptr) {
        ImGui::EndMenu();
    }
    ImGui::EndPopup();
#else
    (void)graph;
#endif
}
