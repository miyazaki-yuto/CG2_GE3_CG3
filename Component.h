#pragma once

class GameObject;

// GameObjectへ機能を追加するための基底クラス。
// 将来のModelRenderer、Collider、VisualScriptなどはこのクラスを継承する。
class Component {
public:
    Component() = default;
    virtual ~Component() = default;

    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;

    GameObject* GetOwner() const { return owner_; }

    bool IsEnabled() const { return enabled_; }
    void SetEnabled(bool enabled) {
        if (enabled_ == enabled) {
            return;
        }
        enabled_ = enabled;
        if (enabled_) {
            OnEnable();
        } else {
            OnDisable();
        }
    }

    // Unityに近いライフサイクル。
    // Awakeは追加直後、Startは最初のScene::Update直前に1回だけ呼ばれる。
    virtual void Awake() {}
    virtual void Start() {}
    virtual void OnEnable() {}
    virtual void OnDisable() {}
    virtual void Update(float /*deltaTime*/) {}
    virtual void OnDestroy() {}

private:
    friend class GameObject;

    GameObject* owner_ = nullptr;
    bool enabled_ = true;
    bool started_ = false;
};
