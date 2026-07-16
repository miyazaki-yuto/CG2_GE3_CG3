#pragma once
#include "Graphics.h"
#include "CommonTypes.h"
#include <vector>

// 花弁1枚のデータを表す構造体
struct Petal {
    float angle;           // 中心からの角度
    TransformData start;   // アニメーション開始時のトランスフォーム
    TransformData end;     // アニメーション終了時のトランスフォーム
    TransformData current; // 現在フレームでDrawへ渡すトランスフォーム
};


// エフェクトのパラメータを調整するための構造体
struct EffectParameters {
    int numPetals;        // 花弁の数 
    float duration;       // アニメーションの時間
    float maxRadius;      // 展開時の半径
    float petalScale;     // 花弁の基本スケール
    float spreadAngle;    // 展開時の傾き角度
    Vector4 color;        // 花の色
    float spinSpeed ;     // 全体の回転スピード
    float pulseSpeed ;    // 脈動のスピード
    float pulseMagnitude; // 脈動の大きさ
    float staggerDelay;   // 展開の時間差
};

class TriangleEffect
{
public:
    TriangleEffect() = default;
    ~TriangleEffect() = default;

    // 初期化
    void Initialize(Graphics* graphics);
    // 更新
    void Update();
    // 全花弁を、引数で指定されたテクスチャを使って描画する。
    void Draw(int textureHandle);
    // エフェクトをリセットして再生
    void Reset();

    // パラメータの取得と設定
    EffectParameters& GetParameters() { return params_; }
    void SetParameters(const EffectParameters& params);

private:
    // イージング関数 
    float EaseOutQuart(float t);
    // 3Dベクトルの線形補間
    Vector3 Lerp(const Vector3& a, const Vector3& b, float t);

private:
    Graphics* graphics_ = nullptr;
    PrimitiveDrawer* primitiveDrawer_ = nullptr;
    TextureVertexData triangleVertices_[3]{};
    std::vector<Petal> petals_;
    EffectParameters params_;
    float currentTime_ = 0.0f;
    bool isExecuting_ = false;

    static const int kMaxPetals = 100; // 最大三角形数
};
