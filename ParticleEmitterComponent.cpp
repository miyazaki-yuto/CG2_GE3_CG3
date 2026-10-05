#include "ParticleEmitterComponent.h"

#include "GameObject.h"

#include <algorithm>
#include <cmath>

namespace {

float Lerp(float start, float end, float amount) {
    return start + (end - start) * amount;
}

Color4 LerpColor(const Color4& start, const Color4& end, float amount) {
    return {
        Lerp(start.r, end.r, amount),
        Lerp(start.g, end.g, amount),
        Lerp(start.b, end.b, amount),
        Lerp(start.a, end.a, amount)
    };
}

} // namespace

ParticleEmitterComponent::ParticleEmitterComponent(
    ParticleDrawer* particleDrawer,
    int textureHandle,
    std::string textureGuid)
    : particleDrawer_(particleDrawer),
      textureHandle_(textureHandle),
      textureGuid_(std::move(textureGuid)) {
    SetBlendMode(BlendMode::Add);
    SetRenderOrder(kTransparentRenderOrder);
    particles_.reserve(maxParticles_);
    renderData_.reserve(maxParticles_);
}

void ParticleEmitterComponent::Start() {
    if (playOnAwake_) {
        Play();
        Emit(burstCount_);
    }
}

void ParticleEmitterComponent::Play() {
    playing_ = true;
}

void ParticleEmitterComponent::Stop(bool clearParticles) {
    playing_ = false;
    emissionAccumulator_ = 0.0f;
    if (clearParticles) {
        Clear();
    }
}

void ParticleEmitterComponent::Clear() {
    particles_.clear();
    renderData_.clear();
}

void ParticleEmitterComponent::SetMaxParticles(uint32_t count) {
    maxParticles_ = (std::clamp)(
        count, 1u, ParticleDrawer::kMaxParticleCount);
    if (particles_.size() > maxParticles_) {
        particles_.resize(maxParticles_);
    }
    particles_.reserve(maxParticles_);
    renderData_.reserve(maxParticles_);
}

void ParticleEmitterComponent::SetEmissionRate(float rate) {
    emissionRate_ = (std::max)(rate, 0.0f);
}

void ParticleEmitterComponent::SetLifetimeRange(
    float minimum,
    float maximum) {
    lifetimeMin_ = (std::max)(minimum, 0.01f);
    lifetimeMax_ = (std::max)(maximum, lifetimeMin_);
}

void ParticleEmitterComponent::SetSpeedRange(float minimum, float maximum) {
    speedMin_ = minimum;
    speedMax_ = (std::max)(maximum, speedMin_);
}

void ParticleEmitterComponent::SetSpread(float spread) {
    spread_ = (std::max)(spread, 0.0f);
}

void ParticleEmitterComponent::SetSizeRange(float startSize, float endSize) {
    startSize_ = (std::max)(startSize, 0.0f);
    endSize_ = (std::max)(endSize, 0.0f);
}

float ParticleEmitterComponent::RandomRange(float minimum, float maximum) {
    std::uniform_real_distribution<float> distribution(minimum, maximum);
    return distribution(randomEngine_);
}

Vector3 ParticleEmitterComponent::RandomDirection() {
    Vector3 result = {
        direction_.x + RandomRange(-spread_, spread_),
        direction_.y + RandomRange(-spread_, spread_),
        direction_.z + RandomRange(-spread_, spread_)
    };
    if (result.Length() <= 0.00001f) {
        result = { 0.0f, 1.0f, 0.0f };
    }
    result.Normalize();
    return result;
}

void ParticleEmitterComponent::Emit(uint32_t count) {
    if (GetOwner() == nullptr || count == 0) {
        return;
    }
    const size_t available = maxParticles_ - particles_.size();
    count = static_cast<uint32_t>((std::min)(
        static_cast<size_t>(count), available));
    const Vector3 emitterPosition =
        GetOwner()->GetTransform().GetWorldPosition();

    for (uint32_t index = 0; index < count; ++index) {
        Particle particle{};
        particle.position = emitterPosition;
        particle.velocity = RandomDirection() *
            RandomRange(speedMin_, speedMax_);
        particle.rotation = RandomRange(-3.14159265f, 3.14159265f);
        particle.angularVelocity = RandomRange(-2.0f, 2.0f);
        particle.lifetime = RandomRange(lifetimeMin_, lifetimeMax_);
        particle.startSize = startSize_;
        particle.endSize = endSize_;
        particle.startColor = startColor_;
        particle.endColor = endColor_;
        particles_.push_back(particle);
    }
}

void ParticleEmitterComponent::Update(float deltaTime) {
    if (deltaTime <= 0.0f) {
        return;
    }
    if (playing_ && emissionRate_ > 0.0f) {
        emissionAccumulator_ += emissionRate_ * deltaTime;
        const uint32_t emitCount =
            static_cast<uint32_t>(emissionAccumulator_);
        if (emitCount > 0) {
            Emit(emitCount);
            emissionAccumulator_ -= static_cast<float>(emitCount);
        }
    }

    for (Particle& particle : particles_) {
        particle.age += deltaTime;
        particle.velocity = particle.velocity + gravity_ * deltaTime;
        particle.position = particle.position + particle.velocity * deltaTime;
        particle.rotation += particle.angularVelocity * deltaTime;
    }
    std::erase_if(
        particles_,
        [](const Particle& particle) {
            return particle.age >= particle.lifetime;
        });
}

void ParticleEmitterComponent::Render() {
    if (particleDrawer_ == nullptr || textureHandle_ < 0 || particles_.empty()) {
        return;
    }
    renderData_.clear();
    renderData_.reserve(particles_.size());
    for (const Particle& particle : particles_) {
        const float normalizedAge = (std::clamp)(
            particle.age / particle.lifetime, 0.0f, 1.0f);
        const float size = Lerp(
            particle.startSize, particle.endSize, normalizedAge);
        renderData_.push_back({
            particle.position,
            { size, size },
            particle.rotation,
            LerpColor(particle.startColor, particle.endColor, normalizedAge)
        });
    }
    particleDrawer_->Draw(renderData_, textureHandle_, GetBlendMode());
}
