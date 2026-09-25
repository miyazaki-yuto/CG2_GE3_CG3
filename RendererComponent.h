#pragma once

#include "Component.h"

struct Matrix4x4;

// Sceneの描画収集対象になるComponentの共通基底。
// RenderOrderが小さいものから描画し、Sky→3D→2Dの順序を制御する。
class RendererComponent : public Component {
public:
    static constexpr int kSkyRenderOrder = -1000;
    static constexpr int kOpaqueRenderOrder = 0;
    static constexpr int kOverlayRenderOrder = 1000;

    RendererComponent() = default;
    ~RendererComponent() override = default;

    int GetRenderOrder() const { return renderOrder_; }
    void SetRenderOrder(int renderOrder) { renderOrder_ = renderOrder; }

    virtual void Render() = 0;
    virtual void RenderShadow(const Matrix4x4& lightViewProjection) {
        static_cast<void>(lightViewProjection);
    }

private:
    int renderOrder_ = kOpaqueRenderOrder;
};
