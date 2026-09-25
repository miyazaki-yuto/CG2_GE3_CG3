#include <Windows.h>
#include "Engine.h"
#include "AssetManager.h"
#include "AudioManager.h"
#include "CameraComponent.h"
#include "DebugCamera.h"
#include "Editor.h"
#include "InputManager.h"
#include "LightingManager.h"
#include "LightComponent.h"
#include "PrimitiveDrawer.h"
#include "PrimitiveRendererComponent.h"
#include "PrefabManager.h"
#include "PrefabInstanceComponent.h"
#include "PlayModeManager.h"
#include "Sprite.h"
#include "SpriteRendererComponent.h"
#include "Model.h"
#include "ModelRendererComponent.h"
#include "Scene.h"
#include "SceneSerializer.h"
#include "CommonTypes.h"
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif
#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <thread>
#include <utility>

#ifdef _DEBUG
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif

namespace {

constexpr char kStartupScenePath[] =
	"Resources/Scenes/MainScene.json";

struct SampleGameObjectIds {
	GameObject::Id triangles[2]{};
	GameObject::Id model = 0;
	GameObject::Id skySphere = 0;
};

bool LoadStartupScene(
	Scene& scene,
	AssetManager& assetManager,
	Graphics& graphics,
	InputManager* inputManager,
	int defaultTextureHandle,
	const std::string& defaultTextureGuid,
	std::string& message) {
	SceneSerializer loader;
	loader.Initialize(
		&assetManager,
		&graphics,
		inputManager,
		defaultTextureHandle,
		defaultTextureGuid);
	return loader.Load(scene, kStartupScenePath, message);
}

void ResolveSampleGameObjectIds(
	const Scene& scene,
	SampleGameObjectIds& ids) {
	const auto findId = [&scene](const char* name) {
		if (const GameObject* object = scene.FindGameObject(name)) {
			return object->GetId();
		}
		return GameObject::Id{ 0 };
	};

	ids.triangles[0] = findId("Triangle 1");
	ids.triangles[1] = findId("Triangle 2");
	ids.model = findId("Cube Model");
	ids.skySphere = findId("Sky Sphere");
}

#ifdef _DEBUG
void RunPrefabSelfTest(
	Scene& scene,
	PrefabManager& prefabManager,
	AssetManager& assetManager) {
	char enabled[2]{};
	if (GetEnvironmentVariableA(
		"CG2_PREFAB_SELF_TEST", enabled, static_cast<DWORD>(std::size(enabled))) == 0) {
		return;
	}

	const std::filesystem::path prefabPath =
		"Resources/Prefabs/__PrefabSelfTest.prefab";
	std::filesystem::path metaPath = prefabPath;
	metaPath += ".meta";
	std::error_code errorCode;
	std::filesystem::remove(prefabPath, errorCode);
	errorCode.clear();
	std::filesystem::remove(metaPath, errorCode);
	assetManager.RefreshAssets();

	GameObject& source = scene.CreateGameObject("Prefab Self Test");
	GameObject& firstChild = scene.CreateGameObject("First Child");
	assert(firstChild.SetParent(&source));
	std::string message;
	const std::string prefabGuid = prefabManager.SavePrefab(
		source, prefabPath.generic_string(), message);
	assert(!prefabGuid.empty());

	GameObject* firstInstance = prefabManager.Instantiate(
		scene, prefabGuid, nullptr, message);
	GameObject* secondInstance = prefabManager.Instantiate(
		scene, prefabGuid, nullptr, message);
	assert(firstInstance != nullptr && secondInstance != nullptr);
	firstInstance->GetTransform().GetLocalTransform().translate.x = 10.0f;
	secondInstance->GetTransform().GetLocalTransform().translate.x = 20.0f;

	const GameObject::Id instanceIds[] = {
		source.GetId(), firstInstance->GetId(), secondInstance->GetId()
	};
	GameObject& addedChild = scene.CreateGameObject("Added Child");
	assert(addedChild.SetParent(&source));
	firstChild.SetName("Updated Child");
	assert(prefabManager.ApplyPrefab(scene, source, message));

	const float expectedPositions[] = { 0.0f, 10.0f, 20.0f };
	GameObject::Id previousFirstChildIds[std::size(instanceIds)]{};
	for (size_t index = 0; index < std::size(instanceIds); ++index) {
		GameObject* instance = scene.FindGameObject(instanceIds[index]);
		assert(instance != nullptr);
		assert(instance->GetChildren().size() == 2);
		assert(instance->GetChildren()[0]->GetName() == "Updated Child");
		previousFirstChildIds[index] = instance->GetChildren()[0]->GetId();
		assert(instance->GetTransform().GetLocalTransform().translate.x ==
			expectedPositions[index]);
		const PrefabInstanceComponent* marker =
			instance->GetComponent<PrefabInstanceComponent>();
		assert(marker != nullptr && marker->GetPrefabGuid() == prefabGuid);
	}

	// Editor外で.prefabが書き換わった場合も、更新時刻の監視で全実体へ反映する。
	{
		std::ofstream externalEdit(prefabPath, std::ios::app);
		externalEdit << '\n';
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(600));
	prefabManager.Update(scene);
	for (size_t index = 0; index < std::size(instanceIds); ++index) {
		GameObject* instance = scene.FindGameObject(instanceIds[index]);
		assert(instance != nullptr && instance->GetChildren().size() == 2);
		assert(instance->GetChildren()[0]->GetId() !=
			previousFirstChildIds[index]);
	}

	for (const GameObject::Id id : instanceIds) {
		if (GameObject* instance = scene.FindGameObject(id)) {
			scene.DestroyGameObjectImmediate(*instance);
		}
	}
	std::filesystem::remove(prefabPath, errorCode);
	errorCode.clear();
	std::filesystem::remove(metaPath, errorCode);
	assetManager.RefreshAssets();
	OutputDebugStringA("Prefab self-test succeeded.\n");
}

void RunPlayModeSelfTest(
	Scene& editScene,
	PlayModeManager& playModeManager,
	LightingManager& lightingManager) {
	char enabled[2]{};
	if (GetEnvironmentVariableA(
		"CG2_PLAY_MODE_SELF_TEST",
		enabled,
		static_cast<DWORD>(std::size(enabled))) == 0) {
		return;
	}

	assert(!editScene.GetGameObjects().empty());
	const size_t editObjectCount = editScene.GetGameObjects().size();
	const GameObject& originalObject = *editScene.GetGameObjects().front();
	const GameObject::Id originalId = originalObject.GetId();
	const std::string originalName = originalObject.GetName();
	const Vector3 originalPosition =
		originalObject.GetTransform().GetLocalTransform().translate;
	const uint32_t directionalLightCount =
		lightingManager.GetDirectionalLightCount();
	const uint32_t pointLightCount = lightingManager.GetPointLightCount();
	const LightingMode originalLightingMode =
		lightingManager.GetLightingMode();
	lightingManager.SetLightingMode(LightingMode::Lambert);

	std::string message;
	assert(playModeManager.StartPlay(editScene, message));
	assert(playModeManager.IsPlaying());
	Scene& playScene = playModeManager.GetActiveScene(editScene);
	assert(&playScene != &editScene);
	assert(playScene.IsRuntimeUpdateEnabled());
	// Edit側を停止してからPlay側を登録するため、ライト数は二重にならない。
	assert(lightingManager.GetDirectionalLightCount() ==
		directionalLightCount);
	assert(lightingManager.GetPointLightCount() == pointLightCount);
	assert(lightingManager.GetLightingMode() == LightingMode::Lambert);

	// 実行用Sceneだけを書き換え、編集用Sceneへ変更が漏れないことを確認する。
	GameObject* runtimeObject = playScene.FindGameObject(originalId);
	assert(runtimeObject != nullptr);
	runtimeObject->SetName("Runtime Modified Object");
	runtimeObject->GetTransform().GetLocalTransform().translate.x += 100.0f;
	playScene.CreateGameObject("Runtime Only Object");
	lightingManager.SetLightingMode(LightingMode::HalfLambert);
	playScene.Update(1.0f / 60.0f);
	assert(editScene.FindGameObject("Runtime Only Object") == nullptr);

	assert(playModeManager.StopPlay(editScene, message));
	assert(!playModeManager.IsPlaying());
	assert(!editScene.IsRuntimeUpdateEnabled());
	assert(lightingManager.GetDirectionalLightCount() ==
		directionalLightCount);
	assert(lightingManager.GetPointLightCount() == pointLightCount);
	assert(lightingManager.GetLightingMode() == LightingMode::Lambert);
	assert(editScene.GetGameObjects().size() == editObjectCount);
	const GameObject* restoredObject = editScene.FindGameObject(originalId);
	assert(restoredObject != nullptr);
	assert(restoredObject->GetName() == originalName);
	const Vector3 restoredPosition =
		restoredObject->GetTransform().GetLocalTransform().translate;
	assert(restoredPosition.x == originalPosition.x);
	assert(restoredPosition.y == originalPosition.y);
	assert(restoredPosition.z == originalPosition.z);
	assert(editScene.FindGameObject("Runtime Only Object") == nullptr);
	lightingManager.SetLightingMode(originalLightingMode);
	OutputDebugStringA("Play Mode self-test succeeded.\n");
}
#endif

} // namespace


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
		AssetManager* assetManager = engine.GetAssetManager();
		PrefabManager* prefabManager = engine.GetPrefabManager();
		assert(assetManager != nullptr);
		assert(prefabManager != nullptr);
		// Graphicsが所有する描画クラスを借りる。生成・破棄はGraphicsが担当する。
		PrimitiveDrawer* primitiveDrawer = graphics->GetPrimitiveDrawer();
		DebugCamera* debugCamera = graphics->GetDebugCamera();
		DebugCamera* editorCamera = graphics->GetEditorCamera();
		Sprite* sprite = graphics->GetSprite();
		LightingManager* lightingManager = graphics->GetLightingManager();
		AudioManager* audioManager = engine.GetAudioManager();
		InputManager* inputManager = engine.GetInputManager();

		// ゲーム内に存在するGameObjectをまとめて所有する最初のScene。
		Scene mainScene("Main Scene");

		// Graphicsが持つ描画用カメラをGameObjectへ接続する。
		// Blender風入力、Transform同期、スペキュラ用座標更新をComponentに任せる。
		GameObject& cameraGameObject =
			mainScene.CreateGameObject("Main Camera");
		cameraGameObject.AddComponent<CameraComponent>(
			debugCamera, inputManager, lightingManager);

		// Sunは位置ではなくTransformの回転で照射方向を決める。
		GameObject& sunGameObject = mainScene.CreateGameObject("Sun");
		TransformData& sunTransform =
			sunGameObject.GetTransform().GetLocalTransform();
		sunTransform.rotate = { 0.785398f, 0.0f, 0.0f };
		sunGameObject.AddComponent<LightComponent>(
			lightingManager, LightComponent::LightType::Directional);

		// Point LightはGameObjectのtranslateが、そのまま光源位置になる。
		GameObject& pointLightGameObject =
			mainScene.CreateGameObject("Point Light");
		TransformData& pointLightTransform =
			pointLightGameObject.GetTransform().GetLocalTransform();
		pointLightTransform.translate = { 0.0f, 2.0f, -3.0f };
		pointLightGameObject.AddComponent<LightComponent>(
			lightingManager, LightComponent::LightType::Point);

		// 2灯目も同じLightingManagerへ別スロットとして登録される。
		GameObject& fillLightGameObject =
			mainScene.CreateGameObject("Blue Fill Light");
		TransformData& fillLightTransform =
			fillLightGameObject.GetTransform().GetLocalTransform();
		fillLightTransform.translate = { -3.0f, 1.5f, 2.0f };
		LightComponent* initialFillLight =
			fillLightGameObject.AddComponent<LightComponent>(
				lightingManager, LightComponent::LightType::Point);
		initialFillLight->GetColor() = { 0.3f, 0.55f, 1.0f, 1.0f };
		initialFillLight->SetIntensity(1.5f);
		initialFillLight->SetRadius(8.0f);

		// BGMはゲーム側がハンドルを所有し、起動時にループ再生する。
		float bgmVolume = 0.25f;
		int bgmHandle = -1;
		if (audioManager != nullptr) {
			bgmHandle = audioManager->LoadWave("Resources/bgm.wav");
			if (bgmHandle >= 0) {
				audioManager->SetVolume(bgmHandle, bgmVolume);
			}
		}

		PrimitiveRendererComponent::TriangleVertices vertices = {
			TextureVertexData{ { -0.5f, -0.5f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } }, // 左下
			TextureVertexData{ {  0.0f,  0.5f, 0.0f, 1.0f }, { 0.5f, 0.0f }, { 0.0f, 0.0f, -1.0f } }, // 上
			TextureVertexData{ {  0.5f, -0.5f, 0.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } }  // 右下
		};

		// GUIDは各Assetの.metaに保存されている。起動コードにもパスを持たせないため、
		// Resources内でファイルを移動してもAssetManagerの走査結果から解決できる。
		const AssetGuid uvCheckerTextureGuid =
			"003276da-7c56-46b8-bda5-b47d5934d004";
		const AssetGuid monsterBallTextureGuid =
			"e4ddc1f4-e313-4d98-b05f-3b63e12cb113";
		const AssetGuid whiteTextureGuid =
			"90ee89d6-3df7-4b61-9896-c2bf03ad3fc6";
		const AssetGuid cubeModelGuid =
			"e63290df-df47-4d6a-9547-df199eb8dc8e";
		const AssetGuid skySphereModelGuid =
			"6325c2bb-97b9-460d-ab5d-42467e27e53a";
		const AssetGuid skySphereTextureGuid =
			"cfe1e1aa-3f59-4684-b181-38dc03e9f66b";
		const int uvCheckerTextureHandle =
			assetManager->LoadTexture(uvCheckerTextureGuid);
		const int monsterBallTextureHandle =
			assetManager->LoadTexture(monsterBallTextureGuid);
		const int whiteTextureHandle =
			assetManager->LoadTexture(whiteTextureGuid);
		// Prefab内部でAssetが欠けた場合に使う既定テクスチャも設定する。
		prefabManager->Initialize(
			assetManager,
			graphics,
			inputManager,
			whiteTextureHandle,
			whiteTextureGuid);
		PlayModeManager playModeManager;
		playModeManager.Initialize(
			assetManager,
			graphics,
			inputManager,
			whiteTextureHandle,
			whiteTextureGuid);

		Editor editor;
		editor.Initialize(
			assetManager,
			prefabManager,
			graphics,
			inputManager,
			whiteTextureHandle,
			whiteTextureGuid,
			cubeModelGuid);

		// 同じモデルGUIDを複数回Loadしても、同じshared_ptr<Model>が返る。
		std::shared_ptr<Model> cubeModel =
			assetManager->LoadModel(cubeModelGuid);
#ifdef _DEBUG
		// 同一GUIDの再Loadが、同じGPUリソースを返すことを確認する。
		assert(assetManager->LoadTexture(uvCheckerTextureGuid) ==
			uvCheckerTextureHandle);
		assert(assetManager->LoadModel(cubeModelGuid).get() == cubeModel.get());
#endif

		// 天球OBJは専用PSOで読み込む。MTLのmap_Kdからsky_sphere.pngのパスも取得する。
		std::shared_ptr<Model> skySphereModel =
			assetManager->LoadModel(skySphereModelGuid, true);
		const int skySphereTextureHandle =
			assetManager->LoadTexture(skySphereTextureGuid);
		if (lightingManager != nullptr && skySphereTextureHandle >= 0) {
			// 天球と同じ経緯度画像をIBLにも使うと、背景と物体の映り込みが自然につながる。
			lightingManager->SetEnvironmentTextureAsset(
				skySphereTextureHandle,
				skySphereTextureGuid);
			lightingManager->SetEnvironmentIntensity(0.25f);
			lightingManager->SetEnvironmentEnabled(true);
		}

		// 三角形も1個ずつGameObjectとしてSceneに登録する。
		// 頂点、色、UV、テクスチャは各RendererComponentが独立して保持する。
		GameObject& triangleGameObject1 =
			mainScene.CreateGameObject("Triangle 1");
		GameObject& triangleGameObject2 =
			mainScene.CreateGameObject("Triangle 2");
		SampleGameObjectIds sampleGameObjectIds{};
		sampleGameObjectIds.triangles[0] = triangleGameObject1.GetId();
		sampleGameObjectIds.triangles[1] = triangleGameObject2.GetId();
		PrimitiveRendererComponent* initialTriangleRenderers[2] = {
			triangleGameObject1.AddComponent<PrimitiveRendererComponent>(
				primitiveDrawer,
				PrimitiveRendererComponent::PrimitiveType::Triangle,
				uvCheckerTextureHandle,
				uvCheckerTextureGuid),
			triangleGameObject2.AddComponent<PrimitiveRendererComponent>(
				primitiveDrawer,
				PrimitiveRendererComponent::PrimitiveType::Triangle,
				uvCheckerTextureHandle,
				uvCheckerTextureGuid)
		};
		initialTriangleRenderers[0]->SetTriangleVertices(vertices);
		initialTriangleRenderers[1]->SetTriangleVertices(vertices);
		// 三角形ごとに独立したUV変換を持たせる。

		// スプライトの初期化
		GameObject& spriteGameObject = mainScene.CreateGameObject("Sprite");
		TransformData& spriteTransform =
			spriteGameObject.GetTransform().GetLocalTransform();
		spriteTransform = {
			{ 100.0f, 100.0f, 1.0f }, // Scale 
			{ 0.0f, 0.0f, 0.0f },     // Rotate
			{ 200.0f, 200.0f, 0.0f }  // Translate 
		};
		spriteGameObject.AddComponent<SpriteRendererComponent>(
			sprite, uvCheckerTextureHandle, uvCheckerTextureGuid);

		GameObject& sphereGameObject = mainScene.CreateGameObject("Sphere");
		TransformData& sphereTransform =
			sphereGameObject.GetTransform().GetLocalTransform();
		sphereTransform = {
			{ 1.0f, 1.0f, 1.0f }, // Scale 
			{ 0.0f, 0.0f, 0.0f }, // Rotate
			{ 2.0f, 0.0f, 0.0f }  // Translate
		};
		sphereGameObject.AddComponent<PrimitiveRendererComponent>(
			primitiveDrawer,
			PrimitiveRendererComponent::PrimitiveType::Sphere,
			monsterBallTextureHandle,
			monsterBallTextureGuid);

		GameObject& modelGameObject = mainScene.CreateGameObject("Cube Model");
		sampleGameObjectIds.model = modelGameObject.GetId();
		TransformData& modelTransform =
			modelGameObject.GetTransform().GetLocalTransform();
		modelTransform = {
			{ 0.75f, 0.75f, 0.75f },
			{ 0.0f, 0.5f, 0.0f },
			{ -1.5f, 0.0f, 0.0f }
		};
		modelGameObject.AddComponent<ModelRendererComponent>(
			std::move(cubeModel),
			uvCheckerTextureHandle,
			cubeModelGuid,
			uvCheckerTextureGuid);

		// 青いFill LightをCubeの子にして、Cubeの移動・回転・拡縮へ追従させる。
		// SetParentはローカル座標を維持するため、現在値がCube基準の相対位置になる。
		const bool fillLightParented =
			fillLightGameObject.SetParent(&modelGameObject);
		assert(fillLightParented);
		(void)fillLightParented;
#ifdef _DEBUG
		// Cubeの親を自分の子へ変更すると循環するため、必ず拒否される。
		const bool hierarchyCycleRejected =
			!modelGameObject.SetParent(&fillLightGameObject);
		assert(hierarchyCycleRejected);
#endif

		// OBJモデルが周囲へ返す反射光の近似パラメーター。
		// 色はmodelColorを使うため、モデルを赤くすると近くの物体にも赤い光が回り込む。
		constexpr bool kEnableModelBounceLight = true;
		constexpr float kModelBounceIntensity = 0.75f;
		constexpr float kModelBounceRadius = 6.0f;
		constexpr float kModelBounceDecay = 2.0f;
		const Vector3 kModelBounceOffset = { 0.0f, 0.0f, 0.0f };

		// 元のOBJは半径約1000なので、遠クリップ面(1000)の内側に収まるよう半径500相当にする。
		// translateは毎フレーム、デバッグカメラの座標に更新する。
		GameObject& skySphereGameObject =
			mainScene.CreateGameObject("Sky Sphere");
		sampleGameObjectIds.skySphere = skySphereGameObject.GetId();
		TransformData& skySphereTransform =
			skySphereGameObject.GetTransform().GetLocalTransform();
		skySphereTransform = {
			{ 0.5f, 0.5f, 0.5f },
			{ 0.0f, 0.0f, 0.0f },
			{ 0.0f, 0.0f, 0.0f }
		};
		const int skySphereFallbackTextureHandle =
			skySphereTextureHandle >= 0
			? skySphereTextureHandle
			: whiteTextureHandle;
		const AssetGuid& skySphereFallbackTextureGuid =
			skySphereTextureHandle >= 0
			? skySphereTextureGuid
			: whiteTextureGuid;
		ModelRendererComponent* initialSkySphereRenderer =
			skySphereGameObject.AddComponent<ModelRendererComponent>(
				std::move(skySphereModel),
				skySphereFallbackTextureHandle,
				skySphereModelGuid,
				skySphereFallbackTextureGuid);
		initialSkySphereRenderer->SetLightingEnabled(false);
		initialSkySphereRenderer->SetRenderOrder(
			RendererComponent::kSkyRenderOrder);

#ifdef _DEBUG
		RunPrefabSelfTest(mainScene, *prefabManager, *assetManager);

		// 新規Sceneがパスを保存せず、GUIDだけを保存することを起動時に検証する。
		SceneSerializer assetSceneVerifier;
		assetSceneVerifier.Initialize(
			assetManager,
			graphics,
			inputManager,
			whiteTextureHandle,
			whiteTextureGuid);
		std::string serializedScene;
		std::string serializeMessage;
		assert(assetSceneVerifier.SerializeToString(
			mainScene, serializedScene, serializeMessage));
		assert(serializedScene.find("\"modelGuid\"") != std::string::npos);
		assert(serializedScene.find("\"textureGuid\"") != std::string::npos);
		assert(serializedScene.find("\"modelPath\"") == std::string::npos);
		assert(serializedScene.find("\"texturePath\"") == std::string::npos);
		assert(lightingManager != nullptr);
		RunPlayModeSelfTest(
			mainScene, playModeManager, *lightingManager);
#endif

		std::string playModeMessage =
			"Edit Mode: press Play to clone and run the Scene.";
		// Debug EditorとRelease Gameの両方で、Editorから保存したSceneを
		// 起動Sceneとして読み込む。DebugはEdit Modeのまま開く。
		std::string startupSceneMessage;
		const bool startupSceneLoaded = LoadStartupScene(
			mainScene,
			*assetManager,
			*graphics,
			inputManager,
			whiteTextureHandle,
			whiteTextureGuid,
			startupSceneMessage);
		if (!startupSceneLoaded) {
#ifdef USE_IMGUI
			// Editorでは新規Sceneを修正・保存できるよう、組み込みSceneを維持する。
			const std::string warningMessage =
				"Debug startup Scene load failed.\n\n" +
				startupSceneMessage +
				"\n\nThe built-in Scene will remain open.";
			OutputDebugStringA((warningMessage + "\n").c_str());
			MessageBoxA(
				nullptr,
				warningMessage.c_str(),
				"CG2 Scene Load Warning",
				MB_OK | MB_ICONWARNING);
			playModeMessage = startupSceneMessage;
#else
			// Releaseでは必要なSceneがない状態で実行を続けない。
			const std::string errorMessage =
				"Release Scene load failed.\n\n" + startupSceneMessage;
			OutputDebugStringA((errorMessage + "\n").c_str());
			MessageBoxA(
				nullptr,
				errorMessage.c_str(),
				"CG2 Scene Load Error",
				MB_OK | MB_ICONERROR);
			return -1;
#endif
		}

		if (startupSceneLoaded) {
			// Sample game logicはIDでObjectを検索するため、保存JSONのIDへ接続し直す。
			// Objectが削除またはRenameされている場合は0にして、その処理だけを安全にSkipする。
			ResolveSampleGameObjectIds(mainScene, sampleGameObjectIds);
			playModeMessage = startupSceneMessage;
#ifdef USE_IMGUI
			// 起動前の組み込みScene用Selection／Undo履歴を残さない。
			editor.ResetSceneContext(mainScene);
#endif
		}

#ifndef USE_IMGUI
		// JSONから復元したEdit Sceneを複製し、起動時からゲーム実行状態にする。
		if (!playModeManager.StartPlay(mainScene, playModeMessage)) {
			const std::string errorMessage =
				"Failed to start Play Mode.\n\n" + playModeMessage;
			OutputDebugStringA((errorMessage + "\n").c_str());
			MessageBoxA(
				nullptr,
				errorMessage.c_str(),
				"CG2 Play Mode Error",
				MB_OK | MB_ICONERROR);
			return -1;
		}
		if (audioManager != nullptr && bgmHandle >= 0) {
			audioManager->Play(bgmHandle, true);
		}
#endif

		OutputDebugStringA("Loop Start\n");

		while (engine.ProcessMessage())
		{
			//============//
			// ゲーム処理 // 
			//============//

			// Engineが実測した秒数。速度に掛けることで、60FPSでも144FPSでも同じ速さになる。
			const float deltaTime = engine.GetDeltaTime();
			Scene& activeScene = playModeManager.GetActiveScene(mainScene);
			const bool isPlaying = playModeManager.IsPlaying();

			// ゲーム固有処理もPlay中だけ実行する。
			// Edit中はHierarchyやInspectorでSceneを編集しても、座標やゲーム状態が進まない。
			if (isPlaying) {
			constexpr float kTriangleRotateSpeed = 0.6f; // 1秒あたりの回転量（ラジアン）
			if (GameObject* triangle =
				activeScene.FindGameObject(sampleGameObjectIds.triangles[0]);
				triangle != nullptr && triangle->GetName() == "Triangle 1") {
				triangle->GetTransform().GetLocalTransform().rotate.y -=
					kTriangleRotateSpeed * deltaTime;
			}
			if (GameObject* triangle =
				activeScene.FindGameObject(sampleGameObjectIds.triangles[1]);
				triangle != nullptr && triangle->GetName() == "Triangle 2") {
				triangle->GetTransform().GetLocalTransform().rotate.x -=
					kTriangleRotateSpeed * deltaTime;
			}

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
				if (GameObject* modelObject =
					activeScene.FindGameObject(sampleGameObjectIds.model);
					modelObject != nullptr &&
					modelObject->GetName() == "Cube Model") {
					TransformData& currentModelTransform =
						modelObject->GetTransform().GetLocalTransform();
					currentModelTransform.translate.x +=
						moveX * kModelMoveSpeed * deltaTime;
					currentModelTransform.translate.y +=
						moveY * kModelMoveSpeed * deltaTime;
				}

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
			allowDebugCameraMouseControl = editor.IsViewportHovered();
#endif
			if (!isPlaying) {
				// Edit Mode owns a separate Scene View camera. Copy only its
				// state to the render camera; never modify CameraComponent.
				if (editorCamera != nullptr && inputManager != nullptr) {
					editorCamera->Update(
						*inputManager, allowDebugCameraMouseControl);
				}
				if (editorCamera != nullptr && debugCamera != nullptr) {
					debugCamera->SetState(
						editorCamera->GetTarget(),
						editorCamera->GetYaw(),
						editorCamera->GetPitch(),
						editorCamera->GetDistance(),
						editorCamera->IsOrthographic(),
						editorCamera->GetFovY(),
						editorCamera->GetNearClip(),
						editorCamera->GetFarClip());
					if (lightingManager != nullptr) {
						lightingManager->SetCameraPosition(
							editorCamera->GetPosition());
					}
				}
			} else {
				for (const std::unique_ptr<GameObject>& gameObject :
					activeScene.GetGameObjects()) {
					if (CameraComponent* cameraComponent =
						gameObject->GetComponent<CameraComponent>()) {
						cameraComponent->SetMouseControlEnabled(
							allowDebugCameraMouseControl);
						break;
					}
				}
			}
			// Componentと将来のVisual ScriptノードはPlay Sceneだけを更新する。
			if (isPlaying) {
				activeScene.Update(deltaTime);
			}
			// .prefabが外部で変更された場合、同じGUIDの全インスタンスを更新する。
			prefabManager->Update(activeScene);

#ifdef USE_IMGUI
			editor.Draw(activeScene, isPlaying, playModeMessage);

			// FPSはImGuiが直近のフレーム時間から平滑化してくれるため、
			// 一瞬の処理落ちで数字が激しく跳ねず、実際の操作感と比べやすい。
			ImGui::Begin("Performance");
			const float currentFps = ImGui::GetIO().Framerate;
			const float frameTimeMilliseconds =
				currentFps > 0.0f ? 1000.0f / currentFps : 0.0f;
			const ImVec4 fpsColor = currentFps >= 55.0f
				? ImVec4(0.35f, 0.9f, 0.45f, 1.0f)
				: (currentFps >= 30.0f
					? ImVec4(1.0f, 0.75f, 0.25f, 1.0f)
					: ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
			ImGui::TextColored(fpsColor, "FPS: %.1f", currentFps);
			ImGui::Text("Frame Time: %.2f ms", frameTimeMilliseconds);
			ImGui::End();

			// BlenderのLightプロパティに相当する、シーン共通ライトの編集画面。
			ImGui::Begin("Lighting");
			if (lightingManager != nullptr) {
				ImGui::Text(
					"Registered: Directional %u/%u, Point %u/%u",
					lightingManager->GetDirectionalLightCount(),
					kMaxDirectionalLights,
					lightingManager->GetPointLightCount(),
					kMaxPointLights);
				ImGui::TextWrapped(
					"Realtime Shadow: first enabled Directional Light "
					"(4 cascades) and first enabled Point Light "
					"(6-face cube).");

				ImGui::SeparatorText("Lighting Model");
				const char* lightingModes[] = {
					"Lambert",
					"Half-Lambert",
					"Current (Half-Lambert + Specular)",
					"PBR (Metallic / Roughness)"
				};
				int lightingMode = static_cast<int>(
					lightingManager->GetLightingMode());
				if (ImGui::Combo(
						"Mode", &lightingMode, lightingModes,
						static_cast<int>(std::size(lightingModes)))) {
					lightingManager->SetLightingMode(
						static_cast<LightingMode>(lightingMode));
				}
				switch (lightingManager->GetLightingMode()) {
				case LightingMode::Lambert:
					ImGui::TextWrapped(
						"Standard diffuse lighting. Back faces become dark.");
					break;
				case LightingMode::HalfLambert:
					ImGui::TextWrapped(
						"Soft diffuse lighting without specular highlights.");
					break;
				case LightingMode::Current:
					ImGui::TextWrapped(
						"Existing lighting: Half-Lambert diffuse, "
						"Blinn-Phong specular, and Bounce Light.");
					break;
				case LightingMode::PBR:
				default:
					ImGui::TextWrapped(
						"Cook-Torrance PBR: GGX, Smith geometry, "
						"Schlick Fresnel, Metallic and Roughness.");
					break;
				}

				// 個々のライトはHierarchyで選択し、Inspectorから編集する。
				// Specularは従来方式のCurrentを選んだ場合だけ使用する。
				ImGui::SeparatorText("Specular");
				ImGui::BeginDisabled(
					lightingManager->GetLightingMode() != LightingMode::Current);
				float specularStrength = lightingManager->GetSpecularStrength();
				if (ImGui::DragFloat(
					"Specular Strength", &specularStrength, 0.01f, 0.0f, 10.0f)) {
					lightingManager->SetSpecularStrength(specularStrength);
				}

				float specularShininess = lightingManager->GetSpecularShininess();
				if (ImGui::DragFloat(
					"Specular Shininess", &specularShininess, 1.0f, 1.0f, 256.0f)) {
					lightingManager->SetSpecularShininess(specularShininess);
				}
				ImGui::TextUnformatted(
					"Strength: brightness / Shininess: highlight sharpness");
				ImGui::EndDisabled();

				ImGui::SeparatorText("Environment / IBL");
				bool environmentEnabled =
					lightingManager->IsEnvironmentEnabled();
				if (ImGui::Checkbox(
						"Use Environment Light", &environmentEnabled)) {
					lightingManager->SetEnvironmentEnabled(environmentEnabled);
				}
				ImGui::BeginDisabled(!environmentEnabled);
				float environmentIntensity =
					lightingManager->GetEnvironmentIntensity();
				if (ImGui::DragFloat(
						"IBL Intensity",
						&environmentIntensity,
						0.01f,
						0.0f,
						4.0f)) {
					lightingManager->SetEnvironmentIntensity(
						environmentIntensity);
				}

				constexpr float kRadiansToDegrees =
					57.29577951308232f;
				constexpr float kDegreesToRadians =
					0.017453292519943295f;
				float environmentRotationDegrees =
					lightingManager->GetEnvironmentRotation() *
					kRadiansToDegrees;
				if (ImGui::DragFloat(
						"Environment Rotation",
						&environmentRotationDegrees,
						0.5f,
						-360.0f,
						360.0f,
						"%.1f deg")) {
					lightingManager->SetEnvironmentRotation(
						environmentRotationDegrees * kDegreesToRadians);
				}
				ImGui::EndDisabled();
				ImGui::TextWrapped(
					"The Sky Sphere texture supplies soft ambient light "
					"and reflections.");

				ImGui::SeparatorText("HDR / Tone Mapping");
				const char* toneMappingModes[] = {
					"None (Clamp)",
					"Reinhard",
					"ACES Filmic"
				};
				int toneMappingMode = static_cast<int>(
					graphics->GetToneMappingMode());
				if (ImGui::Combo(
						"Tone Mapping",
						&toneMappingMode,
						toneMappingModes,
						static_cast<int>(std::size(toneMappingModes)))) {
					graphics->SetToneMappingMode(
						static_cast<ToneMappingMode>(toneMappingMode));
				}
				float exposure = graphics->GetExposure();
				if (ImGui::SliderFloat(
						"Exposure",
						&exposure,
						-5.0f,
						5.0f,
						"%+.2f EV")) {
					graphics->SetExposure(exposure);
				}
				ImGui::TextWrapped(
					"The Scene is rendered to RGBA16F before this pass.");
				ImGui::SeparatorText("Post Processing");
				const char* aaModes[] = {
					"None", "FXAA", "TAA", "MAA"
				};
				int aaMode = static_cast<int>(
					graphics->GetAntiAliasingMode());
				if (ImGui::Combo(
						"Anti-Aliasing", &aaMode, aaModes,
						static_cast<int>(std::size(aaModes)))) {
					graphics->SetAntiAliasingMode(
						static_cast<AntiAliasingMode>(aaMode));
				}
				bool ssaoEnabled = graphics->IsSsaoEnabled();
				if (ImGui::Checkbox("SSAO", &ssaoEnabled)) {
					graphics->SetSsaoEnabled(ssaoEnabled);
				}
				float ssaoStrength = graphics->GetSsaoStrength();
				if (ImGui::SliderFloat(
						"SSAO Strength", &ssaoStrength, 0.0f, 2.0f)) {
					graphics->SetSsaoStrength(ssaoStrength);
				}
				bool bloomEnabled = graphics->IsBloomEnabled();
				if (ImGui::Checkbox("Bloom", &bloomEnabled)) {
					graphics->SetBloomEnabled(bloomEnabled);
				}
				float bloomIntensity = graphics->GetBloomIntensity();
				if (ImGui::SliderFloat(
						"Bloom Intensity", &bloomIntensity, 0.0f, 2.0f)) {
					graphics->SetBloomIntensity(bloomIntensity);
				}
				float bloomThreshold = graphics->GetBloomThreshold();
				if (ImGui::SliderFloat(
						"Bloom Threshold", &bloomThreshold, 0.0f, 5.0f)) {
					graphics->SetBloomThreshold(bloomThreshold);
				}
			}
			ImGui::End();

			// XAudio2で再生中のBGMをImGuiから操作する。
			ImGui::Begin("Sound Control");
			if (audioManager != nullptr && bgmHandle >= 0) {
				// BGMもゲーム実行状態の一部なので、Edit中は操作を無効にする。
				ImGui::BeginDisabled(!isPlaying);
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
				ImGui::EndDisabled();

				const char* soundState = audioManager->IsPaused(bgmHandle)
					? "Paused"
					: (audioManager->IsPlaying(bgmHandle) ? "Playing" : "Stopped");
				ImGui::Text("State: %s", soundState);
			} else {
				ImGui::TextUnformatted("BGM could not be loaded.");
			}
			ImGui::End();

#endif

			// 反射元モデルの現在座標と色から、このフレームのBounceLightを生成する。
			// 同じ方法でGenerateBounceLightを複数回呼べば、別モデルも反射光源にできる。
			GameObject* currentModelObject =
				activeScene.FindGameObject(sampleGameObjectIds.model);
			ModelRendererComponent* currentModelRenderer =
				currentModelObject != nullptr &&
				currentModelObject->GetName() == "Cube Model"
				? currentModelObject->GetComponent<ModelRendererComponent>()
				: nullptr;
			if (isPlaying && lightingManager != nullptr && currentModelObject != nullptr &&
				currentModelRenderer != nullptr &&
				currentModelRenderer->GetModel() != nullptr && kEnableModelBounceLight) {
				const Vector4& modelColor = currentModelRenderer->GetColor();
				const Vector3 modelWorldPosition =
					currentModelObject->GetTransform().GetWorldPosition();
				const Vector3 bouncePosition = {
					modelWorldPosition.x + kModelBounceOffset.x,
					modelWorldPosition.y + kModelBounceOffset.y,
					modelWorldPosition.z + kModelBounceOffset.z
				};
				const Color4 bounceColor = {
					modelColor.x, modelColor.y, modelColor.z, modelColor.w
				};
				lightingManager->GenerateBounceLight(
					bouncePosition,
					bounceColor,
					kModelBounceIntensity,
					kModelBounceRadius,
					kModelBounceDecay);
			}

			// 天球の中心をカメラへ追従させる。実際の描画はModelRendererComponentが行う。
			if (isPlaying && debugCamera != nullptr) {
				if (GameObject* skySphereObject =
					activeScene.FindGameObject(sampleGameObjectIds.skySphere);
					skySphereObject != nullptr &&
					skySphereObject->GetName() == "Sky Sphere") {
					skySphereObject->GetTransform().GetLocalTransform().translate =
						debugCamera->GetPosition();
				}
			}

			// DebugではScene/Game View用Textureへ描画し、ImGui::Imageで表示する。
            for (uint32_t cascadeIndex = 0;
                cascadeIndex < 4;
                ++cascadeIndex) {
                if (!graphics->BeginDirectionalShadowDraw(cascadeIndex)) {
                    break;
                }
                activeScene.RenderShadow(
                    graphics->GetDirectionalShadowViewProjection(cascadeIndex));
                graphics->EndDirectionalShadowDraw(cascadeIndex);
            }
            for (uint32_t cubeFaceIndex = 0;
                cubeFaceIndex < 6;
                ++cubeFaceIndex) {
                if (!graphics->BeginPointShadowDraw(cubeFaceIndex)) {
                    break;
                }
                activeScene.RenderShadow(
                    graphics->GetPointShadowViewProjection(cubeFaceIndex));
                graphics->EndPointShadowDraw(cubeFaceIndex);
            }

#ifdef USE_IMGUI
            if (editor.IsViewportVisible()) {
				graphics->BeginEditorViewportDraw(
					editor.GetViewportWidth(), editor.GetViewportHeight());
				activeScene.Render();
				graphics->EndEditorViewportDraw();
			}
#else
			graphics->BeginEditorViewportDraw(
				graphics->GetEditorViewportTextureWidth(),
				graphics->GetEditorViewportTextureHeight());
			activeScene.Render();
			graphics->EndEditorViewportDraw();
#endif


			// 3. コマンドをGPUへ実行させ、描画済みバックバッファを画面へ表示する。
			graphics->EndDraw();

#ifdef USE_IMGUI
			// 現在のSceneをこのフレームで使い終えてから切り替える。
			// StopでPlay Sceneを破棄しても、上のactiveScene参照が残っていないため安全。
			const Editor::PlayModeRequest playModeRequest =
				editor.ConsumePlayModeRequest();
			if (playModeRequest == Editor::PlayModeRequest::Start) {
				if (playModeManager.StartPlay(mainScene, playModeMessage)) {
					editor.ResetSceneContext(
						playModeManager.GetActiveScene(mainScene));
					if (audioManager != nullptr && bgmHandle >= 0) {
						audioManager->Play(bgmHandle, true);
					}
				}
			} else if (playModeRequest == Editor::PlayModeRequest::Stop) {
				if (playModeManager.StopPlay(mainScene, playModeMessage)) {
					editor.ResetSceneContext(mainScene);
					if (audioManager != nullptr && bgmHandle >= 0) {
						audioManager->Stop(bgmHandle);
					}
				}
			}
#endif

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
