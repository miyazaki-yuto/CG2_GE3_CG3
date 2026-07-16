#pragma once
#include <Windows.h>
#include <cstdint>
#include <memory>
#include <fstream>
#include "DirectXCommon.h"
#include "Graphics.h"

// ウィンドウ、メッセージループ、DirectX初期化の順序を管理するアプリケーションの土台。
class Engine
{
public:
	Engine() = default;
	~Engine();

	void Initialize(HINSTANCE hInstance, int nCmdShow);
	bool ProcessMessage();
	Graphics* GetGraphics() const { return graphics_.get(); }

	static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception);

private:
	static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
	HWND hWnd_ = nullptr;
	const int32_t kWindowWidth_ = 1280;
	const int32_t kWindowHeight_ = 720;

	// シェーダーコンパイルなどの実行ログを書き出す。
	std::ofstream logStream_;
	std::unique_ptr<DirectXCommon> dxCommon_;
	std::unique_ptr<Graphics> graphics_;
};
