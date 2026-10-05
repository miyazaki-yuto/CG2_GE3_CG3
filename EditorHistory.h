#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class Scene;
class SceneSerializer;

// Scene全体のJSONスナップショットを使ったEditor用Undo/Redo。
// UI状態は所有せず、Sceneと選択IDの復元だけを担当する。
class EditorHistory {
public:
    void Initialize(SceneSerializer* serializer);

    bool Capture(
        const Scene& scene,
        std::string& snapshot,
        std::string& resultMessage) const;
    bool Commit(
        const Scene& scene,
        std::string label,
        std::string beforeSnapshot,
        uint64_t selectionBefore,
        uint64_t selectionAfter,
        std::string& resultMessage);
    bool Undo(
        Scene& scene,
        uint64_t& selectedGameObjectId,
        std::string& resultMessage);
    bool Redo(
        Scene& scene,
        uint64_t& selectedGameObjectId,
        std::string& resultMessage);

    void Clear();
    bool CanUndo() const { return cursor_ > 0; }
    bool CanRedo() const { return cursor_ < entries_.size(); }

private:
    struct Entry {
        std::string label;
        std::string beforeSnapshot;
        std::string afterSnapshot;
        uint64_t selectionBefore = 0;
        uint64_t selectionAfter = 0;
    };

    SceneSerializer* serializer_ = nullptr;
    std::vector<Entry> entries_;
    size_t cursor_ = 0;
};
