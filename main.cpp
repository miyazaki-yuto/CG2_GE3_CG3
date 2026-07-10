#include <Windows.h>
#include "Engine.h"
#include "CommonTypes.h"
#include "externals/imgui/imgui.h"
#include "TriangleEffect.h" 


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

		graphics->LoadTexture("Resources/White.png");

		int selectedTexture[3] = { 0, 0 ,0 };
		const char* textureNames[] = { "uvChecker", "monsterBall" ,"White" };

		TransformData zeroTransform = {
			{0.0f, 0.0f, 0.0f},
			{0.0f, 0.0f, 0.0f},
			{0.0f, 0.0f, 0.0f} };

		//  Graphicsクラスのマップ済み頂点バッファへデータを転送
		graphics->SetTriangleVertices(0, vertices);
		graphics->SetTriangleVertices(1, vertices);

		// セット
		graphics->SetTriangleTransform(0, transform[0]);
		graphics->SetTriangleTransform(1, transform[1]);

		// スプライトの初期化
		TransformData spriteTransform = {
			{ 100.0f, 100.0f, 1.0f }, // Scale 
			{ 0.0f, 0.0f, 0.0f },     // Rotate
			{ 200.0f, 200.0f, 0.0f }  // Translate 
		};
		int selectedSpriteTexture = 0;
		Vector4 spriteColor = { 1.0f, 1.0f, 1.0f, 1.0f };

		// セット
		graphics->SetSpriteTransform(0, spriteTransform);
		graphics->SetSpriteTexture(0, selectedSpriteTexture);
		graphics->SetSpriteColor(0, spriteColor);

		graphics->InitializeDrawSphere();

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

			graphics->Update();
			transform[0].rotate.y -= 0.01f;
			transform[1].rotate.x -= 0.01f;

			//================//
			// -- 描画処理 -- //
			//================//
			// 1. 描画準備
			graphics->BeginDraw();

			// 球の操作
			ImGui::Begin("Sphere Control");
			if (ImGui::DragFloat3("Sphere Scale", &sphereTransform.scale.x, 0.1f, 0.01f, 100.0f) ||
				ImGui::SliderFloat3("Sphere Rotate", &sphereTransform.rotate.x, -3.1415f, 3.1415f) ||
				ImGui::DragFloat3("Sphere Translate", &sphereTransform.translate.x, 0.1f)) {
				graphics->SetSphereTransform(sphereTransform);
			}

			if (ImGui::ColorEdit4("Sphere Color", &sphereColor.x)) {
				graphics->SetSphereColor(sphereColor);
			}

			if (ImGui::Combo("Sphere Texture", &selectedSphereTexture, textureNames, _countof(textureNames))) {
				graphics->SetSphereTexture(selectedSphereTexture);
			}
			ImGui::End();
			graphics->SetSphereTransform(sphereTransform);


			ImGui::Begin("Sprite Control");

			bool spriteChanged = false;
			spriteChanged |= ImGui::DragFloat2("Sprite Scale (Size)", &spriteTransform.scale.x, 1.0f, 1.0f, 1280.0f);
			spriteChanged |= ImGui::SliderFloat("Sprite Rotate Z", &spriteTransform.rotate.z, -3.1415f, 3.1415f);
			spriteChanged |= ImGui::DragFloat2("Sprite Position", &spriteTransform.translate.x, 1.0f, 0.0f, 1280.0f);

			// トランスフォームに変更があったらGraphicsに通知
			if (spriteChanged) {
				graphics->SetSpriteTransform(0, spriteTransform);
			}

			// 色の変更
			if (ImGui::ColorEdit4("Sprite Color", &spriteColor.x)) {
				graphics->SetSpriteColor(0, spriteColor);
			}

			// テクスチャの変更
			if (ImGui::Combo("Sprite Texture", &selectedSpriteTexture, textureNames, _countof(textureNames))) {
				graphics->SetSpriteTexture(0, selectedSpriteTexture);
			}

			ImGui::End();

			ImGui::Begin("Settings");
			if (ImGui::ColorEdit4("Triangle Color", &color.x)) {
				graphics->SetColor(color);
			}

			// 1つ目の三角形用のUI
			ImGui::Separator();
			ImGui::Text("Triangle 1 Transform");
			if (ImGui::Combo("Texture 1", &selectedTexture[0], textureNames, _countof(textureNames))) {
				graphics->SetTriangleTexture(0, selectedTexture[0]);
			}
			if (ImGui::DragFloat3("Scale 1", &transform[0].scale.x, 0.1f, 0.01f, 100.0f) ||
				ImGui::SliderFloat3("Rotate 1", &transform[0].rotate.x, -3.1415f, 3.1415f) ||
				ImGui::DragFloat3("Translate 1", &transform[0].translate.x, 0.1f)) {
				graphics->SetTriangleTransform(0, transform[0]);
			}

			// 2つ目の三角形用のUI
			ImGui::Separator();
			ImGui::Text("Triangle 2 Transform");
			if (ImGui::Combo("Texture 2", &selectedTexture[1], textureNames, _countof(textureNames))) {
				graphics->SetTriangleTexture(1, selectedTexture[1]);
			}
			if (ImGui::DragFloat3("Scale 2", &transform[1].scale.x, 0.1f, 0.01f, 100.0f) ||
				ImGui::SliderFloat3("Rotate 2", &transform[1].rotate.x, -3.1415f, 3.1415f) ||
				ImGui::DragFloat3("Translate 2", &transform[1].translate.x, 0.1f)) {
				graphics->SetTriangleTransform(1, transform[1]);
			}

			ImGui::End();

			graphics->SetColor(color);
			graphics->SetTriangleTexture(0, selectedTexture[0]);
			graphics->SetTriangleTexture(1, selectedTexture[1]);
			graphics->SetTriangleTransform(0, transform[0]);
			graphics->SetTriangleTransform(1, transform[1]);


			zeroTransform = {
				{0.0f, 0.0f, 0.0f},
				{0.0f, 0.0f, 0.0f},
				{0.0f, 0.0f, 0.0f} };

			for (int i = 2; i < kTriangleMaxCount; ++i) {
				graphics->SetTriangleTransform(i, zeroTransform);
			}

			// 実際のポリゴン描画
			graphics->Draw();
			graphics->DrawSprites();
			graphics->DrawSphere();

			// 画面フリップ
			graphics->EndDraw();

		}
	}

	CoUninitialize();
	return 0;
}