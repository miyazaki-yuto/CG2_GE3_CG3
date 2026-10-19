#pragma once

#include <chrono>

// フレーム間の経過時間を計測するクラス。
// ゲーム側は「1フレームに進む量」ではなく「1秒あたりの速度」を定義し、
// その速度にDeltaTimeを掛けることで、FPSが変化しても同じ速さで動かせる。
class GameTimer {
public:
    GameTimer() = default;
    ~GameTimer() = default;

    // 現在時刻を基準時刻として保存し、計測値を0へ戻す。
    void Reset();

    // 前回のTickから何秒経過したかを計測する。1フレームに1回だけ呼ぶ。
    void Tick();

    // 直前の1フレームにかかった秒数。
    float GetDeltaTime() const { return deltaTime_; }

    // Resetしてからの累積時間。ポーズ演出などを除いたゲーム内時間として利用できる。
    double GetTotalTime() const { return totalTime_; }

private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point previousTime_{};
    float deltaTime_ = 0.0f;
    double totalTime_ = 0.0;
    bool isInitialized_ = false;
};
