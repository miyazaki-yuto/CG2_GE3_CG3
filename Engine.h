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
	~Engine() = default;

	void Initialize(HINSTANCE hInstance, int nCmdShow);
	void Run();

private:
	static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
	HWND hWnd_ = nullptr;
	const int32_t kWindowWidth_ = 1280;
	const int32_t kWindowHeight_ = 720;

	std::ofstream logStream_;
	std::unique_ptr<Graphics> graphics_;
};