#pragma once

#include <Windows.h>
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>
#include <wrl.h>
#include <Xinput.h>

#include <array>
#include <cstdint>

// マウスボタンを、Win32の仮想キー番号を意識せず指定するための列挙型。
enum class MouseButton : uint8_t {
    Left,
    Right,
    Middle,
    X1,
    X2,
};

struct GamepadStick {
    float x;
    float y;
};

// DirectInputキーボード・マウスと、XInputゲームパッドの状態を管理する。
// Pressed=押している間、Triggered=押した瞬間、Released=離した瞬間。
class InputManager {
public:
    InputManager() = default;
    ~InputManager();

    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    void Initialize(HWND hWnd);

    // Engine::ProcessMessageから1フレームに1回呼ぶ。
    void Update();

    bool IsKeyPressed(int virtualKey) const;
    bool IsKeyTriggered(int virtualKey) const;
    bool IsKeyReleased(int virtualKey) const;

    bool IsMousePressed(MouseButton button) const;
    bool IsMouseTriggered(MouseButton button) const;
    bool IsMouseReleased(MouseButton button) const;
    POINT GetMousePosition() const { return currentMousePosition_; }
    POINT GetMouseDelta() const;
    int GetMouseWheelDelta() const {
        return static_cast<int>(currentMouse_.lZ);
    }

    bool IsGamepadConnected() const { return isGamepadConnected_; }
    bool IsGamepadButtonPressed(WORD button) const;
    bool IsGamepadButtonTriggered(WORD button) const;
    bool IsGamepadButtonReleased(WORD button) const;
    GamepadStick GetLeftStick() const;
    GamepadStick GetRightStick() const;
    float GetLeftTrigger() const;
    float GetRightTrigger() const;

private:
    bool ReadDeviceState(
        IDirectInputDevice8* device,
        DWORD dataSize,
        void* destination);
    static bool IsDirectInputStateDown(BYTE state);
    static bool IsVirtualKeyDown(
        const std::array<BYTE, 256>& keyboardState,
        int virtualKey);
    static int VirtualKeyToDirectInputKey(int virtualKey);
    static size_t MouseButtonToIndex(MouseButton button);
    static float NormalizeThumbStick(SHORT value, SHORT deadZone);
    static float NormalizeTrigger(BYTE value);
    HWND hWnd_ = nullptr;

    // DirectInput本体と、システムキーボード／マウスを表すデバイス。
    Microsoft::WRL::ComPtr<IDirectInput8> directInput_;
    Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboardDevice_;
    Microsoft::WRL::ComPtr<IDirectInputDevice8> mouseDevice_;
    std::array<BYTE, 256> currentKeyboard_{};
    std::array<BYTE, 256> previousKeyboard_{};

    DIMOUSESTATE2 currentMouse_{};
    DIMOUSESTATE2 previousMouse_{};
    POINT currentMousePosition_{};

    XINPUT_STATE currentGamepad_{};
    XINPUT_STATE previousGamepad_{};
    bool isGamepadConnected_ = false;
};
