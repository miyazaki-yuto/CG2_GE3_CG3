#define _USE_MATH_DEFINES
#define NOMINMAX
#include "TriangleEffect.h"
#include <cmath>
#include <algorithm>

void TriangleEffect::Initialize(Graphics* graphics) {
    graphics_ = graphics;

    params_.numPetals = 100;                 // 破片の数
    params_.duration = 0.8f;                 // 展開を素早く
    params_.maxRadius = 5.0f;                // 展開範囲
    params_.petalScale = 0.35f;              // シャープに見せるため少し小さめ
    params_.spreadAngle = 4.0f;              // 拡散幅
    params_.color = { 0.0f, 1.0f, 0.8f, 1.0f }; // 色
    params_.spinSpeed = 12.0f;               // 自転スピード
    params_.pulseSpeed = 8.0f;               // 公転のうねり速度
    params_.pulseMagnitude = 1.0f;           // うねりの大きさ
    params_.staggerDelay = 0.2f;             // 展開のラグを減らして一気に爆発させる

    SetParameters(params_);
}

void TriangleEffect::SetParameters(const EffectParameters& params) {
    params_ = params;
    int num = std::min(params_.numPetals, kMaxPetals);
    petals_.clear();
    petals_.resize(num);

    // 三角形をとげみたいにしとく
    TextureVertexData vertices[3] = {
        { {  0.0f,  1.0f, 0.0f, 1.0f }, { 0.5f, 0.0f } },  // 鋭い先端
        { { -0.15f, -0.4f, 0.0f, 1.0f }, { 0.0f, 1.0f } }, // 細い底辺左
        { {  0.15f, -0.4f, 0.0f, 1.0f }, { 1.0f, 1.0f } }  // 細い底辺右
    };

    // 球状配置による立体計算
    float phi = (float)M_PI * (3.0f - std::sqrt(5.0f));

    for (int i = 0; i < num; ++i) {
        Petal& p = petals_[i];

        // Y軸を -1.0 から 1.0 の間で均等に分布させて球体を作る
        float y = 1.0f - (i / (float)(num - 1)) * 2.0f;
        float radiusAtY = std::sqrt(1.0f - y * y); // 球面になるための半径

        p.angle = i * phi; // 水平面の角度

        // 爆発の開始状態
        p.start.scale = { 0.0f, 0.0f, 0.0f };
        p.start.rotate = { 0.0f, 0.0f, 0.0f };
        p.start.translate = { 0.0f, 0.0f, 0.0f };

        // 爆発の終了状態
        p.end.scale = { params_.petalScale, params_.petalScale, params_.petalScale };

        p.end.translate = {
            std::cos(p.angle) * radiusAtY * params_.maxRadius,
            y * params_.spreadAngle, // spreadAngleをY軸方向の広がりをココでも使う
            std::sin(p.angle) * radiusAtY * params_.maxRadius
        };

        // 各破片が外側を向くように初期回転を設定
        p.end.rotate = { (float)std::acos(y), p.angle, 0.0f };

        p.textureIndex = 1;
        graphics_->SetTriangleVertices(i, vertices);
    }
}

void TriangleEffect::Reset() {
    currentTime_ = 0.0f;
    isExecuting_ = true;
}

void TriangleEffect::Update() {
    if (!isExecuting_) return;

    float deltaTime = 1.0f / 60.0f;
    currentTime_ += deltaTime;

    for (size_t i = 0; i < petals_.size(); ++i) {
        Petal& p = petals_[i];

        // ランダム感のあるディレイでバラバラに爆発させる
        float delay = ((float)(i % 5) / 5.0f) * params_.staggerDelay;
        float localTime = std::max(0.0f, currentTime_ - delay);
        float t = std::min(localTime / params_.duration, 1.0f);

        // 鋭い動きにするためイージングをExpo的なカーブに
        float easedT = (t == 1.0f) ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);

        TransformData current;
        current.scale = Lerp(p.start.scale, p.end.scale, easedT);
        current.rotate = Lerp(p.start.rotate, p.end.rotate, easedT);
        current.translate = Lerp(p.start.translate, p.end.translate, easedT);

        if (easedT > 0.0f) {
            // 全体の公転
            float orbitAngle = currentTime_ * params_.spinSpeed * 0.3f;
            float tx = current.translate.x * cos(orbitAngle) - current.translate.z * sin(orbitAngle);
            float tz = current.translate.x * sin(orbitAngle) + current.translate.z * cos(orbitAngle);
            current.translate.x = tx;
            current.translate.z = tz;

            // Z軸/Y軸にうねりを加えて立体感を強調
            float wave = sin(currentTime_ * params_.pulseSpeed + p.angle) * params_.pulseMagnitude;
            current.translate.y += wave * easedT * 0.5f;

            // 三角形自身の自転
            float spinDir = (i % 2 == 0) ? 1.0f : -1.0f; // 破片ごとに回転方向を反転させてカオス感を出す
            current.rotate.x += currentTime_ * params_.spinSpeed * 1.5f * spinDir;
            current.rotate.y += currentTime_ * params_.spinSpeed * 2.0f;
            current.rotate.z += currentTime_ * params_.spinSpeed * 0.8f * spinDir;

            // 展開完了後鼓動するように
            if (t >= 1.0f) {
                float pulse = sin(currentTime_ * params_.pulseSpeed * 2.0f + i) * 0.1f;
                current.scale.x += pulse;
                current.scale.y += pulse;
                current.scale.z += pulse;
            }
        }

        graphics_->SetTriangleTransform((int)i, current);
    }
}

void TriangleEffect::Draw() {
    graphics_->SetColor(params_.color);
    for (size_t i = 0; i < petals_.size(); ++i) {
        graphics_->SetTriangleTexture((int)i, petals_[i].textureIndex);
    }
}

float TriangleEffect::EaseOutQuart(float t) {
    float invT = 1.0f - t;
    return 1.0f - invT * invT * invT * invT;
}

Vector3 TriangleEffect::Lerp(const Vector3& a, const Vector3& b, float t) {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    };
}