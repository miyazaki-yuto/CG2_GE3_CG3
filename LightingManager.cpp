#include "LightingManager.h"

#include "DirectXCommon.h"

#include <cassert>

void LightingManager::Initialize(DirectXCommon* dxCommon) {
    assert(dxCommon != nullptr);
    dxCommon_ = dxCommon;

    directionalLight_.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLight_.direction = { 0.0f, -1.0f, 1.0f };
    directionalLight_.direction.Normalize();
    directionalLight_.intensity = 1.0f;
}

void LightingManager::BeginFrame() {
    assert(dxCommon_ != nullptr);

    // GPUが前フレームのライトを読み出している間に上書きしないよう、
    // 現在フレーム専用の動的バッファへ値をコピーする。
    const DynamicBufferAllocation allocation =
        dxCommon_->AllocateDynamicBuffer(sizeof(DirectionalLight));
    *static_cast<DirectionalLight*>(allocation.cpuAddress) = directionalLight_;
    directionalLightGpuAddress_ = allocation.gpuAddress;
}

D3D12_GPU_VIRTUAL_ADDRESS
LightingManager::GetDirectionalLightGpuAddress() const {
    // BeginFrameより先にDrawした場合は有効なGPUアドレスがまだ存在しない。
    assert(directionalLightGpuAddress_ != 0);
    return directionalLightGpuAddress_;
}
