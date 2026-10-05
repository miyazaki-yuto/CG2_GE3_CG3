#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

enum class BlendMode : uint8_t {
    None,
    Normal,
    Add,
    Subtract,
    Multiply,
    Screen,
    Count
};

inline constexpr size_t kBlendModeCount =
    static_cast<size_t>(BlendMode::Count);

inline constexpr size_t GetBlendModeIndex(BlendMode mode) {
    return static_cast<size_t>(mode);
}

inline constexpr const char* GetBlendModeName(BlendMode mode) {
    switch (mode) {
    case BlendMode::None: return "None";
    case BlendMode::Normal: return "Normal";
    case BlendMode::Add: return "Add";
    case BlendMode::Subtract: return "Subtract";
    case BlendMode::Multiply: return "Multiply";
    case BlendMode::Screen: return "Screen";
    case BlendMode::Count: break;
    }
    return "Normal";
}

inline bool TryParseBlendMode(std::string_view name, BlendMode& mode) {
    if (name == "None" || name == "Opaque") {
        mode = BlendMode::None;
    } else if (name == "Normal" || name == "Alpha") {
        mode = BlendMode::Normal;
    } else if (name == "Add" || name == "Additive") {
        mode = BlendMode::Add;
    } else if (name == "Subtract") {
        mode = BlendMode::Subtract;
    } else if (name == "Multiply") {
        mode = BlendMode::Multiply;
    } else if (name == "Screen") {
        mode = BlendMode::Screen;
    } else {
        return false;
    }
    return true;
}
