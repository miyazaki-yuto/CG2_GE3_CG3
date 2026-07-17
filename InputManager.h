#pragma once

#include <Windows.h>
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

// キーボード・マウス・XInputゲームパッドの現在値と前フレーム値を管理する。
// Pressed=押している間、Triggered=押した瞬間、Released=離した瞬間。
class InputManager {
public:
    InputManager() = default;
    ~InputManager() = default;

    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    void Initialize(HWND hWnd);

    // Engine::ProcessMessageから1フレームに1回呼ぶ。
    void Update();

    // WM_MOUSEWHEELなど、ポーリングだけでは取れない入力をWindowProcから受け取る。
    void HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    bool IsKeyPressed(int virtualKey) const;
    bool IsKeyTriggered(int virtualKey) const;
    bool IsKeyReleased(int virtualKey) const;

    bool IsMousePressed(MouseButton button) const;
    bool IsMouseTriggered(MouseButton button) const;
    bool IsMouseReleased(MouseButton button) const;
    POINT GetMousePosition() const { return currentMousePosition_; }
    POINT GetMouseDelta() const;
    int GetMouseWheelDelta() const { return mouseWheelDelta_; }

    bool IsGamepadConnected() const { return isGamepadConnected_; }
    bool IsGamepadButtonPressed(WORD button) const;
    bool IsGamepadButtonTriggered(WORD button) const;
    bool IsGamepadButtonReleased(WORD button) const;
    GamepadStick GetLeftStick() const;
    GamepadStick GetRightStick() const;
    float GetLeftTrigger() const;
    float GetRightTrigger() const;

private:
    static bool IsKeyboardStateDown(BYTE state);
    static int MouseButtonToVirtualKey(MouseButton button);
    static float NormalizeThumbStick(SHORT value, SHORT deadZone);
    static float NormalizeTrigger(BYTE value);
    bool IsValidVirtualKey(int virtualKey) const;

    HWND hWnd_ = nullptr;
    bool isWindowActive_ = true;

    std::array<BYTE, 256> currentKeyboard_{};
    std::array<BYTE, 256> previousKeyboard_{};

    POINT currentMousePosition_{};
    POINT previousMousePosition_{};
    int pendingMouseWheelDelta_ = 0;
    int mouseWheelDelta_ = 0;

    XINPUT_STATE currentGamepad_{};
    XINPUT_STATE previousGamepad_{};
    bool isGamepadConnected_ = false;
};
