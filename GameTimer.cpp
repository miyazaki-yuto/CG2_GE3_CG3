#include "GameTimer.h"

#include <algorithm>

namespace {

// ブレークポイントやウィンドウ移動の後に、物体が一瞬で遠くへ飛ばないよう上限を設ける。
constexpr float kMaximumDeltaTime = 0.1f;

} // namespace

void GameTimer::Reset() {
    previousTime_ = Clock::now();
    deltaTime_ = 0.0f;
    totalTime_ = 0.0;
    isInitialized_ = true;
}

void GameTimer::Tick() {
    // Reset前にTickされた場合も、安全に初期化して0秒を返す。
    if (!isInitialized_) {
        Reset();
        return;
    }

    const Clock::time_point currentTime = Clock::now();
    const float elapsedSeconds =
        std::chrono::duration<float>(currentTime - previousTime_).count();
    previousTime_ = currentTime;

    // steady_clockは逆行しないが、異常値に備えて0～0.1秒へ制限する。
    deltaTime_ = (std::clamp)(elapsedSeconds, 0.0f, kMaximumDeltaTime);
    totalTime_ += static_cast<double>(deltaTime_);
}
