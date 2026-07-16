#include <Windows.h>
#include "Engine.h"
#include "PrimitiveDrawer.h"
#include "Sprite.h"
#include "CommonTypes.h"
#include "externals/imgui/imgui.h"
#include "TriangleEffect.h" 
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")


int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE /*hPrevInstance*/, _In_ LPSTR /*lpCmdLine*/, _In_ int nCmdShow)
{
	// クラッシュダンプの登録
	SetUnhandledExceptionFilter(Engine::ExportDump);

	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(hr)) {
		return -1;
	}

	// エンジンの起動とゲームループ
	{
		Engine engine;
		engine.Initialize(hInstance, nCmdShow);

		Graphics* graphics = engine.GetGraphics();
		// Graphicsが所有する描画クラスを借りる。生成・破棄はGraphicsが担当する。
		PrimitiveDrawer* primitiveDrawer = graphics->GetPrimitiveDrawer();
		Sprite* sprite = graphics->GetSprite();

		Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
		TransformData transform[2] = {
			{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} },
			{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} }
		};

		TextureVertexData vertices[3] = {
			{ { -0.5f, -0.5f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } }, // 左下
			{ {  0.0f,  0.5f, 0.0f, 1.0f }, { 0.5f, 0.0f }, { 0.0f, 0.0f, -1.0f } }, // 上
			{ {  0.5f, -0.5f, 0.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } }  // 右下
		};

		// TextureManagerが実体を所有し、mainは返されたハンドルだけを保持する。
		const int uvCheckerTextureHandle = graphics->LoadTexture("Resources/uvChecker.png");
		const int monsterBallTextureHandle = graphics->LoadTexture("Resources/monsterBall.png");
		const int whiteTextureHandle = graphics->LoadTexture("Resources/White.png");

		// ImGuiの選択番号とTextureManagerのハンドルを明確に分ける。
		const int textureHandles[] = {
			uvCheckerTextureHandle,
			monsterBallTextureHandle,
			whiteTextureHandle
		};
		const char* textureNames[] = { "uvChecker", "monsterBall", "White" };
		int selectedTriangleTexture[2] = { 0, 0 };

		// スプライトの初期化
		TransformData spriteTransform = {
			{ 100.0f, 100.0f, 1.0f }, // Scale 
			{ 0.0f, 0.0f, 0.0f },     // Rotate
			{ 200.0f, 200.0f, 0.0f }  // Translate 
		};
		int selectedSpriteTexture = 0;
		Vector4 spriteColor = { 1.0f, 1.0f, 1.0f, 1.0f };

		TransformData sphereTransform = {
			{ 1.0f, 1.0f, 1.0f }, // Scale 
			{ 0.0f, 0.0f, 0.0f }, // Rotate
			{ 2.0f, 0.0f, 0.0f }  // Translate
		};
		int selectedSphereTexture = 1; // monsterBall など
		Vector4 sphereColor = { 1.0f, 1.0f, 1.0f, 1.0f };

		OutputDebugStringA("Loop Start\n");

		while (engine.ProcessMessage())
		{
			//============//
			// ゲーム処理 // 
			//============//

			transform[0].rotate.y -= 0.01f;
			transform[1].rotate.x -= 0.01f;

			//================//
			// -- 描画処理 -- //
			//================//
			// 1. バックバッファをクリアし、描画コマンドの記録を開始する。
			graphics->BeginDraw();

			// 球の操作
			ImGui::Begin("Sphere Control");
			ImGui::DragFloat3("Sphere Scale", &sphereTransform.scale.x, 0.1f, 0.01f, 100.0f);
			ImGui::SliderFloat3("Sphere Rotate", &sphereTransform.rotate.x, -3.1415f, 3.1415f);
			ImGui::DragFloat3("Sphere Translate", &sphereTransform.translate.x, 0.1f);
			ImGui::ColorEdit4("Sphere Color", &sphereColor.x);

			ImGui::Combo("Sphere Texture", &selectedSphereTexture, textureNames, _countof(textureNames));
			ImGui::End();


			ImGui::Begin("Sprite Control");

			ImGui::DragFloat2("Sprite Scale (Size)", &spriteTransform.scale.x, 1.0f, 1.0f, 1280.0f);
			ImGui::SliderFloat("Sprite Rotate Z", &spriteTransform.rotate.z, -3.1415f, 3.1415f);
			ImGui::DragFloat2("Sprite Position", &spriteTransform.translate.x, 1.0f, 0.0f, 1280.0f);

			ImGui::ColorEdit4("Sprite Color", &spriteColor.x);

			// テクスチャの変更
			ImGui::Combo("Sprite Texture", &selectedSpriteTexture, textureNames, _countof(textureNames));

			ImGui::End();

			ImGui::Begin("Settings");
			ImGui::ColorEdit4("Triangle Color", &color.x);

			// 1つ目の三角形用のUI
			ImGui::Separator();
			ImGui::Text("Triangle 1 Transform");
			ImGui::Combo("Texture 1", &selectedTriangleTexture[0], textureNames, _countof(textureNames));
			ImGui::DragFloat3("Scale 1", &transform[0].scale.x, 0.1f, 0.01f, 100.0f);
			ImGui::SliderFloat3("Rotate 1", &transform[0].rotate.x, -3.1415f, 3.1415f);
			ImGui::DragFloat3("Translate 1", &transform[0].translate.x, 0.1f);

			// 2つ目の三角形用のUI
			ImGui::Separator();
			ImGui::Text("Triangle 2 Transform");
			ImGui::Combo("Texture 2", &selectedTriangleTexture[1], textureNames, _countof(textureNames));
			ImGui::DragFloat3("Scale 2", &transform[1].scale.x, 0.1f, 0.01f, 100.0f);
			ImGui::SliderFloat3("Rotate 2", &transform[1].rotate.x, -3.1415f, 3.1415f);
			ImGui::DragFloat3("Translate 2", &transform[1].translate.x, 0.1f);

			ImGui::End();

			// 2. 3Dを先に描き、深度を使わないSpriteを最後に重ねる。
			primitiveDrawer->DrawTriangle(
				vertices, transform[0], color, textureHandles[selectedTriangleTexture[0]]);

			primitiveDrawer->DrawTriangle(
				vertices, transform[1], color, textureHandles[selectedTriangleTexture[1]]);

			primitiveDrawer->DrawSphere(
				sphereTransform, sphereColor, textureHandles[selectedSphereTexture]);

			sprite->Draw(
				spriteTransform, spriteColor, textureHandles[selectedSpriteTexture]);


			// 3. コマンドをGPUへ実行させ、描画済みバックバッファを画面へ表示する。
			graphics->EndDraw();

		}
	}

#ifdef _DEBUG
	Microsoft::WRL::ComPtr<IDXGIDebug1> dxgiDebug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug)))) {
		// 生き残っているオブジェクトを詳細に出力ウィンドウに表示する
		dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
	}
#endif

	CoUninitialize();
	return 0;
}
