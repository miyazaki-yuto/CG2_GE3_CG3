#pragma once

#include "GameObject.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// GameObjectの生成・検索・更新・破棄をまとめて管理するゲーム空間。
class Scene {
public:
    explicit Scene(std::string name = "Untitled Scene");
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    const std::string& GetName() const { return name_; }
    void SetName(const std::string& name) { name_ = name; }

    GameObject& CreateGameObject(const std::string& name = "GameObject");
    // Scene読み込み専用。保存されていたIDを維持してGameObjectを生成する。
    // IDが0または重複している場合はnullptrを返す。
    GameObject* CreateGameObjectWithId(
        GameObject::Id id,
        const std::string& name = "GameObject");
    void DestroyGameObject(GameObject& gameObject);
    void DestroyGameObject(GameObject::Id id);
    // EditorやPrefab更新など、Scene::Updateの外側で安全に即時削除したい場合に使う。
    void DestroyGameObjectImmediate(GameObject& gameObject);

    // 別Sceneの階層をこのSceneへ移し、全GameObjectへ新しいIDを割り当てる。
    // forcedRootIdを指定するとPrefab更新前後でルートIDを維持できる。
    GameObject* AppendHierarchy(
        Scene&& sourceScene,
        GameObject::Id sourceRootId,
        GameObject* parent = nullptr,
        GameObject::Id forcedRootId = 0);

    GameObject* FindGameObject(GameObject::Id id) const;
    GameObject* FindGameObject(const std::string& name) const;

    // Componentと将来のVisual ScriptノードはPlay Sceneだけで更新する。
    void SetRuntimeUpdateEnabled(bool enabled) {
        runtimeUpdateEnabled_ = enabled;
    }
    bool IsRuntimeUpdateEnabled() const { return runtimeUpdateEnabled_; }

    // Scene複製中にLightなどの外部Manager登録を二重化しないための処理。
    // Component自体のenabled設定は変更しない。
    void SuspendRuntimeCallbacks();
    void ResumeRuntimeCallbacks();

    void Update(float deltaTime);
    // 有効なRendererComponentを集め、RenderOrder順に描画する。
    void Render();
    void RenderShadow(const Matrix4x4& lightViewProjection);
    void Clear();

    // 読み込みに成功した一時Sceneと中身を交換する。
    // 先に一時Sceneを完成させることで、JSONエラー時は現在のSceneを維持できる。
    void ReplaceWith(Scene&& scene);

    const std::vector<std::unique_ptr<GameObject>>& GetGameObjects() const {
        return gameObjects_;
    }

private:
    void RemoveDestroyedGameObjects();

    std::string name_;
    GameObject::Id nextGameObjectId_ = 1;
    std::vector<std::unique_ptr<GameObject>> gameObjects_;
    bool runtimeUpdateEnabled_ = false;
    bool runtimeCallbacksSuspended_ = false;
};
