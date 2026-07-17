#pragma once
#include <Windows.h>
#include <cstdint>
#include <memory>
#include <fstream>
#include "DirectXCommon.h"
#include "GameTimer.h"
#include "Graphics.h"

class AudioManager;
class InputManager;

// ウィンドウ、メッセージループ、DirectX初期化の順序を管理するアプリケーションの土台。
class Engine
{
public:
	Engine() = default;
	~Engine();

	void Initialize(HINSTANCE hInstance, int nCmdShow);
	bool ProcessMessage();
	Graphics* GetGraphics() const { return graphics_.get(); }
	AudioManager* GetAudioManager() const { return audioManager_.get(); }
	InputManager* GetInputManager() const { return inputManager_.get(); }
	float GetDeltaTime() const { return gameTimer_.GetDeltaTime(); }
	double GetTotalTime() const { return gameTimer_.GetTotalTime(); }

	static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception);

private:
	static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
	void ApplyPendingResize();

private:
	HWND hWnd_ = nullptr;
	const int32_t kWindowWidth_ = 1280;
	const int32_t kWindowHeight_ = 720;
	uint32_t pendingResizeWidth_ = 0;
	uint32_t pendingResizeHeight_ = 0;
	bool hasPendingResize_ = false;
	GameTimer gameTimer_;

	// シェーダーコンパイルなどの実行ログを書き出す。
	std::ofstream logStream_;
	std::unique_ptr<DirectXCommon> dxCommon_;
	std::unique_ptr<Graphics> graphics_;
	std::unique_ptr<AudioManager> audioManager_;
	std::unique_ptr<InputManager> inputManager_;
};
