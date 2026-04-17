#include <Windows.h>
#include <cstdint>
#include <string>

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
void Log(const std::string& message){
	// メッセージに改行が勝手に付いて出力されるようにした
	std::string formatted = message + "\n";
	OutputDebugStringA(message.c_str());
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	OutputDebugStringA("Hello, DirectX\n");

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

			// 起動中にVANANAと無限に出力されるようにした
			Log("VANANA");
		}
	}

	return 0;
}