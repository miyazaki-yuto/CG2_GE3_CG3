#include <Windows.h>
#include <cstdint>
#include <string>
#include <filesystem>// ファイルとディレクトリを操作するための奴
#include <fstream> // ファイルの読み書きをするための奴
#include <chrono> // 時間を扱うための奴

//int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
//{
//	OutputDebugStringA("Hello, DirectX\n");
//
//	return 0;
//}

// ウィンドウプロシーシャ
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	// メッセージに応じた処理
	switch (message)
	{
		// ウィンドウが破棄されたときの処理
	case WM_DESTROY:
		//OSに対して、アプリケーションの終了を伝える
		PostQuitMessage(0);
		return 0;
	}

	// 標準のメッセージ処理を行う
	return DefWindowProc(hWnd, message, wParam, lParam);
}

// デバッグ出力用の関数
// ファイルを書き出す
void Log(std::ostream& os, const std::string& message) {
	os << message << std::endl;
	// メッセージに改行が勝手に付いて出力されるようにした
	std::string formatted = message + "\n";
	OutputDebugStringA(message.c_str());
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	OutputDebugStringA("Hello, DirectX\n");

	std::filesystem::create_directories("logs");

	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

	std::chrono::time_point<std::chrono::system_clock,std::chrono::seconds>
		nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
	
	std::chrono::zoned_time localTime{ std::chrono::current_zone(), nowSeconds };

	std::string dateString = std::format("{:%Y%m%d_%H%M%S}", localTime);

	std::string logFilePath = std::string("logs/") + dateString + ".log";

	std::ofstream logStream(logFilePath);

	// ウィンドウクラスの登録
	WNDCLASS wc{};
	// ウィンドウプロシーシャ
	wc.lpfnWndProc = WindowProc;
	// ウィンドウクラス名
	wc.lpszClassName = L"CG2WindowClass";
	// インスタンスハンドル
	wc.hInstance = GetModuleHandle(nullptr);
	// カーソル
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	// ウィンドウクラスを登録
	RegisterClass(&wc);



	// クライアント領域のサイズ
	const int32_t kWindowWidth = 1280;
	const int32_t kWindowHeight = 720;

	// ウィンドウサイズを表す
	RECT wrc = { 0, 0, kWindowWidth, kWindowHeight };

	// ウィンドウのサイズを決める
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, FALSE);

	// ウィンドの作成
	HWND hWnd = CreateWindow(
		wc.lpszClassName,	// 利用するクラス名
		L"CG2",				// タイトルバーの文字
		WS_OVERLAPPEDWINDOW, // よく見るウィンドウスタイル
		CW_USEDEFAULT,		// 表示X座標(Windowsに任せる)
		CW_USEDEFAULT,		// 表示Y座標(WindowsOSのに任せる)
		wrc.right - wrc.left, // ウィンドウの幅
		wrc.bottom - wrc.top, // ウィンドウの高さ
		nullptr,			// 親ウィンドウハンドル
		nullptr,			// メニューハンドル
		hInstance,			// インスタンスハンドル
		nullptr);			// オプション

	//ウィンドウを表示する
	ShowWindow(hWnd, nCmdShow);

	MSG msg{};

	// ウィンドウのxボタンが押されているまでループ
	while (msg.message != WM_QUIT)
	{
		if(PeekMessage(&msg,NULL,0,0,PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			// ゲーム処理


			// 書いたけど背景が真っ白だから見えねぇ↓
			//std::string str0{ "STRING!!" };
			//std::string str1{ std::to_string(10) };

			// ゲームループで1回だけログを出す
			static bool initialLog = false;
			if(!initialLog)
			{
				Log(logStream, "Loop Start");
				initialLog = true;
			}

		}
	}

	return 0;
}