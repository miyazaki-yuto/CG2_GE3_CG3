#include "Editor.h"
#include "PrefabInstanceComponent.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"

void Editor::DrawPrefabInstanceInspector(PrefabInstanceComponent& prefab) {
    ImGui::TextWrapped(
        "Prefab GUID: %s", prefab.GetPrefabGuid().c_str());
    bool autoUpdate = prefab.IsAutoUpdateEnabled();
    if (ImGui::Checkbox("ファイルから自動更新###AutoUpdateFromFile", &autoUpdate)) {
        prefab.SetAutoUpdateEnabled(autoUpdate);
    }
}
#endif
