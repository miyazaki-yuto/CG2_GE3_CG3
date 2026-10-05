#include "Editor.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_internal.h"

void Editor::DrawDockSpace() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("レイアウト###Layout")) {
            if (ImGui::MenuItem("標準レイアウトに戻す###ResetToDefaultLayout")) {
                resetDockLayoutRequested_ = true;
            }
            ImGui::Separator();
            ImGui::TextDisabled("タブをドラッグすると、エディター内で独立表示できます。");
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags hostWindowFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("CG2 エディター###CG2 Editor DockSpace", nullptr, hostWindowFlags);
    ImGui::PopStyleVar(3);

    const ImGuiID dockSpaceId = ImGui::GetID("CG2EditorDockSpaceV2");
    if (resetDockLayoutRequested_ ||
        ImGui::DockBuilderGetNode(dockSpaceId) == nullptr) {
        resetDockLayoutRequested_ = false;
        ImGui::DockBuilderRemoveNode(dockSpaceId);
        ImGui::DockBuilderAddNode(
            dockSpaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockSpaceId, viewport->WorkSize);

        ImGuiID centerDockId = dockSpaceId;
        ImGuiID leftDockId = 0;
        ImGuiID rightDockId = 0;
        ImGuiID topDockId = 0;
        ImGuiID bottomDockId = 0;
        ImGui::DockBuilderSplitNode(
            centerDockId,
            ImGuiDir_Left,
            0.20f,
            &leftDockId,
            &centerDockId);
        ImGui::DockBuilderSplitNode(
            centerDockId,
            ImGuiDir_Right,
            0.25f,
            &rightDockId,
            &centerDockId);
        ImGui::DockBuilderSplitNode(
            centerDockId,
            ImGuiDir_Up,
            0.14f,
            &topDockId,
            &centerDockId);
        ImGui::DockBuilderSplitNode(
            centerDockId,
            ImGuiDir_Down,
            0.25f,
            &bottomDockId,
            &centerDockId);

        ImGui::DockBuilderDockWindow("Hierarchy", leftDockId);
        ImGui::DockBuilderDockWindow("Inspector", rightDockId);
        ImGui::DockBuilderDockWindow("Play Mode", topDockId);
        ImGui::DockBuilderDockWindow("Lighting", bottomDockId);
        ImGui::DockBuilderDockWindow("Sound Control", bottomDockId);
        ImGui::DockBuilderDockWindow("Scene View", centerDockId);
        ImGui::DockBuilderDockWindow("Game View", centerDockId);
        ImGui::DockBuilderFinish(dockSpaceId);
    }

    ImGui::DockSpace(
        dockSpaceId,
        ImVec2(0.0f, 0.0f),
        ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
}

#endif
