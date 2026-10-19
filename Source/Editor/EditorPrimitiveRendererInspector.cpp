#include "Editor.h"
#include "AssetManager.h"
#include "EditorInspectorUtilities.h"
#include "PrimitiveRendererComponent.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"

using namespace EditorInspectorUtilities;

void Editor::DrawPrimitiveRendererInspector(PrimitiveRendererComponent& primitiveRenderer) {
    int primitiveType = primitiveRenderer.GetPrimitiveType() ==
        PrimitiveRendererComponent::PrimitiveType::Triangle ? 0 : 1;
    const char* types[] = { "三角形", "球" };
    if (ImGui::Combo("プリミティブ###Primitive", &primitiveType, types, 2)) {
        primitiveRenderer.SetPrimitiveType(
            primitiveType == 0
            ? PrimitiveRendererComponent::PrimitiveType::Triangle
            : PrimitiveRendererComponent::PrimitiveType::Sphere);
    }
    DrawColor(primitiveRenderer.GetColor());
    DrawUVTransform(primitiveRenderer.GetUVTransform());
    const std::string texturePath = assetManager_ != nullptr
        ? assetManager_->GetAssetPath(primitiveRenderer.GetTextureGuid())
        : std::string{};
    ImGui::TextWrapped(
        "テクスチャGUID: %s", primitiveRenderer.GetTextureGuid().c_str());
    ImGui::TextWrapped("テクスチャ: %s", texturePath.c_str());
    if (ImGui::Button("テクスチャを選択...###SelectTexture")) {
        const std::string path = OpenAssetFileDialog(AssetType::Texture);
        if (!path.empty() && assetManager_ != nullptr) {
            std::string error;
            const AssetGuid guid = assetManager_->ImportTexture(path, &error);
            const int handle = guid.empty()
                ? -1 : assetManager_->LoadTexture(guid, &error);
            if (handle >= 0) {
                primitiveRenderer.SetTextureAsset(handle, guid);
                lastMessage_ = "テクスチャアセットを変更しました。";
            } else {
                lastMessage_ = error;
            }
        }
    }
}
#endif
