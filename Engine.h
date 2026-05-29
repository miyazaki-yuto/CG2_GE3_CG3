#pragma once
#include <Windows.h>
#include <cstdint>
#include <memory>
#include <fstream>
#include "Graphics.h"

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

	std::ofstream logStream_;
	std::unique_ptr<Graphics> graphics_;
};