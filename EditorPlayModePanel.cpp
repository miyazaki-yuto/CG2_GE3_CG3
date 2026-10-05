#include "Editor.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"

void Editor::DrawPlayModeToolbar(
    bool isPlaying,
    const std::string& playModeMessage) {
    ImGui::SetNextWindowSize(ImVec2(360.0f, 120.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin(
        "プレイモード###Play Mode",
        nullptr,
        ImGuiWindowFlags_NoCollapse);

    if (!isPlaying) {
        if (ImGui::Button("再生###Play")) {
            playModeRequest_ = PlayModeRequest::Start;
        }
        ImGui::SameLine();
        ImGui::TextColored(
            ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "編集モード");
        ImGui::TextDisabled("コンポーネントとノードは更新されません。");
    } else {
        if (ImGui::Button("停止###Stop")) {
            playModeRequest_ = PlayModeRequest::Stop;
        }
        ImGui::SameLine();
        ImGui::TextColored(
            ImVec4(0.35f, 1.0f, 0.45f, 1.0f), "プレイモード");
        ImGui::TextDisabled("停止すると実行中の変更は破棄されます。");
    }

    if (!playModeMessage.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", playModeMessage.c_str());
    }
    ImGui::End();
}

#endif
