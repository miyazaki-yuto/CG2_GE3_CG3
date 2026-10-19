#include "EditorInspectorUtilities.h"

#include <array>
#include <filesystem>

#ifdef USE_IMGUI
#include <Windows.h>
#include <commdlg.h>
#include "externals/imgui/imgui.h"
#pragma comment(lib, "Comdlg32.lib")
#endif

namespace EditorInspectorUtilities {

void DrawColor(Vector4& color) {
#ifdef USE_IMGUI
    ImGui::ColorEdit4("カラー###Color", &color.x);
#else
    static_cast<void>(color);
#endif
}

void DrawUVTransform(UVTransform& uvTransform) {
#ifdef USE_IMGUI
    if (ImGui::TreeNode("UVトランスフォーム###UVTransform")) {
        ImGui::DragFloat2(
            "UVスケール###UVScale", &uvTransform.scale.x, 0.01f, 0.001f, 100.0f);
        ImGui::SliderFloat(
            "UV回転###UVRotation", &uvTransform.rotate, -3.141592f, 3.141592f);
        ImGui::DragFloat2(
            "UV位置###UVPosition", &uvTransform.translate.x,
            0.01f, -100.0f, 100.0f);
        ImGui::TreePop();
    }
#else
    static_cast<void>(uvTransform);
#endif
}

std::string OpenAssetFileDialog(AssetType type) {
#ifdef USE_IMGUI
    std::array<wchar_t, 32768> selectedPath{};
    const wchar_t modelFilter[] =
        L"3Dモデル (*.obj;*.gltf;*.glb)\0*.obj;*.gltf;*.glb\0"
        L"Wavefront OBJ (*.obj)\0*.obj\0"
        L"glTF 2.0 (*.gltf;*.glb)\0*.gltf;*.glb\0"
        L"すべてのファイル (*.*)\0*.*\0";
    const wchar_t textureFilter[] =
        L"テクスチャ (*.png;*.jpg;*.bmp;*.dds)\0"
        L"*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.dds\0"
        L"すべてのファイル (*.*)\0*.*\0";
    const wchar_t prefabFilter[] =
        L"CG2 Prefab (*.prefab)\0*.prefab\0"
        L"すべてのファイル (*.*)\0*.*\0";

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = selectedPath.data();
    dialog.nMaxFile = static_cast<DWORD>(selectedPath.size());
    dialog.lpstrFilter = type == AssetType::Model
        ? modelFilter
        : (type == AssetType::Texture ? textureFilter : prefabFilter);
    dialog.nFilterIndex = 1;
    dialog.lpstrInitialDir = L"Resources";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog) == FALSE) {
        return {};
    }
    const std::u8string utf8 =
        std::filesystem::path(selectedPath.data()).generic_u8string();
    return std::string(
        reinterpret_cast<const char*>(utf8.data()), utf8.size());
#else
    static_cast<void>(type);
    return {};
#endif
}

std::string OpenPrefabSaveDialog() {
#ifdef USE_IMGUI
    std::array<wchar_t, 32768> selectedPath{};
    const wchar_t filter[] =
        L"CG2 Prefab (*.prefab)\0*.prefab\0"
        L"すべてのファイル (*.*)\0*.*\0";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = selectedPath.data();
    dialog.nMaxFile = static_cast<DWORD>(selectedPath.size());
    dialog.lpstrFilter = filter;
    dialog.lpstrDefExt = L"prefab";
    dialog.lpstrInitialDir = L"Resources\\Prefabs";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&dialog) == FALSE) {
        return {};
    }
    const std::u8string utf8 =
        std::filesystem::path(selectedPath.data()).generic_u8string();
    return std::string(
        reinterpret_cast<const char*>(utf8.data()), utf8.size());
#else
    return {};
#endif
}

} // namespace EditorInspectorUtilities
