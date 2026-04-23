#include <Windows.h>
#include <cstdint>
#include <string>
#include <filesystem>// ファイルとディレクトリを操作するための奴
#include <fstream> // ファイルの読み書きをするための奴
#include <chrono> // 時間を扱うための奴
#include <format> // 文字列のフォーマットをするための奴
#include <d3d12.h> // DirectX 12のヘッダー
#include <dxgi1_6.h> // DirectX Graphics Infrastructureのヘッダー
#include <cassert> // アサーションを使うためのヘッダー
#include <dbghelp.h>
#include <strsafe.h>

// libファイルのリンク
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib,"Dbghelp.lib")

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

// ワイド文字列を通常文字列に変換する
std::string ConvertString(const std::wstring& str) {
	if (str.empty()) { return std::string(); }
	int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
	std::string result(sizeNeeded, 0);
	WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
	return result;
}

static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception)
{
	//// 中身はこれから埋まる
	//return EXCEPTION_EXECUTE_HANDLER;

	// 時刻を獲得して時刻を名前に入れたファイルを作成。Dumpsディレクトリ以下に出力
	SYSTEMTIME time;
	GetLocalTime(&time);
	wchar_t filePath[MAX_PATH] = { 0 };
	CreateDirectory(L"./Dumps", nullptr);
	// ファイルパスを作成
	StringCchPrintfW(filePath, MAX_PATH, L"./Dumps/CrashDump_%04d%02d%02d_%02d%02d%02d.dmp",
		time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);

	// 作成↓ファイルパスを使ってファイルを作成
	HANDLE dumoFileHandle = CreateFile(filePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);

	// processId(exeのId)とクラッシュ(例外)の発生↓threadIdを取得
	DWORD processId = GetCurrentProcessId();
	DWORD threadId = GetCurrentThreadId();
	// 設定情報の入力
	MINIDUMP_EXCEPTION_INFORMATION minidumpInformation{ 0 };
	minidumpInformation.ThreadId = threadId;
	minidumpInformation.ExceptionPointers = exception;
	minidumpInformation.ClientPointers = TRUE;
	// Dumpを出力。MiniDumpNormalは最低限の情報を出力するフラグ
	MiniDumpWriteDump(GetCurrentProcess(), processId, dumoFileHandle, MiniDumpNormal, &minidumpInformation, nullptr, nullptr);
	// 他に関連づけされているSEH例外ハンドラがあれば実行。通常プロセスを終了する
	return EXCEPTION_EXECUTE_HANDLER;
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	// 誰も細くしなかった場合に(Unhandled),細くする関数を登録
	SetUnhandledExceptionFilter(ExportDump);

	OutputDebugStringA("Hello, DirectX\n");


	// ログのディレクトリを作成
	std::filesystem::create_directories("logs");

	// 現在の日時を取る
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

	// 秒単位にする
	std::chrono::time_point<std::chrono::system_clock,std::chrono::seconds>
		nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
	
	// タイムゾーンをローカルに変更する
	std::chrono::zoned_time localTime{ std::chrono::current_zone(), nowSeconds };

	// 日時を文字にする
	std::string dateString = std::format("{:%Y%m%d_%H%M%S}", localTime);

	// ファイルのパスを作る
	std::string logFilePath = std::string("logs/") + dateString + ".log";

	// ファイルのストリームを作る
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

	// DXGI ファクトリーの生成
	IDXGIFactory7* dxgiFactory = nullptr;

	// 関数が成功したか同化をSUCCEEDEDマクロで判定出来る
	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));

	// 初期化の根本的な部分でエラーがあったら危ないのでassertしとく
	assert(SUCCEEDED(hr));

	// 使用するアダプタ用の変数。最初にnullptrをしておく
	IDXGIAdapter4* useAdapter = nullptr;
	//fいい順にアダプタを頼む
	for(UINT i = 0; dxgiFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND; ++i)
	{
		// アダプターの情報を取得する
		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = useAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr)); // 取得できないは一大事
		// ソフトウェアアダプタ出なければ採用
		if(!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE))
		{
			// WCHAR配列をwstringに変えて
			//更にstringに変換してLogに渡す
			std::wstring adapterName = adapterDesc.Description;
			Log(logStream, std::format("Use Adapter: {}", ConvertString(adapterName)));
			break;
		}
		useAdapter = nullptr; // ソフトウェアアダプタの場合はみなかった事にする
	}
	// 適切なアダプタが見つからなかったので起動出来ない
	assert(useAdapter != nullptr);

	ID3D12Device* device = nullptr;
	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_2,D3D_FEATURE_LEVEL_12_1,D3D_FEATURE_LEVEL_12_0
	};
	const char* featureLevelStrings[] = {"12.2","12.1","12.0"};
	// 高い順に生成出来るか試していく
	for(size_t i = 0; i < _countof(featureLevels); ++i)
	{
		// 採用したアダプターでデバイスを生成
		hr = D3D12CreateDevice(useAdapter, featureLevels[i], IID_PPV_ARGS(&device));
		// 指定した機能レベルでデバイスが生成できたかを確認
		if(SUCCEEDED(hr))
		{
			// 生成できたのでログ出力を行ってループを抜ける
			Log(logStream, std::format("FeatureLevel : {}\n", featureLevelStrings[i]));
			break;
		}
	}

	// デバイスの生成が上手く行かなかったので起動できない
	assert(device != nullptr); 
	Log(logStream, "Complete create D3D12Device!!!\n");

	MSG msg{};

	//uint32_t* p = nullptr;
	//*p = 100;

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