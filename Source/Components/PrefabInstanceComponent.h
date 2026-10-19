#pragma once

#include "Component.h"

#include <string>
#include <utility>

// Prefabから生成された階層のルートにだけ付けるComponent。
// SceneはファイルパスではなくPrefab GUIDを保存する。
class PrefabInstanceComponent final : public Component {
public:
    explicit PrefabInstanceComponent(
        std::string prefabGuid = {},
        bool autoUpdate = true)
        : prefabGuid_(std::move(prefabGuid)),
          autoUpdate_(autoUpdate) {
    }

    const std::string& GetPrefabGuid() const { return prefabGuid_; }
    void SetPrefabGuid(std::string prefabGuid) {
        prefabGuid_ = std::move(prefabGuid);
    }

    bool IsAutoUpdateEnabled() const { return autoUpdate_; }
    void SetAutoUpdateEnabled(bool enabled) { autoUpdate_ = enabled; }

    // Scene保存対象外の実行時フラグ。Scene読み込み直後はfalseなので、
    // アプリを閉じている間にPrefabが変わっていても最初のUpdateで同期できる。
    bool IsRuntimeSynchronized() const { return runtimeSynchronized_; }
    void MarkRuntimeSynchronized() { runtimeSynchronized_ = true; }

private:
    std::string prefabGuid_;
    bool autoUpdate_ = true;
    bool runtimeSynchronized_ = false;
};
