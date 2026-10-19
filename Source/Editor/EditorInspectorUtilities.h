#pragma once

#include "AssetManager.h"
#include "CommonTypes.h"

#include <string>

namespace EditorInspectorUtilities {

void DrawColor(Vector4& color);
void DrawUVTransform(UVTransform& uvTransform);
std::string OpenAssetFileDialog(AssetType type);
std::string OpenPrefabSaveDialog();

} // namespace EditorInspectorUtilities
