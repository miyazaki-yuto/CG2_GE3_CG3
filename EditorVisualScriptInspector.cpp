#include "Editor.h"

#include "VisualScriptComponent.h"

#include <algorithm>
#include <array>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"

void Editor::DrawVisualScriptInspector(VisualScriptComponent& script) {
    std::array<char, 260> path{};
    const std::string& currentPath = script.GetScriptPath();
    std::copy_n(
        currentPath.data(),
        (std::min)(currentPath.size(), path.size() - 1),
        path.data());
    ImGui::TextUnformatted("ファイル");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("###VisualScriptPath", path.data(), path.size())) {
        script.SetScriptPath(path.data());
    }
    if (ImGui::Button("保存###SaveVisualScriptInspector")) {
        lastMessage_ = script.SaveGraph()
            ? "Visual Scriptを保存しました。"
            : script.GetLastError();
    }
    ImGui::SameLine();
    if (ImGui::Button("読み込み###LoadVisualScriptInspector")) {
        lastMessage_ = script.LoadGraph()
            ? "Visual Scriptを読み込みました。"
            : script.GetLastError();
    }
    ImGui::TextDisabled(
        "ノード数: %zu / 前回実行: %d",
        script.GetGraph().GetNodes().size(),
        script.GetLastExecutionCount());
    if (!script.GetLastError().empty()) {
        ImGui::TextWrapped("エラー: %s", script.GetLastError().c_str());
    }
}

#else

void Editor::DrawVisualScriptInspector(VisualScriptComponent&) {}

#endif
