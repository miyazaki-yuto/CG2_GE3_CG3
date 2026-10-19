#include "Editor.h"
#include "RendererComponent.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"

void Editor::DrawRendererInspector(RendererComponent& renderer) {
    int renderOrder = renderer.GetRenderOrder();
    if (ImGui::InputInt("描画順###RenderOrder", &renderOrder)) {
        renderer.SetRenderOrder(renderOrder);
    }
    constexpr const char* kBlendModeNames[] = {
        "なし", "通常", "加算", "減算", "乗算", "スクリーン"
    };
    int blendMode = static_cast<int>(renderer.GetBlendMode());
    if (ImGui::Combo(
        "ブレンドモード###BlendMode",
        &blendMode,
        kBlendModeNames,
        static_cast<int>(kBlendModeCount))) {
        renderer.SetBlendMode(static_cast<BlendMode>(blendMode));
    }
}
#endif
