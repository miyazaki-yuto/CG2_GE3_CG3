#include "Editor.h"
#include "AssetManager.h"
#include "EditorInspectorUtilities.h"
#include "SpriteRendererComponent.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"

using namespace EditorInspectorUtilities;

void Editor::DrawSpriteRendererInspector(SpriteRendererComponent& spriteRenderer) {
    DrawColor(spriteRenderer.GetColor());
    DrawUVTransform(spriteRenderer.GetUVTransform());
    const std::string texturePath = assetManager_ != nullptr
        ? assetManager_->GetAssetPath(spriteRenderer.GetTextureGuid())
        : std::string{};
    ImGui::TextWrapped(
        "テクスチャGUID: %s", spriteRenderer.GetTextureGuid().c_str());
    ImGui::TextWrapped("テクスチャ: %s", texturePath.c_str());
    if (ImGui::Button("テクスチャを選択...###SelectTexture")) {
        const std::string path = OpenAssetFileDialog(AssetType::Texture);
        if (!path.empty() && assetManager_ != nullptr) {
            std::string error;
            const AssetGuid guid = assetManager_->ImportTexture(path, &error);
            const int handle = guid.empty()
                ? -1 : assetManager_->LoadTexture(guid, &error);
            if (handle >= 0) {
                spriteRenderer.SetTextureAsset(handle, guid);
                lastMessage_ = "テクスチャアセットを変更しました。";
            } else {
                lastMessage_ = error;
            }
        }
    }
}
#endif
