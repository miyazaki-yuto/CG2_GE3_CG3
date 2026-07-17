#include "InputManager.h"

#include <algorithm>
#include <cmath>

void InputManager::Initialize(HWND hWnd) {
    hWnd_ = hWnd;

    // 初期化時は現在値を前フレーム値にも入れ、起動直後の誤Triggerを防ぐ。
    if (!GetKeyboardState(currentKeyboard_.data())) {
        currentKeyboard_.fill(0);
    }
    previousKeyboard_ = currentKeyboard_;

    if (GetCursorPos(&currentMousePosition_)) {
        ScreenToClient(hWnd_, &currentMousePosition_);
    }
    previousMousePosition_ = currentMousePosition_;

    isGamepadConnected_ =
        XInputGetState(0, &currentGamepad_) == ERROR_SUCCESS;
    previousGamepad_ = currentGamepad_;
}

void InputManager::Update() {
    // 比較用に今の値を前フレームへ移してから、新しい状態を取得する。
    previousKeyboard_ = currentKeyboard_;
    if (!isWindowActive_ || !GetKeyboardState(currentKeyboard_.data())) {
        currentKeyboard_.fill(0);
    }

    previousMousePosition_ = currentMousePosition_;
    if (GetCursorPos(&currentMousePosition_)) {
        ScreenToClient(hWnd_, &currentMousePosition_);
    }

    // WindowProcで蓄積したホイール量を、このフレームの値として確定する。
    mouseWheelDelta_ = pendingMouseWheelDelta_;
    pendingMouseWheelDelta_ = 0;

    previousGamepad_ = currentGamepad_;
    currentGamepad_ = {};
    isGamepadConnected_ =
        XInputGetState(0, &currentGamepad_) == ERROR_SUCCESS;
}

void InputManager::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    static_cast<void>(lParam);
    switch (message) {
    case WM_MOUSEWHEEL:
        pendingMouseWheelDelta_ += GET_WHEEL_DELTA_WPARAM(wParam);
        break;
    case WM_ACTIVATEAPP:
        isWindowActive_ = wParam != FALSE;
        if (!isWindowActive_) {
            pendingMouseWheelDelta_ = 0;
        }
        break;
    default:
        break;
    }
}

bool InputManager::IsKeyPressed(int virtualKey) const {
    return IsValidVirtualKey(virtualKey) &&
        IsKeyboardStateDown(currentKeyboard_[static_cast<size_t>(virtualKey)]);
}

bool InputManager::IsKeyTriggered(int virtualKey) const {
    if (!IsValidVirtualKey(virtualKey)) {
        return false;
    }
    const size_t index = static_cast<size_t>(virtualKey);
    return IsKeyboardStateDown(currentKeyboard_[index]) &&
        !IsKeyboardStateDown(previousKeyboard_[index]);
}

bool InputManager::IsKeyReleased(int virtualKey) const {
    if (!IsValidVirtualKey(virtualKey)) {
        return false;
    }
    const size_t index = static_cast<size_t>(virtualKey);
    return !IsKeyboardStateDown(currentKeyboard_[index]) &&
        IsKeyboardStateDown(previousKeyboard_[index]);
}

bool InputManager::IsMousePressed(MouseButton button) const {
    return IsKeyPressed(MouseButtonToVirtualKey(button));
}

bool InputManager::IsMouseTriggered(MouseButton button) const {
    return IsKeyTriggered(MouseButtonToVirtualKey(button));
}

bool InputManager::IsMouseReleased(MouseButton button) const {
    return IsKeyReleased(MouseButtonToVirtualKey(button));
}

POINT InputManager::GetMouseDelta() const {
    return {
        currentMousePosition_.x - previousMousePosition_.x,
        currentMousePosition_.y - previousMousePosition_.y
    };
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

bool InputManager::IsKeyboardStateDown(BYTE state) {
    return (state & 0x80u) != 0;
}

int InputManager::MouseButtonToVirtualKey(MouseButton button) {
    switch (button) {
    case MouseButton::Left:
        return VK_LBUTTON;
    case MouseButton::Right:
        return VK_RBUTTON;
    case MouseButton::Middle:
        return VK_MBUTTON;
    case MouseButton::X1:
        return VK_XBUTTON1;
    case MouseButton::X2:
        return VK_XBUTTON2;
    default:
        return 0;
    }
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

bool InputManager::IsValidVirtualKey(int virtualKey) const {
    return virtualKey >= 0 &&
        virtualKey < static_cast<int>(currentKeyboard_.size());
}
