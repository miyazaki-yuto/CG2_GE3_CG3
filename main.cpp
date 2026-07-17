#include <Windows.h>
#include "Engine.h"
#include "AudioManager.h"
#include "DebugCamera.h"
#include "InputManager.h"
#include "LightingManager.h"
#include "PrimitiveDrawer.h"
#include "Sprite.h"
#include "Model.h"
#include "CommonTypes.h"
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif
#include "TriangleEffect.h" 

#include <cmath>

#ifdef _DEBUG
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif


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
		DebugCamera* debugCamera = graphics->GetDebugCamera();
		Sprite* sprite = graphics->GetSprite();
#ifdef USE_IMGUI
		LightingManager* lightingManager = graphics->GetLightingManager();
#endif
		AudioManager* audioManager = engine.GetAudioManager();
		InputManager* inputManager = engine.GetInputManager();

		// BGMはゲーム側がハンドルを所有し、起動時にループ再生する。
		float bgmVolume = 0.25f;
		int bgmHandle = -1;
		if (audioManager != nullptr) {
			bgmHandle = audioManager->LoadWave("Resources/bgm.wav");
			if (bgmHandle >= 0) {
				audioManager->SetVolume(bgmHandle, bgmVolume);
				audioManager->Play(bgmHandle, true);
			}
		}

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

		// GraphicsのFactoryでOBJを読み込み、Model自身に頂点・インデックスを所有させる。
		std::unique_ptr<Model> cubeModel =
			graphics->CreateModel("Resources/Models/cube.obj");

		// ImGuiの選択番号とTextureManagerのハンドルを明確に分ける。
		const int textureHandles[] = {
			uvCheckerTextureHandle,
			monsterBallTextureHandle,
			whiteTextureHandle
		};
#ifdef USE_IMGUI
		// テクスチャ名はImGuiのコンボボックスでのみ使用する。
		const char* textureNames[] = { "uvChecker", "monsterBall", "White" };
#endif
		int selectedTriangleTexture[2] = { 0, 0 };
		// 三角形ごとに独立したUV変換を持たせる。
		UVTransform triangleUVTransform[2] = {
			{ { 1.0f, 1.0f }, 0.0f, { 0.0f, 0.0f } },
			{ { 1.0f, 1.0f }, 0.0f, { 0.0f, 0.0f } }
		};

		// スプライトの初期化
		TransformData spriteTransform = {
			{ 100.0f, 100.0f, 1.0f }, // Scale 
			{ 0.0f, 0.0f, 0.0f },     // Rotate
			{ 200.0f, 200.0f, 0.0f }  // Translate 
		};
		int selectedSpriteTexture = 0;
		Vector4 spriteColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		UVTransform spriteUVTransform = {
			{ 1.0f, 1.0f }, 0.0f, { 0.0f, 0.0f }
		};

		TransformData sphereTransform = {
			{ 1.0f, 1.0f, 1.0f }, // Scale 
			{ 0.0f, 0.0f, 0.0f }, // Rotate
			{ 2.0f, 0.0f, 0.0f }  // Translate
		};
		int selectedSphereTexture = 1; // monsterBall など
		Vector4 sphereColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		UVTransform sphereUVTransform = {
			{ 1.0f, 1.0f }, 0.0f, { 0.0f, 0.0f }
		};

		TransformData modelTransform = {
			{ 0.75f, 0.75f, 0.75f },
			{ 0.0f, 0.5f, 0.0f },
			{ -1.5f, 0.0f, 0.0f }
		};
		Vector4 modelColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		UVTransform modelUVTransform = {
			{ 1.0f, 1.0f }, 0.0f, { 0.0f, 0.0f }
		};
		int selectedModelTexture = 0;

		OutputDebugStringA("Loop Start\n");

		while (engine.ProcessMessage())
		{
			//============//
			// ゲーム処理 // 
			//============//

			// Engineが実測した秒数。速度に掛けることで、60FPSでも144FPSでも同じ速さになる。
			const float deltaTime = engine.GetDeltaTime();
			constexpr float kTriangleRotateSpeed = 0.6f; // 1秒あたりの回転量（ラジアン）
			transform[0].rotate.y -= kTriangleRotateSpeed * deltaTime;
			transform[1].rotate.x -= kTriangleRotateSpeed * deltaTime;

			if (inputManager != nullptr) {
				// W/A/S/Dとゲームパッド左スティックを、同じ移動処理へまとめる。
				GamepadStick leftStick = inputManager->GetLeftStick();
				float moveX = leftStick.x;
				float moveY = leftStick.y;
				if (inputManager->IsKeyPressed('A')) {
					moveX -= 1.0f;
				}
				if (inputManager->IsKeyPressed('D')) {
					moveX += 1.0f;
				}
				if (inputManager->IsKeyPressed('W')) {
					moveY += 1.0f;
				}
				if (inputManager->IsKeyPressed('S')) {
					moveY -= 1.0f;
				}
				// 斜め入力やキーボード＋スティックで長さが1を超えないよう正規化する。
				const float moveLength = std::sqrt(moveX * moveX + moveY * moveY);
				if (moveLength > 1.0f) {
					moveX /= moveLength;
					moveY /= moveLength;
				}

				constexpr float kModelMoveSpeed = 3.0f; // 1秒あたり3ワールド単位
				modelTransform.translate.x += moveX * kModelMoveSpeed * deltaTime;
				modelTransform.translate.y += moveY * kModelMoveSpeed * deltaTime;

				// SpaceまたはゲームパッドAで、BGMの一時停止と再開を切り替える。
				const bool toggleBgm = inputManager->IsKeyTriggered(VK_SPACE) ||
					inputManager->IsGamepadButtonTriggered(XINPUT_GAMEPAD_A);
				if (toggleBgm && audioManager != nullptr && bgmHandle >= 0) {
					if (audioManager->IsPlaying(bgmHandle)) {
						audioManager->Pause(bgmHandle);
					} else if (audioManager->IsPaused(bgmHandle)) {
						audioManager->Resume(bgmHandle);
					} else {
						audioManager->Play(bgmHandle, true);
					}
				}
			}

			//================//
			// -- 描画処理 -- //
			//================//
			// 1. バックバッファをクリアし、描画コマンドの記録を開始する。
			graphics->BeginDraw();

			// ReleaseではImGuiが存在しないため常に操作を許可し、
			// DebugではImGui操作中だけカメラのマウス入力を止める。
			bool allowDebugCameraMouseControl = true;
#ifdef USE_IMGUI
			allowDebugCameraMouseControl = !ImGui::GetIO().WantCaptureMouse;
#endif
			if (debugCamera != nullptr && inputManager != nullptr) {
				debugCamera->Update(*inputManager, allowDebugCameraMouseControl);
			}

#ifdef USE_IMGUI
			ImGui::Begin("Debug Camera");
			if (debugCamera != nullptr) {
				const Vector3& cameraPosition = debugCamera->GetPosition();
				const Vector3& cameraTarget = debugCamera->GetTarget();
				ImGui::TextUnformatted("MMB: Orbit");
				ImGui::TextUnformatted("Shift + MMB: Pan");
				ImGui::TextUnformatted("Ctrl + MMB / Wheel: Zoom");
				ImGui::TextUnformatted("Numpad 1 / 3 / 7: Front / Right / Top");
				ImGui::TextUnformatted("Ctrl + Numpad 1 / 3 / 7: Opposite View");
				ImGui::TextUnformatted("Numpad 5: Perspective / Orthographic");
				ImGui::TextUnformatted("Home: Reset Camera");
				ImGui::Separator();
				ImGui::Text("Position: %.2f, %.2f, %.2f",
					cameraPosition.x, cameraPosition.y, cameraPosition.z);
				ImGui::Text("Target: %.2f, %.2f, %.2f",
					cameraTarget.x, cameraTarget.y, cameraTarget.z);
				ImGui::Text("Distance: %.2f", debugCamera->GetDistance());
				ImGui::Text("Projection: %s",
					debugCamera->IsOrthographic() ? "Orthographic" : "Perspective");
				if (ImGui::Button("Reset Camera")) {
					debugCamera->Reset();
				}
			}
			ImGui::End();

			// BlenderのLightプロパティに相当する、シーン共通ライトの編集画面。
			ImGui::Begin("Lighting");
			if (lightingManager != nullptr) {
				DirectionalLight directionalLight =
					lightingManager->GetDirectionalLight();
				bool directionalEnabled = directionalLight.enabled != 0;
				ImGui::SeparatorText("Sun");
				ImGui::Checkbox("Enable Sun", &directionalEnabled);
				directionalLight.enabled = directionalEnabled ? 1 : 0;
				ImGui::ColorEdit3("Sun Color", &directionalLight.color.r);
				ImGui::DragFloat3(
					"Sun Direction", &directionalLight.direction.x, 0.01f, -1.0f, 1.0f);
				ImGui::DragFloat(
					"Sun Intensity", &directionalLight.intensity, 0.01f, 0.0f, 20.0f);
				lightingManager->SetDirectionalLight(directionalLight);

				PointLight pointLight = lightingManager->GetPointLight();
				bool pointEnabled = pointLight.enabled != 0;
				ImGui::SeparatorText("Point Light");
				ImGui::Checkbox("Enable Point Light", &pointEnabled);
				pointLight.enabled = pointEnabled ? 1 : 0;
				ImGui::ColorEdit3("Point Color", &pointLight.color.r);
				// Positionを動かすと、各ピクセルまでの方向と距離がシェーダー内で再計算される。
				ImGui::DragFloat3(
					"Point Position", &pointLight.position.x, 0.05f, -100.0f, 100.0f);
				ImGui::DragFloat(
					"Point Intensity", &pointLight.intensity, 0.05f, 0.0f, 100.0f);
				ImGui::DragFloat(
					"Point Radius", &pointLight.radius, 0.05f, 0.01f, 100.0f);
				ImGui::DragFloat(
					"Point Decay", &pointLight.decay, 0.05f, 0.01f, 8.0f);
				lightingManager->SetPointLight(pointLight);
			}
			ImGui::End();

			ImGui::Begin("Input State");
			if (inputManager != nullptr) {
				const POINT mousePosition = inputManager->GetMousePosition();
				const POINT mouseDelta = inputManager->GetMouseDelta();
				const GamepadStick leftStick = inputManager->GetLeftStick();
				const GamepadStick rightStick = inputManager->GetRightStick();

				ImGui::Text(
					"Keyboard W/A/S/D: %s %s %s %s",
					inputManager->IsKeyPressed('W') ? "W" : "-",
					inputManager->IsKeyPressed('A') ? "A" : "-",
					inputManager->IsKeyPressed('S') ? "S" : "-",
					inputManager->IsKeyPressed('D') ? "D" : "-");
				ImGui::Text("Space Trigger: %s",
					inputManager->IsKeyTriggered(VK_SPACE) ? "ON" : "OFF");
				ImGui::Separator();
				ImGui::Text("Mouse Position: %ld, %ld", mousePosition.x, mousePosition.y);
				ImGui::Text("Mouse Delta: %ld, %ld", mouseDelta.x, mouseDelta.y);
				ImGui::Text("Mouse Wheel: %d", inputManager->GetMouseWheelDelta());
				ImGui::Text(
					"Mouse Buttons: L=%s R=%s M=%s",
					inputManager->IsMousePressed(MouseButton::Left) ? "ON" : "OFF",
					inputManager->IsMousePressed(MouseButton::Right) ? "ON" : "OFF",
					inputManager->IsMousePressed(MouseButton::Middle) ? "ON" : "OFF");
				ImGui::Separator();
				ImGui::Text(
					"XInput Gamepad: %s",
					inputManager->IsGamepadConnected() ? "Connected" : "Not Connected");
				if (inputManager->IsGamepadConnected()) {
					ImGui::Text("Left Stick: %.2f, %.2f", leftStick.x, leftStick.y);
					ImGui::Text("Right Stick: %.2f, %.2f", rightStick.x, rightStick.y);
					ImGui::Text(
						"Triggers: L=%.2f R=%.2f",
						inputManager->GetLeftTrigger(),
						inputManager->GetRightTrigger());
				}
			}
			ImGui::End();

			// XAudio2で再生中のBGMをImGuiから操作する。
			ImGui::Begin("Sound Control");
			if (audioManager != nullptr && bgmHandle >= 0) {
				if (ImGui::SliderFloat("BGM Volume", &bgmVolume, 0.0f, 1.0f)) {
					audioManager->SetVolume(bgmHandle, bgmVolume);
				}

				if (ImGui::Button("Play from Start")) {
					audioManager->Play(bgmHandle, true);
				}
				ImGui::SameLine();
				if (ImGui::Button("Pause")) {
					audioManager->Pause(bgmHandle);
				}
				ImGui::SameLine();
				if (ImGui::Button("Resume")) {
					audioManager->Resume(bgmHandle);
				}
				ImGui::SameLine();
				if (ImGui::Button("Stop")) {
					audioManager->Stop(bgmHandle);
				}

				const char* soundState = audioManager->IsPaused(bgmHandle)
					? "Paused"
					: (audioManager->IsPlaying(bgmHandle) ? "Playing" : "Stopped");
				ImGui::Text("State: %s", soundState);
			} else {
				ImGui::TextUnformatted("BGM could not be loaded.");
			}
			ImGui::End();

			// 球の操作
			ImGui::Begin("Sphere Control");
			ImGui::DragFloat3("Sphere Scale", &sphereTransform.scale.x, 0.1f, 0.01f, 100.0f);
			ImGui::SliderFloat3("Sphere Rotate", &sphereTransform.rotate.x, -3.1415f, 3.1415f);
			ImGui::DragFloat3("Sphere Translate", &sphereTransform.translate.x, 0.1f);
			ImGui::ColorEdit4("Sphere Color", &sphereColor.x);
			ImGui::SeparatorText("Sphere UV Transform");
			ImGui::DragFloat2("Sphere UV Scale", &sphereUVTransform.scale.x, 0.01f, 0.01f, 10.0f);
			ImGui::SliderFloat("Sphere UV Rotate", &sphereUVTransform.rotate, -3.1415f, 3.1415f);
			ImGui::DragFloat2("Sphere UV Translate", &sphereUVTransform.translate.x, 0.01f, -10.0f, 10.0f);

			ImGui::Combo("Sphere Texture", &selectedSphereTexture, textureNames, _countof(textureNames));
			ImGui::End();

			ImGui::Begin("OBJ Model Control");
			if (cubeModel != nullptr) {
				ImGui::Text("Vertices: %u", cubeModel->GetVertexCount());
				ImGui::Text("Indices: %u", cubeModel->GetIndexCount());
			}
			ImGui::DragFloat3("Model Scale", &modelTransform.scale.x, 0.1f, 0.01f, 100.0f);
			ImGui::SliderFloat3("Model Rotate", &modelTransform.rotate.x, -3.1415f, 3.1415f);
			ImGui::DragFloat3("Model Translate", &modelTransform.translate.x, 0.1f);
			ImGui::ColorEdit4("Model Color", &modelColor.x);
			ImGui::Combo("Model Texture", &selectedModelTexture, textureNames, _countof(textureNames));
			ImGui::DragFloat2("Model UV Scale", &modelUVTransform.scale.x, 0.01f, 0.01f, 10.0f);
			ImGui::SliderFloat("Model UV Rotate", &modelUVTransform.rotate, -3.1415f, 3.1415f);
			ImGui::DragFloat2("Model UV Translate", &modelUVTransform.translate.x, 0.01f, -10.0f, 10.0f);
			ImGui::End();


			ImGui::Begin("Sprite Control");

			ImGui::DragFloat2("Sprite Scale (Size)", &spriteTransform.scale.x, 1.0f, 1.0f, 1280.0f);
			ImGui::SliderFloat("Sprite Rotate Z", &spriteTransform.rotate.z, -3.1415f, 3.1415f);
			ImGui::DragFloat2("Sprite Position", &spriteTransform.translate.x, 1.0f, 0.0f, 1280.0f);

			ImGui::ColorEdit4("Sprite Color", &spriteColor.x);
			ImGui::SeparatorText("Sprite UV Transform");
			ImGui::DragFloat2("Sprite UV Scale", &spriteUVTransform.scale.x, 0.01f, 0.01f, 10.0f);
			ImGui::SliderFloat("Sprite UV Rotate", &spriteUVTransform.rotate, -3.1415f, 3.1415f);
			ImGui::DragFloat2("Sprite UV Translate", &spriteUVTransform.translate.x, 0.01f, -10.0f, 10.0f);

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
			ImGui::DragFloat2("UV Scale 1", &triangleUVTransform[0].scale.x, 0.01f, 0.01f, 10.0f);
			ImGui::SliderFloat("UV Rotate 1", &triangleUVTransform[0].rotate, -3.1415f, 3.1415f);
			ImGui::DragFloat2("UV Translate 1", &triangleUVTransform[0].translate.x, 0.01f, -10.0f, 10.0f);

			// 2つ目の三角形用のUI
			ImGui::Separator();
			ImGui::Text("Triangle 2 Transform");
			ImGui::Combo("Texture 2", &selectedTriangleTexture[1], textureNames, _countof(textureNames));
			ImGui::DragFloat3("Scale 2", &transform[1].scale.x, 0.1f, 0.01f, 100.0f);
			ImGui::SliderFloat3("Rotate 2", &transform[1].rotate.x, -3.1415f, 3.1415f);
			ImGui::DragFloat3("Translate 2", &transform[1].translate.x, 0.1f);
			ImGui::DragFloat2("UV Scale 2", &triangleUVTransform[1].scale.x, 0.01f, 0.01f, 10.0f);
			ImGui::SliderFloat("UV Rotate 2", &triangleUVTransform[1].rotate, -3.1415f, 3.1415f);
			ImGui::DragFloat2("UV Translate 2", &triangleUVTransform[1].translate.x, 0.01f, -10.0f, 10.0f);

			ImGui::End();
#endif

			// 2. 3Dを先に描き、深度を使わないSpriteを最後に重ねる。
			primitiveDrawer->DrawTriangle(
				vertices, transform[0], color, textureHandles[selectedTriangleTexture[0]],
				triangleUVTransform[0]);

			primitiveDrawer->DrawTriangle(
				vertices, transform[1], color, textureHandles[selectedTriangleTexture[1]],
				triangleUVTransform[1]);

			primitiveDrawer->DrawSphere(
				sphereTransform, sphereColor, textureHandles[selectedSphereTexture],
				sphereUVTransform);

			if (cubeModel != nullptr) {
				cubeModel->Draw(
					modelTransform,
					modelColor,
					textureHandles[selectedModelTexture],
					modelUVTransform);
			}

			sprite->Draw(
				spriteTransform, spriteColor, textureHandles[selectedSpriteTexture],
				spriteUVTransform);


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
