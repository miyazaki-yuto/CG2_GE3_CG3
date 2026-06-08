#include <Windows.h>
#include "Engine.h"
#include "CommonTypes.h"
#include "externals/imgui/imgui.h"
#include "TriangleEffect.h" 

enum Scene {
	KADAI_SCENE,
	EFFECT_SCENE,
};

int scene = KADAI_SCENE;


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
			{ {1.0f, 1.0f, 1.0f}, {0.0f, -3.0f, 0.0f}, {0.0f, 0.0f, 0.0f} },
			{ {1.0f, 1.0f, 1.0f}, {0.0f, -3.0f, 0.0f}, {0.0f, 0.0f, 0.0f} }
		};

		graphics->LoadTexture("Resources/White.png");

		int selectedTexture[3] = { 0, 0 ,0};
		const char* textureNames[] = { "uvChecker", "monsterBall" ,"White"};

		TextureVertexData vertices[3] = {
			{ { -0.5f, -0.5f, 0.0f, 1.0f }, { 0.0f, 1.0f } }, // 左下
			{ {  0.0f,  0.5f, 0.0f, 1.0f }, { 0.5f, 0.0f } }, // 上
			{ {  0.5f, -0.5f, 0.0f, 1.0f }, { 1.0f, 1.0f } }  // 右下
		};

		TransformData zeroTransform = {
			{0.0f, 0.0f, 0.0f},
			{0.0f, 0.0f, 0.0f},
			{0.0f, 0.0f, 0.0f} };

		//  Graphicsクラスのマップ済み頂点バッファへデータを転送
		graphics->SetTriangleVertices(0, vertices);
		graphics->SetTriangleVertices(1, vertices);

		// ImGuiを触る前の初期の座標・回転・スケール
		graphics->SetTriangleTransform(0, transform[0]);
		graphics->SetTriangleTransform(1, transform[1]);


		TriangleEffect effect;
		effect.Initialize(graphics);

		OutputDebugStringA("Loop Start\n");

		while (engine.ProcessMessage())
		{
			//============//
			// ゲーム処理 // 
			//============//

			// エフェクトの更新 
			if (scene == EFFECT_SCENE) {
				effect.Update();
			}

			graphics->Update();

			//================//
			// -- 描画処理 -- //
			//================//
			// 1. 描画準備
			graphics->BeginDraw();

			ImGui::Begin("Scene Configuration");
			const char* sceneNames[] = { "Kadai Scene", "Effect Scene" };
			ImGui::Combo("Select Scene", &scene, sceneNames, _countof(sceneNames));
			ImGui::End();

			switch (scene)
			{
			case KADAI_SCENE:
				// 課題シーンの処理
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
				effect.Reset();


				break;

			case EFFECT_SCENE:

				// エフェクトシーンの処理
				ImGui::Begin("EffectControl");

				// パラメータ調整用
				EffectParameters params = effect.GetParameters();
				bool changed = false;

				if (ImGui::Button("Play / Reset")) {
					effect.Reset();
				}
				ImGui::Separator();
				changed |= ImGui::ColorEdit4("Flower Color", &params.color.x);
				changed |= ImGui::SliderInt("Petals", &params.numPetals, 1, 100);
				changed |= ImGui::SliderFloat("Duration", &params.duration, 0.1f, 10.0f);
				changed |= ImGui::SliderFloat("Max Radius", &params.maxRadius, 0.0f, 10.0f);
				changed |= ImGui::SliderFloat("Petal Scale", &params.petalScale, 0.01f, 2.0f);
				changed |= ImGui::SliderFloat("Spread Angle", &params.spreadAngle, 0.0f, 3.1415f);
				changed |= ImGui::SliderFloat("Spin Speed", &params.spinSpeed, -10.0f, 10.0f);
				changed |= ImGui::SliderFloat("Pulse Speed", &params.pulseSpeed, 0.0f, 20.0f);
				changed |= ImGui::SliderFloat("Pulse Magnitude", &params.pulseMagnitude, 0.0f, 2.0f);
				changed |= ImGui::SliderFloat("Stagger Delay", &params.staggerDelay, 0.0f, 3.0f);

				if (changed) {
					effect.SetParameters(params);
					// パラメータが変わったらリセットして見せる
					effect.Reset();
				}

				ImGui::End();
				break;
			}

			if (scene == EFFECT_SCENE) {
				effect.Draw();
			}


			// 実際のポリゴン描画
			graphics->Draw();

			// 画面フリップ
			graphics->EndDraw();

		}
	}

	CoUninitialize();
	return 0;
}