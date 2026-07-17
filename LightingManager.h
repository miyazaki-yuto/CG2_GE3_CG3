#pragma once

#include <d3d12.h>

#include "CommonTypes.h"

class DirectXCommon;

// シーンで共有する平行光源を1か所で管理するクラス。
// Primitive・Model・Spriteが個別に同じ定数バッファを作る必要をなくす。
class LightingManager {
public:
    LightingManager() = default;
    ~LightingManager() = default;

    LightingManager(const LightingManager&) = delete;
    LightingManager& operator=(const LightingManager&) = delete;

    void Initialize(DirectXCommon* dxCommon);

    // 現在フレーム専用の定数領域へ、CPU側で保持しているライト値を1回だけコピーする。
    void BeginFrame();

    void SetDirectionalLight(const DirectionalLight& light) {
        directionalLight_ = light;
    }
    const DirectionalLight& GetDirectionalLight() const {
        return directionalLight_;
    }

    D3D12_GPU_VIRTUAL_ADDRESS GetDirectionalLightGpuAddress() const;

private:
    DirectXCommon* dxCommon_ = nullptr;
    DirectionalLight directionalLight_{};
    D3D12_GPU_VIRTUAL_ADDRESS directionalLightGpuAddress_ = 0;
};
