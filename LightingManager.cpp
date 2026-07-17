#include "LightingManager.h"

#include "DirectXCommon.h"

#include <algorithm>
#include <cassert>

void LightingManager::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon != nullptr);
    dxCommon_ = dxCommon;

    // Blenderの初期シーンに近く、移動結果が分かりやすいPoint Lightを最初から有効にする。
    DirectionalLight directionalLight{};
    directionalLight.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLight.direction = { 0.0f, -1.0f, 1.0f };
    directionalLight.intensity = 1.0f;
    directionalLight.enabled = 0;
    SetDirectionalLight(directionalLight);

    PointLight pointLight{};
    pointLight.color = { 1.0f, 0.9f, 0.75f, 1.0f };
    pointLight.position = { 0.0f, 2.0f, -3.0f };
    pointLight.intensity = 2.0f;
    pointLight.radius = 10.0f;
    pointLight.decay = 2.0f;
    pointLight.enabled = 1;
    SetPointLight(pointLight);
}

void LightingManager::BeginFrame() {
    assert(dxCommon_ != nullptr);

    // Drawより前にImGuiで編集された値を同じフレームへ反映できるよう、
    // 実際のUploadはGetLightingGpuAddressが最初に呼ばれた時まで遅らせる。
    lightingGpuAddress_ = 0;
}

void LightingManager::SetDirectionalLight(const DirectionalLight& light) {
    DirectionalLight sanitized = light;
    if (sanitized.direction.Length() <= 0.0001f) {
        sanitized.direction = { 0.0f, -1.0f, 0.0f };
    } else {
        sanitized.direction.Normalize();
    }
    sanitized.intensity = (std::max)(sanitized.intensity, 0.0f);
    sanitized.enabled = sanitized.enabled != 0 ? 1 : 0;
    sanitized.padding[0] = 0.0f;
    sanitized.padding[1] = 0.0f;
    sanitized.padding[2] = 0.0f;
    lightingData_.directionalLight = sanitized;
}

void LightingManager::SetPointLight(const PointLight& light) {
    PointLight sanitized = light;
    sanitized.intensity = (std::max)(sanitized.intensity, 0.0f);
    sanitized.radius = (std::max)(sanitized.radius, 0.001f);
    sanitized.decay = (std::max)(sanitized.decay, 0.001f);
    sanitized.enabled = sanitized.enabled != 0 ? 1 : 0;
    sanitized.padding = 0.0f;
    lightingData_.pointLight = sanitized;
}

D3D12_GPU_VIRTUAL_ADDRESS LightingManager::GetLightingGpuAddress() {
    assert(dxCommon_ != nullptr);
    if (lightingGpuAddress_ != 0) {
        return lightingGpuAddress_;
    }

    // GPUが前フレームのライトを読み出している間に上書きしないよう、
    // 現在フレーム専用領域へSunとPoint Lightをまとめて1回だけコピーする。
    const DynamicBufferAllocation allocation =
        dxCommon_->AllocateDynamicBuffer(sizeof(LightingData));
    *static_cast<LightingData*>(allocation.cpuAddress) = lightingData_;
    lightingGpuAddress_ = allocation.gpuAddress;
    return lightingGpuAddress_;
}
