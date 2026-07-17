#include "InputManager.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "xinput.lib")

namespace {

void ThrowIfFailed(HRESULT result, const char* message) {
    if (FAILED(result)) {
        throw std::runtime_error(message);
    }
}

} // namespace

InputManager::~InputManager() {
    // Acquireしたデバイスは、COMオブジェクトを解放する前に入力の占有を解除する。
    if (mouseDevice_ != nullptr) {
        mouseDevice_->Unacquire();
    }
    if (keyboardDevice_ != nullptr) {
        keyboardDevice_->Unacquire();
    }
}

void InputManager::Initialize(HWND hWnd) {
    assert(hWnd != nullptr);
    hWnd_ = hWnd;

    // DirectInput本体を作成する。キーボードとマウスはこのオブジェクトから生成する。
    ThrowIfFailed(
        DirectInput8Create(
            GetModuleHandleW(nullptr),
            DIRECTINPUT_VERSION,
            IID_IDirectInput8,
            reinterpret_cast<void**>(directInput_.GetAddressOf()),
            nullptr),
        "Failed to create DirectInput 8.");

    ThrowIfFailed(
        directInput_->CreateDevice(
            GUID_SysKeyboard,
            keyboardDevice_.GetAddressOf(),
            nullptr),
        "Failed to create the DirectInput keyboard device.");
    ThrowIfFailed(
        keyboardDevice_->SetDataFormat(&c_dfDIKeyboard),
        "Failed to set the DirectInput keyboard data format.");
    ThrowIfFailed(
        keyboardDevice_->SetCooperativeLevel(
            hWnd_, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE),
        "Failed to set the DirectInput keyboard cooperative level.");

    ThrowIfFailed(
        directInput_->CreateDevice(
            GUID_SysMouse,
            mouseDevice_.GetAddressOf(),
            nullptr),
        "Failed to create the DirectInput mouse device.");
    ThrowIfFailed(
        mouseDevice_->SetDataFormat(&c_dfDIMouse2),
        "Failed to set the DirectInput mouse data format.");
    ThrowIfFailed(
        mouseDevice_->SetCooperativeLevel(
            hWnd_, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE),
        "Failed to set the DirectInput mouse cooperative level.");

    // Blender風カメラで使いやすいよう、マウスは画面座標ではなく相対移動量を取得する。
    DIPROPDWORD axisMode{};
    axisMode.diph.dwSize = sizeof(DIPROPDWORD);
    axisMode.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    axisMode.diph.dwObj = 0;
    axisMode.diph.dwHow = DIPH_DEVICE;
    axisMode.dwData = DIPROPAXISMODE_REL;
    ThrowIfFailed(
        mouseDevice_->SetProperty(DIPROP_AXISMODE, &axisMode.diph),
        "Failed to set the DirectInput mouse axis mode.");

    keyboardDevice_->Acquire();
    mouseDevice_->Acquire();

    // 起動直後にキーやボタンをTrigger扱いしないよう、現在値を前フレームにも入れる。
    currentKeyboard_.fill(0);
    ReadDeviceState(
        keyboardDevice_.Get(),
        static_cast<DWORD>(currentKeyboard_.size()),
        currentKeyboard_.data());
    previousKeyboard_ = currentKeyboard_;

    currentMouse_ = {};
    ReadDeviceState(
        mouseDevice_.Get(),
        sizeof(DIMOUSESTATE2),
        &currentMouse_);
    previousMouse_ = currentMouse_;

    // 絶対カーソル座標はデバッグ表示用。ボタン・移動量・ホイールはDirectInputから取得する。
    if (GetCursorPos(&currentMousePosition_)) {
        ScreenToClient(hWnd_, &currentMousePosition_);
    }

    isGamepadConnected_ =
        XInputGetState(0, &currentGamepad_) == ERROR_SUCCESS;
    previousGamepad_ = currentGamepad_;
}

void InputManager::Update() {
    previousKeyboard_ = currentKeyboard_;
    currentKeyboard_.fill(0);
    ReadDeviceState(
        keyboardDevice_.Get(),
        static_cast<DWORD>(currentKeyboard_.size()),
        currentKeyboard_.data());

    previousMouse_ = currentMouse_;
    currentMouse_ = {};
    ReadDeviceState(
        mouseDevice_.Get(),
        sizeof(DIMOUSESTATE2),
        &currentMouse_);

    if (GetCursorPos(&currentMousePosition_)) {
        ScreenToClient(hWnd_, &currentMousePosition_);
    }

    // ゲームパッドはDirectInputよりXInputの方が振動やトリガーを扱いやすいため継続する。
    previousGamepad_ = currentGamepad_;
    currentGamepad_ = {};
    isGamepadConnected_ =
        XInputGetState(0, &currentGamepad_) == ERROR_SUCCESS;
}

bool InputManager::ReadDeviceState(
    IDirectInputDevice8* device,
    DWORD dataSize,
    void* destination) {
    assert(device != nullptr);
    assert(destination != nullptr);

    HRESULT result = device->GetDeviceState(dataSize, destination);
    if (SUCCEEDED(result)) {
        return true;
    }

    // Alt+Tabなどでフォーカスを失うとAcquireが解除される。
    // フォーカスが戻ったフレームで再取得し、利用者側に再初期化を要求しない。
    result = device->Acquire();
    while (result == DIERR_INPUTLOST) {
        result = device->Acquire();
    }
    if (FAILED(result)) {
        return false;
    }

    return SUCCEEDED(device->GetDeviceState(dataSize, destination));
}

bool InputManager::IsKeyPressed(int virtualKey) const {
    return IsVirtualKeyDown(currentKeyboard_, virtualKey);
}

bool InputManager::IsKeyTriggered(int virtualKey) const {
    return IsVirtualKeyDown(currentKeyboard_, virtualKey) &&
        !IsVirtualKeyDown(previousKeyboard_, virtualKey);
}

bool InputManager::IsKeyReleased(int virtualKey) const {
    return !IsVirtualKeyDown(currentKeyboard_, virtualKey) &&
        IsVirtualKeyDown(previousKeyboard_, virtualKey);
}

bool InputManager::IsMousePressed(MouseButton button) const {
    const size_t index = MouseButtonToIndex(button);
    return IsDirectInputStateDown(currentMouse_.rgbButtons[index]);
}

bool InputManager::IsMouseTriggered(MouseButton button) const {
    const size_t index = MouseButtonToIndex(button);
    return IsDirectInputStateDown(currentMouse_.rgbButtons[index]) &&
        !IsDirectInputStateDown(previousMouse_.rgbButtons[index]);
}

bool InputManager::IsMouseReleased(MouseButton button) const {
    const size_t index = MouseButtonToIndex(button);
    return !IsDirectInputStateDown(currentMouse_.rgbButtons[index]) &&
        IsDirectInputStateDown(previousMouse_.rgbButtons[index]);
}

POINT InputManager::GetMouseDelta() const {
    return { currentMouse_.lX, currentMouse_.lY };
}

bool InputManager::IsGamepadButtonPressed(WORD button) const {
    return isGamepadConnected_ &&
        (currentGamepad_.Gamepad.wButtons & button) != 0;
}

bool InputManager::IsGamepadButtonTriggered(WORD button) const {
    return isGamepadConnected_ &&
        (currentGamepad_.Gamepad.wButtons & button) != 0 &&
        (previousGamepad_.Gamepad.wButtons & button) == 0;
}

bool InputManager::IsGamepadButtonReleased(WORD button) const {
    return isGamepadConnected_ &&
        (currentGamepad_.Gamepad.wButtons & button) == 0 &&
        (previousGamepad_.Gamepad.wButtons & button) != 0;
}

GamepadStick InputManager::GetLeftStick() const {
    if (!isGamepadConnected_) {
        return {};
    }
    return {
        NormalizeThumbStick(
            currentGamepad_.Gamepad.sThumbLX,
            XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE),
        NormalizeThumbStick(
            currentGamepad_.Gamepad.sThumbLY,
            XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)
    };
}

GamepadStick InputManager::GetRightStick() const {
    if (!isGamepadConnected_) {
        return {};
    }
    return {
        NormalizeThumbStick(
            currentGamepad_.Gamepad.sThumbRX,
            XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE),
        NormalizeThumbStick(
            currentGamepad_.Gamepad.sThumbRY,
            XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE)
    };
}

float InputManager::GetLeftTrigger() const {
    return isGamepadConnected_
        ? NormalizeTrigger(currentGamepad_.Gamepad.bLeftTrigger)
        : 0.0f;
}

float InputManager::GetRightTrigger() const {
    return isGamepadConnected_
        ? NormalizeTrigger(currentGamepad_.Gamepad.bRightTrigger)
        : 0.0f;
}

bool InputManager::IsDirectInputStateDown(BYTE state) {
    return (state & 0x80u) != 0;
}

bool InputManager::IsVirtualKeyDown(
    const std::array<BYTE, 256>& keyboardState,
    int virtualKey) {
    // VK_SHIFTなど左右共通の仮想キーは、DirectInputでは左右別なので両方を確認する。
    if (virtualKey == VK_SHIFT) {
        return IsDirectInputStateDown(keyboardState[DIK_LSHIFT]) ||
            IsDirectInputStateDown(keyboardState[DIK_RSHIFT]);
    }
    if (virtualKey == VK_CONTROL) {
        return IsDirectInputStateDown(keyboardState[DIK_LCONTROL]) ||
            IsDirectInputStateDown(keyboardState[DIK_RCONTROL]);
    }
    if (virtualKey == VK_MENU) {
        return IsDirectInputStateDown(keyboardState[DIK_LMENU]) ||
            IsDirectInputStateDown(keyboardState[DIK_RMENU]);
    }

    const int directInputKey = VirtualKeyToDirectInputKey(virtualKey);
    return directInputKey >= 0 && directInputKey < 256 &&
        IsDirectInputStateDown(
            keyboardState[static_cast<size_t>(directInputKey)]);
}

int InputManager::VirtualKeyToDirectInputKey(int virtualKey) {
    if (virtualKey < 0 || virtualKey > 0xff) {
        return -1;
    }

    // VKコードをハードコードせず、現在のキーボードレイアウトに対応するスキャンコードへ変換する。
    const UINT scanCode = MapVirtualKeyW(
        static_cast<UINT>(virtualKey), MAPVK_VK_TO_VSC_EX);
    if (scanCode == 0) {
        return -1;
    }

    int directInputKey = static_cast<int>(scanCode & 0xffu);
    // E0拡張キーはDirectInputのDIK値で上位ビットが立つ。
    if ((scanCode & 0xff00u) == 0xe000u) {
        directInputKey |= 0x80;
    }
    return directInputKey;
}

size_t InputManager::MouseButtonToIndex(MouseButton button) {
    const size_t index = static_cast<size_t>(button);
    assert(index < 5);
    return index < 5 ? index : 0;
}

float InputManager::NormalizeThumbStick(SHORT value, SHORT deadZone) {
    const int magnitude = std::abs(static_cast<int>(value));
    if (magnitude <= deadZone) {
        return 0.0f;
    }

    const float normalized = static_cast<float>(magnitude - deadZone) /
        static_cast<float>(32767 - deadZone);
    return value < 0
        ? -(std::min)(normalized, 1.0f)
        : (std::min)(normalized, 1.0f);
}

float InputManager::NormalizeTrigger(BYTE value) {
    if (value <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
        return 0.0f;
    }
    return static_cast<float>(
        value - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) /
        static_cast<float>(255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
}
