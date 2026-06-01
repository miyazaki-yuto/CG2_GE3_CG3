#include "Engine.h"
#include <filesystem>
#include <chrono>
#include <format>
#ifdef USE_IMGUI
#include <DbgHelp.h>
#include <strsafe.h>
#pragma comment(lib, "Dbghelp.lib")
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_win32.h"
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
// ImGuiのメッセージハンドラーの宣言
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

LRESULT CALLBACK Engine::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
#ifdef USE_IMGUI
	// メッセージは最初にImGuiに渡す
	if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam)) {
		return true;
	}
#endif

	switch (message)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

Engine::~Engine()
{
	// Engineが持つ graphics_ (std::unique_ptr) をここで明示的に解放する
	// これにより、COMオブジェクトの解放がリークチェックより先に確実に行われます
	graphics_.reset();

	// リソースリークチェック
	IDXGIDebug1* debug = nullptr;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug))))
	{
		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		debug->Release();
	}
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

bool Engine::ProcessMessage()
{
	MSG msg{};
	// メッセージがある限りループして処理
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
	{
		// ウィンドウの×ボタンなどが押され、終了メッセージが来たら false を返す
		if (msg.message == WM_QUIT)
		{
			return false;
		}
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	// 終了メッセージが来ていなければ true を返す（ゲーム続行）
	return true;
}

LONG WINAPI Engine::ExportDump(EXCEPTION_POINTERS* exception)
{
	SYSTEMTIME time;
	GetLocalTime(&time);
	wchar_t filePath[MAX_PATH] = { 0 };
	CreateDirectory(L"./Dumps", nullptr);
	StringCchPrintfW(filePath, MAX_PATH, L"./Dumps/CrashDump_%04d%02d%02d_%02d%02d%02d.dmp",
		time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);

	HANDLE dumoFileHandle = CreateFile(filePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);

	DWORD processId = GetCurrentProcessId();
	DWORD threadId = GetCurrentThreadId();

	MINIDUMP_EXCEPTION_INFORMATION minidumpInformation{ 0 };
	minidumpInformation.ThreadId = threadId;
	minidumpInformation.ExceptionPointers = exception;
	minidumpInformation.ClientPointers = TRUE;

	MiniDumpWriteDump(GetCurrentProcess(), processId, dumoFileHandle, MiniDumpNormal, &minidumpInformation, nullptr, nullptr);
	return EXCEPTION_EXECUTE_HANDLER;
}