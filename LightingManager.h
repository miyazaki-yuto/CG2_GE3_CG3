#pragma once

#include <d3d12.h>

#include "CommonTypes.h"

class DirectXCommon;

// シーンで共有するSunとPoint Lightを1か所で管理するクラス。
// Primitive・Model・Spriteが個別に同じ定数バッファを作る必要をなくす。
class LightingManager {
public:
    LightingManager() = default;
    ~LightingManager() = default;

    LightingManager(const LightingManager&) = delete;
    LightingManager& operator=(const LightingManager&) = delete;

    void Initialize(DirectXCommon* dxCommon);

    // フレーム開始時に、前フレームのGPUアドレスを無効化する。
    void BeginFrame();

    void SetDirectionalLight(const DirectionalLight& light);
    const DirectionalLight& GetDirectionalLight() const {
        return lightingData_.directionalLight;
    }

    void SetPointLight(const PointLight& light);
    const PointLight& GetPointLight() const {
        return lightingData_.pointLight;
    }

    // 最初のDraw時に現在値をGPUへコピーし、同一フレーム中は同じアドレスを共有する。
    D3D12_GPU_VIRTUAL_ADDRESS GetLightingGpuAddress();

private:
    DirectXCommon* dxCommon_ = nullptr;
    LightingData lightingData_{};
    D3D12_GPU_VIRTUAL_ADDRESS lightingGpuAddress_ = 0;
};
