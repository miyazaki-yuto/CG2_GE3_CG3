#include "Engine.h"
#include <filesystem>
#include <chrono>
#include <format>

LRESULT CALLBACK Engine::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

void Engine::Initialize(HINSTANCE hInstance, int nCmdShow)
{
	OutputDebugStringA("Hello, DirectX\n");

	// ログ設定
	std::filesystem::create_directories("logs");
	auto now = std::chrono::system_clock::now();
	auto nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
	std::chrono::zoned_time localTime{ std::chrono::current_zone(), nowSeconds };
	std::string dateString = std::format("{:%Y%m%d_%H%M%S}", localTime);
	logStream_.open(std::format("logs/{}.log", dateString));

	// ウィンドウクラスの登録
	WNDCLASS wc{};
	wc.lpfnWndProc = WindowProc;
	wc.lpszClassName = L"CG2WindowClass";
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	RegisterClass(&wc);

	RECT wrc = { 0, 0, kWindowWidth_, kWindowHeight_ };
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, FALSE);

	hWnd_ = CreateWindow(
		wc.lpszClassName, L"CG2", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		wrc.right - wrc.left, wrc.bottom - wrc.top,
		nullptr, nullptr, hInstance, nullptr);

	ShowWindow(hWnd_, nCmdShow);

	// Graphicsクラスの初期化
	graphics_ = std::make_unique<Graphics>();
	graphics_->Initialize(hWnd_, kWindowWidth_, kWindowHeight_, logStream_);
}

void Engine::Run()
{
	MSG msg{};
	bool initialLog = false;

	// メインループ
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			if (!initialLog) {
				logStream_ << "Loop Start" << std::endl;
				OutputDebugStringA("Loop Start\n");
				initialLog = true;
			}

			// --- ゲーム処理 ---

			// --- 描画処理 ---
			graphics_->BeginDraw(); // 画面クリアなど

			// ここにモデルやスプライトの描画コマンドを追加していく

			graphics_->EndDraw();   // 画面フリップなど
		}
	}
}