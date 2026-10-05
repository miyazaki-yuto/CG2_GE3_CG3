#include "ShaderGraphEditor.h"

#include "Graphics.h"
#include "ShaderManager.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>

#ifdef USE_IMGUI
#include <commdlg.h>
#include "externals/imgui/imgui.h"
#pragma comment(lib, "Comdlg32.lib")
#endif

namespace {

#ifdef USE_IMGUI
constexpr float kNodeWidth = 210.0f;
constexpr float kNodeHeight = 150.0f;
constexpr float kSocketRadius = 7.0f;
#endif

std::string SanitizeName(const char* source) {
    std::string result;
    for (const unsigned char character : std::string(source)) {
        if (std::isalnum(character) || character == '_') {
            result.push_back(static_cast<char>(character));
        }
    }
    return result;
}

bool ReadTextFile(
    const std::filesystem::path& path,
    std::string& text) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    text = stream.str();
    return input.good() || input.eof();
}

std::string UnescapeJsonString(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    bool escaped = false;
    for (const char character : text) {
        if (escaped) {
            if (character == 'n') {
                result.push_back('\n');
            } else if (character == 't') {
                result.push_back('\t');
            } else {
                result.push_back(character);
            }
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else {
            result.push_back(character);
        }
    }
    return result;
}

bool ReadStringField(
    const std::string& object,
    const std::string& field,
    std::string& result) {
    const std::regex pattern(
        "\"" + field + "\"\\s*:\\s*\"((?:\\\\.|[^\"])*)\"",
        std::regex::ECMAScript);
    std::smatch match;
    if (!std::regex_search(object, match, pattern)) {
        return false;
    }
    result = UnescapeJsonString(match[1].str());
    return true;
}

bool ReadIntField(
    const std::string& object,
    const std::string& field,
    int& result) {
    const std::regex pattern(
        "\"" + field + "\"\\s*:\\s*(-?[0-9]+)");
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

bool ReadFloatField(
    const std::string& object,
    const std::string& field,
    float& result) {
    const std::regex pattern(
        "\"" + field + "\"\\s*:\\s*"
        "(-?[0-9]+(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)");
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

std::vector<float> ReadFloatArrayField(
    const std::string& object,
    const std::string& field) {
    const std::regex arrayPattern(
        "\"" + field + "\"\\s*:\\s*\\[([^\\]]*)\\]");
    const std::regex numberPattern(
        "-?[0-9]+(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?");
    std::smatch match;
    if (!std::regex_search(object, match, arrayPattern)) {
        return {};
    }
    const std::string values = match[1].str();
    std::vector<float> result;
    for (std::sregex_iterator iterator(
            values.begin(), values.end(), numberPattern), end;
        iterator != end;
        ++iterator) {
        try {
            const float value = std::stof((*iterator)[0].str());
            if (!std::isfinite(value)) {
                return {};
            }
            result.push_back(value);
        } catch (...) {
            return {};
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

    std::vector<std::string> result;
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
                result.push_back(
                    json.substr(objectStart, index - objectStart + 1));
                objectStart = std::string::npos;
            }
        } else if (character == ']' && depth == 0) {
            break;
        }
    }
    return result;
}

#ifdef USE_IMGUI
std::filesystem::path OpenShaderGraphDialog() {
    std::array<wchar_t, 32768> selectedPath{};
    const wchar_t filter[] =
        L"Shader Graph (*.shadergraph.json)\0*.shadergraph.json\0"
        L"JSON Files (*.json)\0*.json\0All Files (*.*)\0*.*\0";
    const std::filesystem::path initialDirectory =
        std::filesystem::absolute("Resources/Shaders/Generated");

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = selectedPath.data();
    dialog.nMaxFile = static_cast<DWORD>(selectedPath.size());
    dialog.lpstrFilter = filter;
    dialog.nFilterIndex = 1;
    dialog.lpstrInitialDir = initialDirectory.c_str();
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog) == FALSE) {
        return {};
    }
    return std::filesystem::path(selectedPath.data());
}

ImVec2 GetOutputSocketPosition(
    float nodeX,
    float nodeY,
    float originX,
    float originY) {
    return {
        originX + nodeX + kNodeWidth,
        originY + nodeY + 76.0f
    };
}

ImVec2 GetInputSocketPosition(
    float nodeX,
    float nodeY,
    int inputIndex,
    float originX,
    float originY) {
    return {
        originX + nodeX,
        originY + nodeY + 76.0f + inputIndex * 30.0f
    };
}
#endif

} // namespace

void ShaderGraphEditor::Initialize(Graphics* graphics) {
    graphics_ = graphics;
    ResetGraph();
}

void ShaderGraphEditor::ResetGraph() {
    nodes_.clear();
    nextNodeId_ = 1;
    selectedNodeId_ = 0;
    draggingSourceNodeId_ = 0;
    pendingDeleteNodeId_ = 0;
    graphName_.fill('\0');
    const std::string defaultName = "NewShaderGraph";
    std::copy(defaultName.begin(), defaultName.end(), graphName_.begin());

    Node texture{};
    texture.id = nextNodeId_++;
    texture.type = NodeType::Texture;
    texture.name = "Main Texture";
    texture.x = 30.0f;
    texture.y = 80.0f;
    nodes_.push_back(texture);

    Node tint{};
    tint.id = nextNodeId_++;
    tint.type = NodeType::Color;
    tint.name = "Tint";
    tint.x = 30.0f;
    tint.y = 250.0f;
    nodes_.push_back(tint);

    Node multiply{};
    multiply.id = nextNodeId_++;
    multiply.type = NodeType::Multiply;
    multiply.name = "Multiply";
    multiply.x = 300.0f;
    multiply.y = 150.0f;
    multiply.inputA = texture.id;
    multiply.inputB = tint.id;
    nodes_.push_back(multiply);

    Node output{};
    output.id = nextNodeId_++;
    output.type = NodeType::Output;
    output.name = "Output";
    output.x = 570.0f;
    output.y = 170.0f;
    output.inputA = multiply.id;
    nodes_.push_back(output);
    status_ = "標準グラフを作成しました。";
}

void ShaderGraphEditor::AddNode(NodeType type) {
    Node node{};
    node.id = nextNodeId_++;
    node.type = type;
    node.name = std::format("{}{}", GetNodeTypeName(type), node.id);
    node.x = 80.0f + static_cast<float>((node.id % 4) * 150);
    node.y = 80.0f + static_cast<float>((node.id % 3) * 120);
    nodes_.push_back(std::move(node));
}

void ShaderGraphEditor::DeleteNode(int nodeId) {
    const Node* node = FindNode(nodeId);
    if (node == nullptr) {
        return;
    }
    if (node->type == NodeType::Output) {
        status_ = "出力ノードは削除できません。";
        return;
    }

    nodes_.erase(
        std::remove_if(
            nodes_.begin(),
            nodes_.end(),
            [nodeId](const Node& value) { return value.id == nodeId; }),
        nodes_.end());
    for (Node& target : nodes_) {
        if (target.inputA == nodeId) {
            target.inputA = 0;
        }
        if (target.inputB == nodeId) {
            target.inputB = 0;
        }
    }
    if (selectedNodeId_ == nodeId) {
        selectedNodeId_ = 0;
    }
    if (draggingSourceNodeId_ == nodeId) {
        draggingSourceNodeId_ = 0;
    }
    status_ = "ノードと、そのノードを使う接続を削除しました。";
}

const char* ShaderGraphEditor::GetNodeTypeName(NodeType type) const {
    switch (type) {
    case NodeType::Texture: return "Texture";
    case NodeType::Color: return "Color";
    case NodeType::Float: return "Float";
    case NodeType::Multiply: return "Multiply";
    case NodeType::Add: return "Add";
    case NodeType::Output: return "Output";
    }
    return "Unknown";
}

bool ShaderGraphEditor::TryParseNodeType(
    const std::string& name,
    NodeType& type) const {
    if (name == "Texture") {
        type = NodeType::Texture;
    } else if (name == "Color") {
        type = NodeType::Color;
    } else if (name == "Float") {
        type = NodeType::Float;
    } else if (name == "Multiply") {
        type = NodeType::Multiply;
    } else if (name == "Add") {
        type = NodeType::Add;
    } else if (name == "Output") {
        type = NodeType::Output;
    } else {
        return false;
    }
    return true;
}

void ShaderGraphEditor::Draw() {
#ifdef USE_IMGUI
    if (!ImGui::Begin("シェーダーグラフ###Shader Graph")) {
        ImGui::End();
        return;
    }

    ImGui::InputText("グラフ名###GraphName", graphName_.data(), graphName_.size());
    if (ImGui::Button("新規###NewGraph")) {
        ResetGraph();
    }
    ImGui::SameLine();
    if (ImGui::Button("読み込み...###LoadGraph")) {
        const std::filesystem::path path = OpenShaderGraphDialog();
        if (!path.empty()) {
            LoadGraph(path);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("保存してコンパイル###SaveAndCompile")) {
        SaveAndCompile();
    }
    if (ImGui::Button("カラーを追加###AddColor")) {
        AddNode(NodeType::Color);
    }
    ImGui::SameLine();
    if (ImGui::Button("Floatを追加###AddFloat")) {
        AddNode(NodeType::Float);
    }
    ImGui::SameLine();
    if (ImGui::Button("乗算を追加###AddMultiply")) {
        AddNode(NodeType::Multiply);
    }
    ImGui::SameLine();
    if (ImGui::Button("加算を追加###AddAdd")) {
        AddNode(NodeType::Add);
    }
    ImGui::SameLine();
    const Node* selectedNode = FindNode(selectedNodeId_);
    const bool canDelete =
        selectedNode != nullptr && selectedNode->type != NodeType::Output;
    ImGui::BeginDisabled(!canDelete);
    if (ImGui::Button("選択ノードを削除###DeleteSelected")) {
        pendingDeleteNodeId_ = selectedNodeId_;
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled(
        "出力の丸から入力の丸へドラッグして接続します。"
        "入力を右クリックすると接続を解除します。");
    ImGui::TextWrapped("%s", status_.c_str());

    ImGui::BeginChild(
        "ShaderGraphCanvas",
        ImVec2(0.0f, 0.0f),
        true,
        ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        origin,
        ImVec2(origin.x + 1200.0f, origin.y + 800.0f),
        IM_COL32(28, 30, 34, 255));
    for (float x = 0.0f; x < 1200.0f; x += 32.0f) {
        drawList->AddLine(
            ImVec2(origin.x + x, origin.y),
            ImVec2(origin.x + x, origin.y + 800.0f),
            IM_COL32(45, 48, 54, 255));
    }
    for (float y = 0.0f; y < 800.0f; y += 32.0f) {
        drawList->AddLine(
            ImVec2(origin.x, origin.y + y),
            ImVec2(origin.x + 1200.0f, origin.y + y),
            IM_COL32(45, 48, 54, 255));
    }

    for (const Node& target : nodes_) {
        const int inputs[2] = { target.inputA, target.inputB };
        for (int inputIndex = 0; inputIndex < 2; ++inputIndex) {
            if (inputs[inputIndex] == 0 ||
                (inputIndex == 1 &&
                 target.type != NodeType::Multiply &&
                 target.type != NodeType::Add)) {
                continue;
            }
            const Node* source = FindNode(inputs[inputIndex]);
            if (source == nullptr) {
                continue;
            }
            const ImVec2 start = GetOutputSocketPosition(
                source->x, source->y, origin.x, origin.y);
            const ImVec2 end = GetInputSocketPosition(
                target.x, target.y, inputIndex, origin.x, origin.y);
            drawList->AddBezierCubic(
                start,
                ImVec2(start.x + 70.0f, start.y),
                ImVec2(end.x - 70.0f, end.y),
                end,
                IM_COL32(95, 190, 255, 255),
                3.0f);
        }
    }

    if (const Node* draggingSource = FindNode(draggingSourceNodeId_)) {
        const ImVec2 start = GetOutputSocketPosition(
            draggingSource->x,
            draggingSource->y,
            origin.x,
            origin.y);
        const ImVec2 end = ImGui::GetMousePos();
        drawList->AddBezierCubic(
            start,
            ImVec2(start.x + 70.0f, start.y),
            ImVec2(end.x - 70.0f, end.y),
            end,
            IM_COL32(255, 205, 90, 255),
            3.0f);
    }

    for (Node& node : nodes_) {
        DrawNode(node, origin.x, origin.y);
        DrawNodeSockets(node, origin.x, origin.y);
    }

    const bool canvasFocused = ImGui::IsWindowFocused(
        ImGuiFocusedFlags_RootAndChildWindows);
    if (canvasFocused &&
        !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        pendingDeleteNodeId_ = selectedNodeId_;
    }
    if (draggingSourceNodeId_ != 0 && ImGui::IsMouseReleased(0)) {
        draggingSourceNodeId_ = 0;
        status_ = "接続をキャンセルしました。";
    }
    if (pendingDeleteNodeId_ != 0) {
        const int nodeId = pendingDeleteNodeId_;
        pendingDeleteNodeId_ = 0;
        DeleteNode(nodeId);
    }
    ImGui::SetCursorScreenPos(
        ImVec2(origin.x + 1200.0f, origin.y + 800.0f));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
    ImGui::EndChild();
    ImGui::End();
#endif
}

void ShaderGraphEditor::DrawNode(
    Node& node,
    float originX,
    float originY) {
#ifdef USE_IMGUI
    ImGui::PushID(node.id);
    const ImVec2 position(originX + node.x, originY + node.y);
    ImGui::SetCursorScreenPos(position);
    ImGui::BeginGroup();
    ImGui::InvisibleButton("Header", ImVec2(kNodeWidth, 28.0f));
    if (ImGui::IsItemClicked()) {
        selectedNodeId_ = node.id;
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        node.x = (std::max)(0.0f, node.x + delta.x);
        node.y = (std::max)(0.0f, node.y + delta.y);
    }
    if (ImGui::BeginPopupContextItem("NodeContext")) {
        selectedNodeId_ = node.id;
        ImGui::BeginDisabled(node.type == NodeType::Output);
        if (ImGui::MenuItem("ノードを削除###DeleteNode")) {
            pendingDeleteNodeId_ = node.id;
        }
        ImGui::EndDisabled();
        if (node.type == NodeType::Output) {
            ImGui::TextDisabled("出力ノードは必須です。");
        }
        ImGui::EndPopup();
    }
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        position,
        ImVec2(position.x + kNodeWidth, position.y + kNodeHeight),
        IM_COL32(54, 58, 66, 245),
        7.0f);
    drawList->AddRectFilled(
        position,
        ImVec2(position.x + kNodeWidth, position.y + 28.0f),
        IM_COL32(45, 110, 165, 255),
        7.0f);
    drawList->AddRect(
        position,
        ImVec2(position.x + kNodeWidth, position.y + kNodeHeight),
        selectedNodeId_ == node.id
            ? IM_COL32(255, 205, 90, 255)
            : IM_COL32(92, 98, 108, 255),
        7.0f,
        0,
        selectedNodeId_ == node.id ? 2.5f : 1.0f);
    drawList->AddText(
        ImVec2(position.x + 8.0f, position.y + 6.0f),
        IM_COL32_WHITE,
        node.name.c_str());

    ImGui::SetNextItemWidth(190.0f);
    if (node.type != NodeType::Texture &&
        node.type != NodeType::Multiply &&
        node.type != NodeType::Add &&
        node.type != NodeType::Output) {
        std::array<char, 64> name{};
        std::copy_n(
            node.name.data(),
            (std::min)(node.name.size(), name.size() - 1),
            name.data());
        if (ImGui::InputText("名前###NodeName", name.data(), name.size())) {
            node.name = name.data();
        }
    }
    if (node.type == NodeType::Color) {
        ImGui::ColorEdit4("値###ColorValue", node.value.data());
    } else if (node.type == NodeType::Float) {
        ImGui::DragFloat("値###FloatValue", &node.value[0], 0.01f);
    } else if (node.type == NodeType::Multiply ||
               node.type == NodeType::Add) {
        DrawInputSelector("A", node.id, node.inputA);
        DrawInputSelector("B", node.id, node.inputB);
    } else if (node.type == NodeType::Output) {
        DrawInputSelector("カラー###Color", node.id, node.inputA);
    } else {
        ImGui::TextDisabled("メインテクスチャ（t0）を使用します");
    }
    ImGui::EndGroup();
    ImGui::PopID();
#endif
}

void ShaderGraphEditor::DrawNodeSockets(
    Node& node,
    float originX,
    float originY) {
#ifdef USE_IMGUI
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImGui::PushID(node.id);

    if (node.type != NodeType::Output) {
        const ImVec2 center = GetOutputSocketPosition(
            node.x, node.y, originX, originY);
        ImGui::SetCursorScreenPos(ImVec2(
            center.x - kSocketRadius,
            center.y - kSocketRadius));
        ImGui::InvisibleButton(
            "OutputSocket",
            ImVec2(kSocketRadius * 2.0f, kSocketRadius * 2.0f));
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        if (ImGui::IsItemActivated()) {
            draggingSourceNodeId_ = node.id;
            selectedNodeId_ = node.id;
            status_ = "入力ソケットへドラッグしてください。";
        }
        drawList->AddCircleFilled(
            center,
            kSocketRadius,
            draggingSourceNodeId_ == node.id
                ? IM_COL32(255, 205, 90, 255)
                : (hovered
                    ? IM_COL32(150, 220, 255, 255)
                    : IM_COL32(95, 190, 255, 255)));
        drawList->AddCircle(
            center,
            kSocketRadius,
            IM_COL32(12, 16, 22, 255),
            16,
            1.5f);
    }

    const int inputCount =
        node.type == NodeType::Multiply || node.type == NodeType::Add
        ? 2
        : (node.type == NodeType::Output ? 1 : 0);
    for (int inputIndex = 0; inputIndex < inputCount; ++inputIndex) {
        int& sourceId = inputIndex == 0 ? node.inputA : node.inputB;
        const ImVec2 center = GetInputSocketPosition(
            node.x, node.y, inputIndex, originX, originY);
        ImGui::PushID(inputIndex);
        ImGui::SetCursorScreenPos(ImVec2(
            center.x - kSocketRadius,
            center.y - kSocketRadius));
        ImGui::InvisibleButton(
            "InputSocket",
            ImVec2(kSocketRadius * 2.0f, kSocketRadius * 2.0f));
        const ImVec2 mousePosition = ImGui::GetMousePos();
        const bool rawHovered =
            mousePosition.x >= center.x - kSocketRadius &&
            mousePosition.x <= center.x + kSocketRadius &&
            mousePosition.y >= center.y - kSocketRadius &&
            mousePosition.y <= center.y + kSocketRadius;
        const bool hovered =
            ImGui::IsItemHovered() ||
            (draggingSourceNodeId_ != 0 && rawHovered);
        if (hovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            sourceId = 0;
            status_ = "入力の接続を解除しました。";
        }
        if (hovered &&
            draggingSourceNodeId_ != 0 &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            std::vector<int> visited;
            if (draggingSourceNodeId_ == node.id ||
                DependsOn(
                    draggingSourceNodeId_,
                    node.id,
                    visited)) {
                status_ = "循環参照になるため接続できません。";
            } else {
                sourceId = draggingSourceNodeId_;
                status_ = "ノードを接続しました。";
            }
            draggingSourceNodeId_ = 0;
        }
        drawList->AddCircleFilled(
            center,
            kSocketRadius,
            hovered
                ? IM_COL32(255, 225, 135, 255)
                : (sourceId != 0
                    ? IM_COL32(95, 190, 255, 255)
                    : IM_COL32(115, 122, 134, 255)));
        drawList->AddCircle(
            center,
            kSocketRadius,
            IM_COL32(12, 16, 22, 255),
            16,
            1.5f);
        ImGui::PopID();
    }
    ImGui::PopID();
#else
    (void)node;
    (void)originX;
    (void)originY;
#endif
}

void ShaderGraphEditor::DrawInputSelector(
    const char* label,
    int targetNodeId,
    int& sourceId) {
#ifdef USE_IMGUI
    const Node* selected = FindNode(sourceId);
    const char* preview = selected != nullptr
        ? selected->name.c_str() : "（なし）";
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::BeginCombo(label, preview)) {
        for (const Node& source : nodes_) {
            if (source.type == NodeType::Output) {
                continue;
            }
            std::vector<int> visited;
            const bool wouldCreateCycle =
                source.id == targetNodeId ||
                DependsOn(source.id, targetNodeId, visited);
            ImGui::BeginDisabled(wouldCreateCycle);
            if (ImGui::Selectable(
                    std::format("{}##{}", source.name, source.id).c_str(),
                    source.id == sourceId)) {
                sourceId = source.id;
                status_ = "ノードを接続しました。";
            }
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
#else
    (void)label;
    (void)targetNodeId;
    (void)sourceId;
#endif
}

const ShaderGraphEditor::Node* ShaderGraphEditor::FindNode(int nodeId) const {
    const auto node = std::ranges::find_if(
        nodes_,
        [nodeId](const Node& value) { return value.id == nodeId; });
    return node != nodes_.end() ? &*node : nullptr;
}

bool ShaderGraphEditor::DependsOn(
    int nodeId,
    int dependencyId,
    std::vector<int>& visited) const {
    if (nodeId == dependencyId) {
        return true;
    }
    if (std::ranges::find(visited, nodeId) != visited.end()) {
        return false;
    }
    visited.push_back(nodeId);
    const Node* node = FindNode(nodeId);
    if (node == nullptr) {
        return false;
    }
    return
        (node->inputA != 0 &&
         DependsOn(node->inputA, dependencyId, visited)) ||
        (node->inputB != 0 &&
         DependsOn(node->inputB, dependencyId, visited));
}

bool ShaderGraphEditor::LoadGraph(
    const std::filesystem::path& path) {
    std::string json;
    if (!ReadTextFile(path, json)) {
        status_ = "シェーダーグラフファイルを開けませんでした。";
        return false;
    }

    std::string graphName;
    if (!ReadStringField(json, "name", graphName) ||
        graphName.empty()) {
        status_ = "シェーダーグラフJSONには名前が必要です。";
        return false;
    }

    const std::vector<std::string> nodeObjects =
        ExtractObjectArray(json, "nodes");
    if (nodeObjects.empty() || nodeObjects.size() > 512) {
        status_ = "シェーダーグラフのノード数は1～512個である必要があります。";
        return false;
    }

    std::vector<Node> loadedNodes;
    loadedNodes.reserve(nodeObjects.size());
    std::set<int> nodeIds;
    int maximumNodeId = 0;
    int outputCount = 0;
    for (const std::string& object : nodeObjects) {
        Node node{};
        std::string typeName;
        if (!ReadIntField(object, "id", node.id) ||
            !ReadStringField(object, "type", typeName) ||
            !ReadStringField(object, "name", node.name) ||
            !ReadFloatField(object, "x", node.x) ||
            !ReadFloatField(object, "y", node.y) ||
            !ReadIntField(object, "inputA", node.inputA) ||
            !ReadIntField(object, "inputB", node.inputB) ||
            !TryParseNodeType(typeName, node.type)) {
            status_ = "シェーダーグラフに不正なノードがあります。";
            return false;
        }
        if (node.id <= 0 ||
            node.id >= 1000000 ||
            !nodeIds.insert(node.id).second) {
            status_ = "重複または不正なノードIDがあります。";
            return false;
        }
        if (node.inputA < 0 || node.inputB < 0) {
            status_ = "接続IDに負の値があります。";
            return false;
        }
        node.x = (std::max)(0.0f, node.x);
        node.y = (std::max)(0.0f, node.y);
        const std::vector<float> values =
            ReadFloatArrayField(object, "value");
        for (size_t index = 0;
            index < values.size() && index < node.value.size();
            ++index) {
            node.value[index] = values[index];
        }
        if (node.type == NodeType::Output) {
            ++outputCount;
        }
        maximumNodeId = (std::max)(maximumNodeId, node.id);
        loadedNodes.push_back(std::move(node));
    }

    if (outputCount != 1) {
        status_ = "出力ノードは1つだけ必要です。";
        return false;
    }
    for (const Node& node : loadedNodes) {
        const int inputs[2] = { node.inputA, node.inputB };
        for (const int sourceId : inputs) {
            if (sourceId == 0) {
                continue;
            }
            const auto source = std::ranges::find_if(
                loadedNodes,
                [sourceId](const Node& value) {
                    return value.id == sourceId;
                });
            if (source == loadedNodes.end() ||
                source->type == NodeType::Output) {
                status_ =
                    "不足または不正な接続があります。";
                return false;
            }
        }
    }

    std::vector<Node> previousNodes = std::move(nodes_);
    nodes_ = std::move(loadedNodes);
    bool valid = true;
    std::vector<int> recursionStack;
    const auto outputNode = std::ranges::find_if(
        nodes_,
        [](const Node& node) { return node.type == NodeType::Output; });
    BuildExpression(outputNode->id, recursionStack, valid);
    if (!valid) {
        nodes_ = std::move(previousNodes);
        status_ = "循環参照または必須入力の不足があるため、"
            "グラフを読み込めませんでした。";
        return false;
    }

    nextNodeId_ = maximumNodeId + 1;
    selectedNodeId_ = 0;
    draggingSourceNodeId_ = 0;
    pendingDeleteNodeId_ = 0;
    graphName_.fill('\0');
    std::copy_n(
        graphName.data(),
        (std::min)(graphName.size(), graphName_.size() - 1),
        graphName_.data());
    status_ = "シェーダーグラフを読み込みました: " + path.filename().string();
    return true;
}

std::string ShaderGraphEditor::BuildExpression(
    int nodeId,
    std::vector<int>& recursionStack,
    bool& valid) const {
    const Node* node = FindNode(nodeId);
    if (node == nullptr ||
        std::ranges::find(recursionStack, nodeId) != recursionStack.end()) {
        valid = false;
        return "float4(1, 0, 1, 1)";
    }
    recursionStack.push_back(nodeId);
    std::string expression;
    switch (node->type) {
    case NodeType::Texture:
        expression = "sampledTexture";
        break;
    case NodeType::Color:
        expression = std::format("gParameter{}", node->id);
        break;
    case NodeType::Float:
        expression = std::format("gParameter{}.xxxx", node->id);
        break;
    case NodeType::Multiply:
        expression = "(" +
            BuildExpression(node->inputA, recursionStack, valid) + " * " +
            BuildExpression(node->inputB, recursionStack, valid) + ")";
        break;
    case NodeType::Add:
        expression = "(" +
            BuildExpression(node->inputA, recursionStack, valid) + " + " +
            BuildExpression(node->inputB, recursionStack, valid) + ")";
        break;
    case NodeType::Output:
        expression = BuildExpression(node->inputA, recursionStack, valid);
        break;
    }
    recursionStack.pop_back();
    return expression;
}

bool ShaderGraphEditor::SaveAndCompile() {
    const std::string graphName = SanitizeName(graphName_.data());
    if (graphName.empty() || graphics_ == nullptr) {
        status_ = "グラフ名が空、またはGraphicsを利用できません。";
        return false;
    }
    const auto outputNode = std::ranges::find_if(
        nodes_,
        [](const Node& node) { return node.type == NodeType::Output; });
    if (outputNode == nodes_.end()) {
        status_ = "出力ノードが必要です。";
        return false;
    }
    bool valid = true;
    std::vector<int> recursionStack;
    const std::string expression =
        BuildExpression(outputNode->id, recursionStack, valid);
    if (!valid) {
        status_ = "入力不足または循環参照があります。";
        return false;
    }

    const std::filesystem::path directory =
        "Resources/Shaders/Generated";
    std::error_code directoryError;
    std::filesystem::create_directories(directory, directoryError);
    if (directoryError) {
        status_ = "生成シェーダーディレクトリを作成できませんでした。";
        return false;
    }

    std::ostringstream constants;
    std::ostringstream jsonParameters;
    uint32_t offset = 0;
    bool firstParameter = true;
    for (const Node& node : nodes_) {
        if (node.type != NodeType::Color &&
            node.type != NodeType::Float) {
            continue;
        }
        if (node.type == NodeType::Color) {
            constants << std::format(
                "    float4 gParameter{}; // offset {}\n",
                node.id,
                offset);
        } else {
            constants << std::format(
                "    float gParameter{}; float3 gParameterPadding{};"
                " // offset {}\n",
                node.id,
                node.id,
                offset);
        }
        if (!firstParameter) {
            jsonParameters << ",\n";
        }
        firstParameter = false;
        jsonParameters << std::format(
            "    {{ \"name\": \"{}\", \"type\": \"{}\", "
            "\"offset\": {}, \"default\": {} }}",
            SanitizeName(node.name.c_str()),
            node.type == NodeType::Color ? "color" : "float",
            offset,
            node.type == NodeType::Color
            ? std::format(
                "[{}, {}, {}, {}]",
                node.value[0], node.value[1],
                node.value[2], node.value[3])
            : std::format("{}", node.value[0]));
        offset += 16;
    }
    if (offset > ShaderManager::kMaterialParameterBufferSize) {
        status_ = "マテリアルパラメーターが256バイトを超えています。";
        return false;
    }

    const auto hlslPath =
        directory / (graphName + ".generated.PS.hlsl");
    const auto shaderJsonPath =
        directory / (graphName + ".shader.json");
    const auto graphJsonPath =
        directory / (graphName + ".shadergraph.json");
    std::ofstream hlsl(hlslPath);
    hlsl <<
        "#include \"../../../Object3d.hlsli\"\n"
        "Texture2D<float4> gTexture : register(t0);\n"
        "SamplerState gSampler : register(s0);\n"
        "cbuffer CustomMaterialParameters : register(b3)\n{\n" <<
        constants.str() <<
        "}\n"
        "float4 main(VertexShaderOutput input) : SV_TARGET0\n{\n"
        "    float4 transformedUV = mul(float4(input.texcoord, 0, 1), "
        "gUVTransform);\n"
        "    float4 sampledTexture = gTexture.Sample(gSampler, "
        "transformedUV.xy) * gMaterialColor;\n"
        "    return " << expression << ";\n"
        "}\n";
    std::ofstream shaderJson(shaderJsonPath);
    shaderJson << std::format(
        "{{\n  \"name\": \"{}\",\n"
        "  \"vertexShader\": \"../../../Object3d.VS.hlsl\",\n"
        "  \"pixelShader\": \"{}.generated.PS.hlsl\",\n"
        "  \"blend\": \"Alpha\",\n"
        "  \"cull\": \"Back\",\n"
        "  \"depthTest\": true,\n"
        "  \"depthWrite\": true,\n"
        "  \"parameters\": [\n{}\n  ]\n}}\n",
        graphName,
        graphName,
        jsonParameters.str());
    std::ofstream graphJson(graphJsonPath);
    graphJson << "{\n  \"version\": 1,\n  \"name\": \""
        << graphName << "\",\n  \"nodes\": [\n";
    for (size_t index = 0; index < nodes_.size(); ++index) {
        const Node& node = nodes_[index];
        graphJson << std::format(
            "    {{ \"id\": {}, \"type\": \"{}\", \"name\": \"{}\", "
            "\"x\": {}, \"y\": {}, \"value\": [{}, {}, {}, {}], "
            "\"inputA\": {}, \"inputB\": {} }}{}",
            node.id,
            GetNodeTypeName(node.type),
            SanitizeName(node.name.c_str()),
            node.x,
            node.y,
            node.value[0],
            node.value[1],
            node.value[2],
            node.value[3],
            node.inputA,
            node.inputB,
            index + 1 < nodes_.size() ? ",\n" : "\n");
    }
    graphJson << "  ]\n}\n";
    hlsl.close();
    shaderJson.close();
    graphJson.close();
    if (!hlsl || !shaderJson || !graphJson) {
        status_ = "シェーダーグラフファイルの書き込みに失敗しました。";
        return false;
    }
    if (!graphics_->GetShaderManager()->LoadShaderDefinition(
            shaderJsonPath,
            std::cerr,
            true)) {
        status_ = graphics_->GetShaderManager()->GetLastError();
        return false;
    }
    status_ = "保存・コンパイルしました: " + graphName;
    return true;
}
