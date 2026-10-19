#include "EditorHistory.h"

#include "Scene.h"
#include "SceneSerializer.h"

#include <cstddef>
#include <utility>

void EditorHistory::Initialize(SceneSerializer* serializer) {
    serializer_ = serializer;
    Clear();
}

bool EditorHistory::Capture(
    const Scene& scene,
    std::string& snapshot,
    std::string& resultMessage) const {
    if (serializer_ == nullptr) {
        resultMessage = "履歴用のシリアライザーを利用できません。";
        return false;
    }
    if (!serializer_->SerializeToString(scene, snapshot, resultMessage)) {
        resultMessage = "履歴の取得に失敗しました: " + resultMessage;
        return false;
    }
    return true;
}

bool EditorHistory::Commit(
    const Scene& scene,
    std::string label,
    std::string beforeSnapshot,
    uint64_t selectionBefore,
    uint64_t selectionAfter,
    std::string& resultMessage) {
    if (beforeSnapshot.empty()) {
        return false;
    }
    std::string afterSnapshot;
    if (!Capture(scene, afterSnapshot, resultMessage) ||
        beforeSnapshot == afterSnapshot) {
        return false;
    }
    if (cursor_ < entries_.size()) {
        entries_.erase(
            entries_.begin() + static_cast<std::ptrdiff_t>(cursor_),
            entries_.end());
    }
    entries_.push_back({
        std::move(label),
        std::move(beforeSnapshot),
        std::move(afterSnapshot),
        selectionBefore,
        selectionAfter
    });
    constexpr size_t kMaximumHistoryCount = 50;
    if (entries_.size() > kMaximumHistoryCount) {
        entries_.erase(entries_.begin());
    }
    cursor_ = entries_.size();
    resultMessage.clear();
    return true;
}

bool EditorHistory::Undo(
    Scene& scene,
    uint64_t& selectedGameObjectId,
    std::string& resultMessage) {
    if (!CanUndo()) {
        resultMessage = "元に戻せる操作がありません。";
        return false;
    }
    const Entry& entry = entries_[cursor_ - 1];
    std::string serializerMessage;
    if (serializer_ == nullptr ||
        !serializer_->DeserializeFromString(
            scene, entry.beforeSnapshot, serializerMessage)) {
        resultMessage = "元に戻す操作に失敗しました: " + serializerMessage;
        return false;
    }
    --cursor_;
    selectedGameObjectId =
        scene.FindGameObject(entry.selectionBefore) != nullptr
        ? entry.selectionBefore
        : 0;
    resultMessage = "元に戻しました: " + entry.label;
    return true;
}

bool EditorHistory::Redo(
    Scene& scene,
    uint64_t& selectedGameObjectId,
    std::string& resultMessage) {
    if (!CanRedo()) {
        resultMessage = "やり直せる操作がありません。";
        return false;
    }
    const Entry& entry = entries_[cursor_];
    std::string serializerMessage;
    if (serializer_ == nullptr ||
        !serializer_->DeserializeFromString(
            scene, entry.afterSnapshot, serializerMessage)) {
        resultMessage = "やり直しに失敗しました: " + serializerMessage;
        return false;
    }
    ++cursor_;
    selectedGameObjectId =
        scene.FindGameObject(entry.selectionAfter) != nullptr
        ? entry.selectionAfter
        : 0;
    resultMessage = "やり直しました: " + entry.label;
    return true;
}

void EditorHistory::Clear() {
    entries_.clear();
    cursor_ = 0;
}
