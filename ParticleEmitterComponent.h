#pragma once

#include "ParticleDrawer.h"
#include "RendererComponent.h"

#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

// 寿命・速度・色・サイズをCPUで更新し、描画はParticleDrawerで一括するEmitter。
class ParticleEmitterComponent final : public RendererComponent {
public:
    ParticleEmitterComponent(
        ParticleDrawer* particleDrawer,
        int textureHandle,
        std::string textureGuid = {});
    ~ParticleEmitterComponent() override = default;

    ParticleEmitterComponent(const ParticleEmitterComponent&) = delete;
    ParticleEmitterComponent& operator=(
        const ParticleEmitterComponent&) = delete;

    void Start() override;
    void Update(float deltaTime) override;
    void Render() override;

    void Play();
    void Stop(bool clearParticles = false);
    void Clear();
    void Emit(uint32_t count);

    ParticleDrawer* GetParticleDrawer() const { return particleDrawer_; }
    int GetTextureHandle() const { return textureHandle_; }
    const std::string& GetTextureGuid() const { return textureGuid_; }
    void SetTextureAsset(int textureHandle, std::string textureGuid) {
        textureHandle_ = textureHandle;
        textureGuid_ = std::move(textureGuid);
    }

    uint32_t GetMaxParticles() const { return maxParticles_; }
    void SetMaxParticles(uint32_t count);
    uint32_t GetAliveParticleCount() const {
        return static_cast<uint32_t>(particles_.size());
    }
    float GetEmissionRate() const { return emissionRate_; }
    void SetEmissionRate(float rate);
    uint32_t GetBurstCount() const { return burstCount_; }
    void SetBurstCount(uint32_t count) { burstCount_ = count; }
    bool IsPlayOnAwake() const { return playOnAwake_; }
    void SetPlayOnAwake(bool enabled) { playOnAwake_ = enabled; }
    bool IsPlaying() const { return playing_; }

    float GetLifetimeMin() const { return lifetimeMin_; }
    float GetLifetimeMax() const { return lifetimeMax_; }
    void SetLifetimeRange(float minimum, float maximum);
    float GetSpeedMin() const { return speedMin_; }
    float GetSpeedMax() const { return speedMax_; }
    void SetSpeedRange(float minimum, float maximum);
    const Vector3& GetDirection() const { return direction_; }
    void SetDirection(const Vector3& direction) { direction_ = direction; }
    float GetSpread() const { return spread_; }
    void SetSpread(float spread);
    const Vector3& GetGravity() const { return gravity_; }
    void SetGravity(const Vector3& gravity) { gravity_ = gravity; }
    float GetStartSize() const { return startSize_; }
    float GetEndSize() const { return endSize_; }
    void SetSizeRange(float startSize, float endSize);
    const Color4& GetStartColor() const { return startColor_; }
    const Color4& GetEndColor() const { return endColor_; }
    void SetStartColor(const Color4& color) { startColor_ = color; }
    void SetEndColor(const Color4& color) { endColor_ = color; }

private:
    struct Particle {
        Vector3 position{};
        Vector3 velocity{};
        float rotation = 0.0f;
        float angularVelocity = 0.0f;
        float age = 0.0f;
        float lifetime = 1.0f;
        float startSize = 1.0f;
        float endSize = 0.0f;
        Color4 startColor{};
        Color4 endColor{};
    };

    float RandomRange(float minimum, float maximum);
    Vector3 RandomDirection();

    ParticleDrawer* particleDrawer_ = nullptr;
    int textureHandle_ = -1;
    std::string textureGuid_;
    std::vector<Particle> particles_;
    std::vector<ParticleRenderData> renderData_;
    std::mt19937 randomEngine_{ 0x50415254u };

    uint32_t maxParticles_ = 1000;
    float emissionRate_ = 100.0f;
    uint32_t burstCount_ = 0;
    bool playOnAwake_ = true;
    bool playing_ = false;
    float emissionAccumulator_ = 0.0f;
    float lifetimeMin_ = 1.0f;
    float lifetimeMax_ = 2.0f;
    float speedMin_ = 1.0f;
    float speedMax_ = 3.0f;
    Vector3 direction_{ 0.0f, 1.0f, 0.0f };
    float spread_ = 0.5f;
    Vector3 gravity_{ 0.0f, -1.0f, 0.0f };
    float startSize_ = 0.5f;
    float endSize_ = 0.0f;
    Color4 startColor_{ 1.0f, 1.0f, 1.0f, 1.0f };
    Color4 endColor_{ 1.0f, 1.0f, 1.0f, 0.0f };
};
