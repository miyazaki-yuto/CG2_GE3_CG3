# CG2 Engine 機能実装状況

最終更新: 2026-07-27

このドキュメントは、現在のCG2 Engineで「何ができるのか」と「まだできないこと」を機能別にまとめたものです。
クラス名だけでなく、Editor上での使い方、保存される情報、簡易実装、現在の制限も記載しています。

### 実装状態の読み方

| 状態 | 意味 |
| --- | --- |
| ✅ 実装済み | 現在のEngine／Editorで利用できる機能 |
| ⚠️ 簡易実装 | 利用できるが、本格的なゲームエンジンと比べて制限がある機能 |
| ❌ 未実装 | 現在のコードにはまだ存在しない機能 |

### 機能別サマリー

| 分類 | 状態 | 現在の内容 |
| --- | --- | --- |
| DirectX 12描画基盤 | ✅ | SwapChain、Depth、Fence、動的／静的GPU Buffer |
| 3D描画 | ✅ | OBJ／MTL、Material Slot別Texture／Normal Map／PBR値／UV Transform、Triangle、Sphere、Sky Sphere |
| 2D描画 | ✅ | Texture付きSprite、Color、UV Transform |
| GameObject／Component／Scene | ✅ | ID、親子階層、Lifecycle、JSON保存／読み込み |
| Lighting | ✅ | Lambert／Half-Lambert／Current／PBR、Directional／Point、4段CSM、Point Shadow Cube、環境光／IBL |
| HDR／Tone Mapping | ✅ | RGBA16F Scene描画、Exposure、None／Reinhard／ACES |
| Post Process | ⚠️ | FXAA／軽量TAA／MAA、DepthベースSSAO、HDR Bloom |
| Bounce Light | ⚠️ | Point Lightによる間接光の簡易近似 |
| Input／Audio | ✅ | DirectInput、XInput、XAudio2 WAV再生 |
| Asset管理 | ✅ | GUID、`.meta`、Model／Texture／Prefab Cache |
| Prefab | ⚠️ | 保存、生成、全体更新に対応。Nested／Property Overrideなし |
| ImGui Editor | ✅ | Docking、Floating、Hierarchy、Inspector、Undo／Redo、FPS／Frame Time表示 |
| Scene View／Game View | ⚠️ | Mode別表示に対応。同時描画は未対応 |
| Scene View Gizmo | ⚠️ | Light／Cameraの表示に対応。Transform操作は未対応 |
| Edit Mode／Play Mode | ✅ | Scene複製、実行、Stop時の変更破棄 |
| Scratch風Visual Scripting | ❌ | Node Editorと実行Runtimeの両方が未実装 |
| Physics／Animation | ❌ | Collider、RigidBody、Bone、Skinned Mesh未実装 |

## 1. 現在のエンジン概要

現在のCG2 Engineは、DirectX 12を土台にしたWindows向けの2D・3Dゲームエンジンです。

次のような仕組みが実装されています。

- DirectX 12による3Dモデル、三角形、球、2Dスプライトの描画
- OBJ・MTL・画像Assetの読み込み
- GameObject・Component・Sceneによるゲームオブジェクト管理
- 親子階層を持つTransform
- Camera Componentと複数Light Component
- Unity風のHierarchy・Inspector
- SceneのJSON保存・読み込み
- Undo／Redo
- Asset GUIDと`.meta`ファイル
- Prefabの保存・複数生成・変更反映
- Edit Mode／Play Modeの分離
- ImGui Docking／Floating対応Editor Layout
- Docking／Floating可能なPerformance WindowでFPSとFrame Timeを表示
- Dock可能なScene View／Game View
- Light／CameraのScene View Gizmo
- Directional Light用4段Cascaded Shadow MapとPoint Light用Shadow Cube Map
- 3×3／9サンプルPCFソフトシャドウ
- Metallic／Roughness方式のPBRとNormal Map
- HDR描画、Tone Mapping、FXAA／軽量TAA／MAA
- DepthベースSSAOとHDR Bloom
- DirectInput、XInput、XAudio2
- Blender風デバッグカメラ

## 2. 起動方法

1. `CG2.slnx`をVisual Studioで開きます。
2. 構成を`Debug | x64`または`Release | x64`にします。
3. ソリューションをビルドして実行します。

### Debug版

- ImGui Editorが表示されます。
- 起動時に`Resources/Scenes/MainScene.json`を自動的に読み込みます。
- 保存SceneはEdit ModeのままHierarchy／Inspector／Scene Viewへ表示されます。
- JSONが存在しない、または壊れている場合はWarningを表示し、組み込みSceneを維持します。
- 起動直後はEdit Modeです。
- `Play Mode`ウィンドウの`Play`を押すとゲームを実行します。
- `Stop`を押すとPlay中の変更を破棄してEdit Modeへ戻ります。

### Release版

- ImGui Editorは含まれません。
- 起動時に`Resources/Scenes/MainScene.json`を読み込みます。
- Editorで同Pathへ保存したGameObject、Transform、Component、Light、Camera設定を使用します。
- 起動時に自動的にPlay Modeへ入ります。
- Scene JSONが存在しない、または内容が壊れている場合はError Dialogを表示して終了します。
- Release構成でもビルドと起動を確認済みです。

## 3. エンジン基盤

### Engine

`Engine`は、アプリケーション全体の起動と終了を管理します。

- Win32ウィンドウの生成
- Windowsメッセージ処理
- DirectX、描画、入力、音声、Asset管理の初期化
- ウィンドウリサイズ要求の処理
- フレーム時間の更新
- クラッシュ時の`.dmp`出力
- 各システムが安全な順番で破棄されるように所有権を管理

主なファイル:

- `Engine.h / Engine.cpp`
- `main.cpp`

### FPS非依存のゲーム速度

`GameTimer`が毎フレームの経過秒数を`deltaTime`として計測します。

移動量や回転量を次のように計算することで、60 FPSと144 FPSでゲーム速度が変わりにくい構成です。

```cpp
position += speedPerSecond * deltaTime;
```

主なファイル:

- `GameTimer.h / GameTimer.cpp`

### ウィンドウリサイズ

ウィンドウサイズが変わった場合、次のものを作り直します。

- SwapChainのバックバッファ
- RTV
- 深度バッファとDSV
- ViewportとScissor Rect
- Scene／Game View用Offscreen Render Texture
- デバッグカメラのアスペクト比
- Sprite用の正射影範囲

## 4. DirectX 12とGPUリソース管理

### DirectXCommon

DirectX 12の基本リソースをまとめて管理します。

- Device
- DXGI Factory
- Command Queue
- Command Allocator
- Graphics Command List
- SwapChain
- Render Target
- Depth Stencil
- Fence
- フレームごとの動的Uploadバッファ

バックバッファとフレームリソースは2フレーム分です。

毎フレームGPU全体の完了を待つ方式ではなく、同じフレーム領域を再利用するときだけFenceを待ちます。
これにより、CPUとGPUが並行して処理しやすくなっています。

### 動的バッファ

毎Drawで変化する次のデータは、フレーム専用の共有Upload領域から確保します。

- World／WVP行列
- マテリアル
- 動的頂点
- ライト定数

フレームごとの動的バッファ容量は16 MBです。

### 静的バッファ

変更しない頂点・インデックスはDEFAULTヒープへ転送します。

- モデル頂点
- モデルインデックス
- 球の頂点とインデックス
- Sprite共通インデックス

一時UploadリソースはFence値と一緒に保持し、GPUのコピー完了後に解放します。

### ComPtrとスマートポインタ

リソースの種類に応じて次の所有方法を使っています。

| リソース | 所有方法 |
| --- | --- |
| DirectX COMオブジェクト | `Microsoft::WRL::ComPtr` |
| Engine内部システム | `std::unique_ptr` |
| 複数GameObjectで共有するModel | `std::shared_ptr` |
| XAudio2 Voice | カスタムデリータ付き`std::unique_ptr` |

手動の`Release()`呼び出しを減らし、解放漏れを防ぐ構成です。

### DX12Utility

DirectX 12で共通利用する処理を名前空間へ分離しています。

- HLSLのコンパイル
- Uploadバッファ作成
- DEFAULTバッファ作成
- Textureリソース作成
- Textureデータ転送
- Descriptor Heap作成
- UTF-8／UTF-16変換
- ログ出力

主なファイル:

- `DirectXCommon.h / DirectXCommon.cpp`
- `DX12Utility.h / DX12Utility.cpp`
- `Graphics.h / Graphics.cpp`
- `TextureManager.h / TextureManager.cpp`

## 5. 描画機能

### 共通描画構成

`Graphics`は共通のRoot Signatureと用途別Pipeline Stateを管理します。

- 通常3D用PSO
- 天球用PSO
- Sprite用PSO
- Shadow Map深度描画用PSO
- HDR Sceneを表示用sRGBへ変換するFullscreen Post Process／Tone Mapping用PSO

実際の形状データやDraw処理は、`Model`、`PrimitiveDrawer`、`Sprite`へ分割されています。

### 3Dモデル

OBJモデルを読み込み、頂点バッファとインデックスバッファを作成できます。

対応している主なOBJ／MTL情報:

- OBJ頂点位置
- UV座標
- 法線
- 面インデックス
- `mtllib`
- `usemtl`
- MTLの`newmtl`
- `Kd` 拡散反射色
- `Ks` 鏡面反射色
- `Ns` 鏡面反射の鋭さ
- `Pm` PBR Metallic
- `Pr` PBR Roughness
- `d` 不透明度
- `map_Kd` ベースカラーテクスチャ
- `map_Bump`／`bump`／`norm` Normal Map

`usemtl`の切り替わりごとにSubMeshを作り、マテリアルとテクスチャを切り替えて`DrawIndexedInstanced`します。

Model RendererのInspectorには、OBJで実際に使われているMaterial Slotが`usemtl`名ごとに表示されます。
各Slotの`Select Texture...`から別々のTexture Assetを割り当てられます。
`Use MTL Texture`を押すと、そのSlotだけInspectorの上書きを解除し、MTLの`map_Kd`へ戻します。
Normal MapはMTLから自動取得できるほか、各Slotの`Select Normal Map...`から個別に上書きできます。
Normal Mapは色としてガンマ変換せず、Linear Textureとして読み込みます。
OBJ読込時に各三角形の位置・UV・法線からTangentとHandednessを生成し、Pixel ShaderでTBN空間からWorld法線へ変換します。
PBR Modeでは各SlotのMetallic／Roughnessを使用します。
MTLに`Pr`がない場合は`Ns`からRoughnessを近似し、Inspectorの`Override Metallic / Roughness`で個別調整できます。
`Override UV Transform`を有効にすると、そのSlotだけUV Scale・Rotation・Positionを個別に調整できます。
無効なSlotはModel Renderer全体のUV Transformを使用します。
Material Slot別Texture、Normal Map、UV TransformはScene JSONへMaterial名・Indexと一緒に保存され、Play ModeとRelease版にも反映されます。

同じModel Assetは`shared_ptr`で共有されるため、同じモデルを複数GameObjectから描画できます。
World行列や色などのインスタンス固有情報は、Drawごとに別の動的バッファへ書き込みます。

### 3Dプリミティブ

次の形状を描画できます。

- 頂点を指定できる三角形
- 緯度・経度を16分割した球

両方ともインデックス描画です。
呼び出し側で描画番号を管理する必要はなく、フレーム内の番号は内部で自動管理します。

1フレームの上限:

- 三角形: 1000個
- 球: 100個

### 2Dスプライト

- 4頂点と6インデックスによる四角形描画
- `DrawIndexedInstanced`を使用
- ピクセル座標用の正射影
- 色の指定
- Textureの指定
- UV Transform
- 1フレーム最大1000枚

### 天球

`Resources/sky_sphere/sky_sphere.obj`を天球として読み込めます。

天球用PSOでは通常モデルと異なり、次の設定を使用します。

- 球の内側を描画
- 深度バッファへ書き込まない
- 通常の不透明オブジェクトより先に描画
- カメラ位置へ追従

### Render Order

`RendererComponent`には描画順を表す`RenderOrder`があります。

| 用途 | 既定値 |
| --- | ---: |
| Sky | -1000 |
| 通常の不透明3D | 0 |
| 2D Overlay | 1000 |

Sceneは有効なRendererを収集し、Render Orderの小さい順に描画します。

## 6. TextureとUV Transform

### TextureManager

- DirectXTexによる画像読み込み
- Mipmap生成
- GPU Texture作成
- SRV作成
- 同じファイルの重複読み込み防止
- Texture Handleによる参照
- 最大128枚
- SRV Heapの先頭6枠をImGui Font、Editor Viewport、Directional Shadow Map、HDR Scene、Point Shadow Cube、Scene Depth用に予約

### UV Transform

Model、Sprite、Triangle、Sphereで個別のUV Transformを使用できます。

- UV Scale
- UV Rotation
- UV Position

Inspectorから値を変更できます。

## 7. ライティング

### Lighting Mode

Lighting Windowの`Mode`から、Scene全体のLighting方式を切り替えられます。

| Mode | 拡散反射 | 鏡面反射 | Bounce Light |
| --- | --- | --- | --- |
| Lambert | 標準Lambert | なし | Lambert |
| Half-Lambert | 暗部を柔らかくするHalf-Lambert | なし | Half-Lambert |
| Current | Half-Lambert | Blinn-Phong | 従来通りLambert |
| PBR | Energy Conserving Lambert | Cook-Torrance GGX | Lambert近似 |

`Current`は切り替え機能を追加する前のLighting方式をそのまま維持します。
Specular Strength／Shininessは`Current`を選択した場合だけ使用します。
選択したModeはScene JSONの`lighting.mode`へ保存され、Play ModeとRelease版にも反映されます。
`lighting.mode`が存在しない古いScene JSONは`Current`として読み込みます。

### 複数ライト

Scene内へ複数のLight Componentを配置できます。

| Light | 最大数 | Transformの使用方法 |
| --- | ---: | --- |
| Directional Light | 4 | World回転から方向を計算 |
| Point Light | 16 | World座標を光源位置として使用 |
| Bounce Light | 3 | 毎フレーム生成する一時的なPoint Light近似 |

Point Lightでは次を個別に設定できます。

- Color
- Intensity
- Radius
- Decay
- Enabled

### Cascaded Directional Shadow Map

最初に有効になっているDirectional Light 1灯から、リアルタイムシャドウを生成します。

- 2048×2048×4段の32-bit Depth Texture Array
- Cameraから10／25／60／120の距離で4 Cascadeへ分割
- CascadeごとにLight方向から正射影する深度専用描画Pass
- Model、Triangle、Sphereが影を落とし、同じ3D描画物が影を受ける
- Sky SphereとSpriteはShadow Mapへ描画しない
- Pixel ShaderでShadow Mapを9回比較する3×3 PCF
- 法線とLight方向に応じたReceiver Bias
- Shadow用Rasterizer Depth Bias／Slope Scaled Depth Bias
- Scene ViewとPlay／Release描画の両方へ反映
- Directional Lightが無効な場合はShadow処理を自動的に無効化

現在はShadowを生成するDirectional Lightを1灯に限定しています。
Cascade数、分割距離、Shadow Mapの解像度、Biasは固定値です。

### Point Light Shadow Cube

最初に有効になっているPoint Light 1灯から、全方向のリアルタイムシャドウを生成します。

- 1024×1024×6面の32-bit Depth Cube Map
- Point Lightを中心とした前後左右上下6方向の深度専用描画Pass
- 各面を視野角90度の透視投影で描画
- Point LightのRadiusをShadowのFar距離として使用
- Model、Triangle、Sphereが影を落とし、同じ3D描画物が影を受ける
- Sky SphereとSpriteはShadow Cubeへ描画しない
- Pixel ShaderでCube Mapを9回比較するPCF
- 法線、Light方向、Lightからの距離に応じたReceiver Bias
- Scene ViewとPlay／Release描画の両方へ反映
- Point Lightが無効な場合はShadow処理を自動的に無効化
- Directional ShadowとPoint Shadowを同時に使用可能

現在はShadowを生成するPoint Lightを1灯に限定しています。
Shadow Cubeの解像度とBiasは固定値です。

### スペキュラ

Blinn-Phong方式の鏡面反射を実装しています。

- シーン共通のSpecular Strength
- シーン共通のSpecular Shininess
- MTLの`Ks`による色と強さ
- MTLの`Ns`によるハイライトの鋭さ
- Camera位置を利用した視線方向計算

MTLに`Ns`がある場合はOBJマテリアル値を優先し、値がない描画物はシーン共通値を使います。

### 環境光／IBL

Sky Sphereの経緯度画像を環境Mapとして再利用するIBLを実装しています。

- 法線方向の環境色を使う拡散環境光
- Cameraからの反射方向を使う鏡面環境反射
- MTLの`Ks`を基礎反射率として使うFresnel近似
- `Ns`から求めた粗さに応じたMip Level選択
- Lighting WindowからON／OFF、Intensity、水平Rotationを編集
- 環境Texture GUIDと設定をScene JSONへ保存

現在は経緯度Textureの通常Mip Mapを使った軽量な近似IBLです。
事前畳み込み済みIrradiance Map、Prefiltered Environment Map、BRDF LUTを使うPBR IBLではありません。

### PBR

Metallic／Roughness方式のCook-Torrance PBRを実装しています。

- GGX Normal Distribution
- Smith Geometry
- Schlick Fresnel
- Base Color、Metallic、Roughness
- Normal Map
- Directional／Point Light
- Directional／Point Shadow
- 環境光／環境反射

### HDR／Tone Mapping

Scene ViewとRelease Gameの3D Sceneを`R16G16B16A16_FLOAT`へ描画します。
描画後にFullscreen Triangle Passを実行し、表示用のsRGB Render Targetへ変換します。

- ExposureをEV単位で調整
- None（Clamp）
- Reinhard
- ACES Filmic

### Anti-Aliasing／SSAO／Bloom

HDR SceneとScene Depthを使うFullscreen Post Processを実装しています。

- None／FXAA／TAA／MAAの切り替え
- TAAは4フレーム周期のSub-pixel Resolveを使う軽量方式
- MAAは輪郭方向を判定して輪郭と平行に補間
- Scene Depthの8方向比較によるSSAO
- SSAOのON／OFFとStrength編集
- HDR輝度抽出と25サンプルBlurによるBloom
- BloomのON／OFF、Intensity、Threshold編集
- Scene ViewとPlay／Releaseの両方へ反映
- 設定をScene JSONへ保存・復元
- DebugではTone Mapping済みTextureをImGuiへ表示
- ReleaseではTone Mapping結果をSwapChainへ直接出力
- DockingでScene Viewが小さい場合も、有効描画領域のUV Scaleを使って比率を維持

### 非均一スケールへの対応

法線はWorld行列ではなくWorld逆転置行列で変換します。
X、Y、Zで異なる拡大率を設定しても、法線方向が崩れにくくなっています。

### Bounce Light

モデル表面の反射光を、弱い色付きPoint Lightとして近似できます。

これはリアルタイムGIやレイトレーシングではありません。
反射元モデルの位置・色などから簡易ライトを生成する近似処理です。

主なファイル:

- `LightingManager.h / LightingManager.cpp`
- `LightComponent.h / LightComponent.cpp`
- `Object3d.hlsli`
- `Object3d.VS.hlsl`
- `Object3d.PS.hlsl`
- `ShadowMap.VS.hlsl`

## 8. GameObject・Component・Scene

### GameObject

各GameObjectは次の情報を持ちます。

- 永続的なID
- 名前
- Active状態
- Transform Component
- 任意個数のComponent
- 親GameObject
- 子GameObject一覧

GameObjectを削除すると、その子階層も同じフレーム末尾に削除されます。

### Component

Component基底クラスにはUnityに近いライフサイクルがあります。

- `Awake()`
- `Start()`
- `OnEnable()`
- `OnDisable()`
- `Update(deltaTime)`
- `OnDestroy()`

ComponentごとにEnabledを切り替えられます。
無効なComponentはStart／Updateされません。

### 実装済みComponent

| Component | 役割 |
| --- | --- |
| TransformComponent | Local座標とWorld行列、親子Transform |
| ModelRendererComponent | OBJ Modelの描画 |
| SpriteRendererComponent | 2D Spriteの描画 |
| PrimitiveRendererComponent | Triangle／Sphereの描画 |
| CameraComponent | DebugCameraとGameObject Transformの接続 |
| LightComponent | Directional／Point Lightの配置 |
| PrefabInstanceComponent | 元Prefab GUIDと自動更新設定の保持 |

### Scene

SceneはGameObjectを一元管理します。

- GameObject生成
- IDまたは名前による検索
- 遅延削除
- 即時削除
- Component更新
- Renderer収集と描画
- Shadow Map用Renderer収集と深度描画
- 別Scene階層の追加
- Scene全体の置き換え

## 9. Transform階層

TransformはLocal座標とWorld座標を分離しています。

- Local Scale／Rotation／Position
- Local Matrix
- 親のWorld Matrixを掛けたWorld Matrix
- World Positionの取得と設定
- 親変更時のWorld座標維持

親子関係では次の安全対策があります。

- 自分自身を親にできない
- 自分の子孫を親にできない
- 別SceneのGameObjectを親にできない
- 循環する親子関係を拒否
- 親削除時は子階層も削除
- 親の移動、回転、拡縮を子へ反映

## 10. Renderer Component

Renderer Componentは、GameObjectの計算済みWorld Matrixを使用します。

### ModelRendererComponent

- Model GUID
- 共有Model
- Fallback Texture GUID
- Material Slot別Texture Override GUID
- Material Slot別Normal Map Override GUID
- Material Slot別Metallic／Roughness Override
- Material Slot別UV Transform Override
- Color
- UV Transform
- Lighting Enabled
- Render Order

### SpriteRendererComponent

- Texture GUID
- Color
- UV Transform
- Render Order
- GameObject Transformをピクセル座標として使用

### PrimitiveRendererComponent

- Triangle／Sphere切り替え
- Triangleのローカル頂点
- Texture GUID
- Color
- UV Transform
- Render Order

## 11. 入力

### DirectInput

キーボードとマウスはDirectInput 8で取得します。

次の3状態を判定できます。

- Pressed: 押している間
- Triggered: 押した瞬間
- Released: 離した瞬間

マウスでは次を取得できます。

- 左、右、中、X1、X2ボタン
- 画面上のマウス座標
- フレーム間の移動量
- ホイール量

### XInput

ゲームパッドはXInputで取得します。

- ボタンのPressed／Triggered／Released
- 左スティック
- 右スティック
- 左トリガー
- 右トリガー
- デッドゾーン処理

現在はXInputの0番ゲームパッドを使用します。

## 12. Blender風デバッグカメラ

DebugCameraは注視点を中心とするOrbit Cameraです。

| 操作 | 動作 |
| --- | --- |
| 中マウスドラッグ | 注視点の周囲をOrbit |
| Shift + 中マウスドラッグ | Pan |
| Ctrl + 中マウス上下 | Dolly |
| マウスホイール | Zoom |
| Numpad 1 | 正面 |
| Ctrl + Numpad 1 | 背面 |
| Numpad 3 | 右面 |
| Ctrl + Numpad 3 | 左面 |
| Numpad 7 | 上面 |
| Ctrl + Numpad 7 | 下面 |
| Numpad 5 | Perspective／Orthographic切り替え |
| Home | カメラを初期状態へ戻す |

Scene View画像にMouse Cursorがある間だけEditor CameraをMouse操作できます。
Gizmos CheckboxなどのImGui操作中はCamera操作を止めます。

## 13. サウンド

XAudio2を使用しています。

- WAV読み込み
- 同じ音声ファイルの重複読み込み防止
- 整数Handleによる管理
- 通常再生
- ループ再生
- Stop
- Pause
- Resume
- 音声ごとのVolume
- Master Volume
- 再生中／一時停止中の確認

サンプルとして`Resources/bgm.wav`を読み込みます。
Debug版ではPlay開始時に再生し、Stop時に停止します。

## 14. AssetManager

Model、Texture、PrefabをGUIDで管理します。

### `.meta`ファイル

Assetの隣に`.meta`ファイルを作成し、GUIDを保存します。

例:

```text
Resources/uvChecker.png
Resources/uvChecker.png.meta
```

SceneやComponentはファイルパスではなくGUIDを保存します。

### 実装済み機能

- Model AssetのImport
- Texture AssetのImport
- Prefab AssetのImport
- GUIDからファイルパスを検索
- ファイルパスからGUIDを検索
- 同一Modelの重複読み込み防止
- 同一Textureの重複読み込み防止
- Inspectorからファイル選択
- Asset一覧の再走査
- Assetと`.meta`を一緒に移動した場合の参照維持
- Assetだけ移動した場合、サイズと内容ハッシュによる`.meta`追従

Assetの管理対象ルートは`Resources`です。

## 15. Scene保存・読み込み

SceneをJSONとして保存・読み込みできます。

保存対象:

- Scene名
- GameObject ID
- GameObject名
- Active状態
- 親IDと子階層
- Local Transform
- Component種類
- Component Enabled
- Renderer設定
- Model／Texture GUID
- ModelのMaterial Slot別Texture Override
- ModelのMaterial Slot別Normal Map Override
- ModelのMaterial Slot別Metallic／Roughness Override
- ModelのMaterial Slot別UV Transform Override
- UV Transform
- Light設定
- Camera設定
- Prefab GUID
- Lighting Mode
- シーン共通Specular設定
- 環境光／IBL設定とEnvironment Texture GUID
- Tone Mapping方式とExposure
- Anti-Aliasing方式
- SSAOのON／OFFとStrength
- BloomのON／OFF、Intensity、Threshold

読み込みは一時SceneでJSON全体を検証してから現在のSceneと交換します。
JSONが壊れている場合は、現在のSceneをできるだけ維持します。

`Resources/Scenes/MainScene.json`はDebug／Release共通の起動Sceneとして使用します。
EditorでこのPathへ保存すると、次回のDebug起動時は保存内容をEdit Modeで開きます。
Release版は同じ保存内容を読み込み、Play Modeを自動的に開始します。

主なファイル:

- `SceneSerializer.h / SceneSerializer.cpp`
- `Resources/Scenes/MainScene.json`

## 16. Editor

EditorはDebug構成で使用できます。

### Docking Layout

- Hierarchy、Inspector、Play Mode、Lighting、Sound Control、Performance、Scene View、Game ViewのDocking
- TabをドラッグしてDock位置を変更
- EditorのMain Window内で切り離せるFloating Window
- `Layout > Reset to Default Layout`でUnity風の初期配置へ復元
- Scene ViewはEdit ModeのEditor Camera映像を表示
- Game ViewはPlay ModeのGame Camera映像を表示
- Edit／Play切り替え時に対応するView Tabへ自動フォーカス
- View画像にMouse Cursorがある間だけEditor CameraをMouse操作
- Scene／Game描画をOffscreen Render Textureへ出力して`ImGui::Image`で表示
- Dock位置とWindowサイズを`imgui.ini`へ保存

### Lighting Window

- Lambert／Half-Lambert／Current／PBRのMode切り替え
- Directional／Point Lightの登録数表示
- Directional CSMとPoint Shadow Cubeの対象Light説明
- Current Mode用Specular Strength／Shininess編集
- 環境光／IBLのON／OFF、Intensity、水平Rotation編集
- HDR Tone Mapping方式とExposure編集
- FXAA／軽量TAA／MAAの切り替え
- SSAOのON／OFFとStrength編集
- BloomのON／OFF、Intensity、Threshold編集
- 選択したModeをScene Save／LoadとPlay複製へ反映

### Performance Window

- ImGuiが平滑化した現在のFPSを表示
- 1フレームの処理時間をミリ秒で表示
- 55 FPS以上は緑、30 FPS以上は黄色、30 FPS未満は赤で表示
- ほかのEditor Windowと同様にDocking／Floating可能
- Debug構成だけに表示し、Releaseのゲーム画面には含めない

### Scene View Gizmos

- `Gizmos` CheckboxでEditor専用表示をON／OFF
- Point Lightの位置をLight Color付きIconで表示
- 選択中Point LightのRadiusをScene View上へ表示
- Directional Lightの位置と照射方向をArrowで表示
- 無効なLight／Cameraを半透明表示
- Game Cameraの位置、前方方向、視錐台を表示
- 選択中のLight／CameraをWhite Ringと太線で強調
- Scene View右上にXYZ Orientation Compassを表示
- GizmoはImGui OverlayのためGame ViewとRelease描画には含まれない

### Hierarchy

- 親子構造のTree表示
- GameObject選択
- Root GameObject作成
- Child GameObject作成
- GameObject削除
- 子階層を含む複製
- 名前変更
- F2で名前変更
- Ctrl + Dで複製
- Deleteキーで削除
- 右クリックメニュー
- Drag & Dropによる親変更
- 空白へのDropによる親解除
- 循環する親子変更の拒否

### Inspector

- GameObject名
- ID
- Active状態
- Local Transform
- World座標の表示
- Component Enabled
- Component追加
- Component削除
- Render Order
- Color
- UV Transform
- Model／Texture Asset選択
- Light種類、色、強さ、半径、減衰
- Camera入力の有効／無効
- Prefab操作

追加できるComponent:

- Model Renderer
- Sprite Renderer
- Triangle Renderer
- Sphere Renderer
- Directional Light
- Point Light
- Camera

現在、Camera ComponentはScene内に1個だけ追加できる制限があります。

## 17. Undo／Redo

Sceneをメモリ上のJSONスナップショットへ変換し、操作前後の状態を保存します。

対応している主な操作:

- Transform変更
- Component追加
- Component削除
- GameObject作成
- GameObject削除
- GameObject複製
- 親子変更
- 親解除
- Prefab生成と更新操作

操作:

- Ctrl + Z: Undo
- Ctrl + Y: Redo

履歴は直近50操作まで保持します。
Scene読み込みやEdit／Play切り替え時には、別Sceneへ誤適用しないよう履歴を初期化します。

## 18. Prefab

GameObjectとその子階層を`.prefab`ファイルとして再利用できます。

### 実装済み機能

- GameObjectと子階層をPrefabとして保存
- `.prefab.meta`によるGUID管理
- 同じPrefabから複数インスタンスを生成
- Rootまたは選択GameObjectのChildとして生成
- `Apply To Prefab`で元Prefabへ変更を保存
- `Revert From Prefab`で元Prefabの状態へ戻す
- 同じGUIDのインスタンスへ変更を反映
- `.prefab`の外部更新を約0.5秒ごとに検出
- Auto Updateの有効／無効
- Scene保存時はPrefab GUIDを保存
- 更新前後でPrefab Instance RootのIDを維持
- 更新時にRootの配置、名前、Active、親を維持

標準保存先:

```text
Resources/Prefabs
```

現在のPrefab更新は、子階層とComponentを元Prefabで置き換える方式です。
Unityのようなプロパティ単位のOverrideやNested Prefabはまだ実装していません。

## 19. Edit Mode／Play Mode

### Edit Mode

- Sceneの編集用状態
- Componentの`Start()`と`Update()`は実行しない
- ゲーム固有の移動や回転処理を実行しない
- Hierarchy、Inspector、Scene保存を使用可能
- Sceneは確認用に描画する

### Play開始

1. Edit Sceneをメモリ上のJSONへ保存します。
2. JSONから別のPlay Sceneを生成します。
3. GameObject IDと親子関係を維持します。
4. Edit Scene側のLight登録を停止します。
5. Play Scene側のComponent更新を有効にします。
6. BGMを再生します。

### Play Mode

- Componentの`Start()`と`Update()`を実行
- ゲーム固有処理を実行
- Play SceneのHierarchyとInspectorを編集可能
- Scene Save／Loadを無効化
- PrefabへのApply／Saveを無効化

### Stop

- GPUがPlay Sceneを使い終えるまで待機
- Play Sceneを破棄
- Play開始前のEdit Sceneスナップショットを復元
- Play中のTransform、Component、GameObject変更を破棄
- Light登録をEdit Sceneへ戻す
- BGMを停止

主なファイル:

- `PlayModeManager.h / PlayModeManager.cpp`
- `Scene.h / Scene.cpp`
- `Editor.h / Editor.cpp`
- `main.cpp`

## 20. 現在Resourcesにある主なAsset

- `Resources/Models/cube.obj`
- `Resources/axis/axis.obj`
- `Resources/bunny/bunny.obj`
- `Resources/plane/plane.obj`
- `Resources/suzanne/suzanne.obj`
- `Resources/teapot/teapot.obj`
- `Resources/multiMaterial/multiMaterial.obj`
- `Resources/multiMesh/multiMesh.obj`
- `Resources/sky_sphere/sky_sphere.obj`
- `Resources/sky_sphere/sky_sphere.png`
- `Resources/uvChecker.png`
- `Resources/monsterBall.png`
- `Resources/White.png`
- `Resources/bgm.wav`

## 21. 主なクラスの担当表

| クラス | 主な担当 |
| --- | --- |
| Engine | アプリケーション起動、Window、各Managerの所有 |
| DirectXCommon | DirectX 12本体、SwapChain、Fence、フレームリソース |
| Graphics | 共通PSO、描画システムの生成順と窓口 |
| DX12Utility | DirectX 12共通ヘルパー |
| TextureManager | TextureとSRV管理 |
| Model | OBJ／MTL読み込みとモデル描画 |
| PrimitiveDrawer | Triangle／Sphere描画 |
| Sprite | 2D Sprite描画 |
| LightingManager | 複数ライトとGPU定数管理 |
| DebugCamera | Blender風カメラ |
| InputManager | DirectInput／XInput |
| AudioManager | XAudio2とWAV管理 |
| Scene | GameObjectの所有、更新、描画 |
| GameObject | ID、名前、Component、親子関係 |
| Component | 機能追加とライフサイクル |
| TransformComponent | Local／World Transform |
| AssetManager | GUID、`.meta`、Assetキャッシュ |
| SceneSerializer | Scene JSON保存・復元 |
| PrefabManager | Prefab保存、生成、更新 |
| PlayModeManager | Edit SceneとPlay Sceneの切り替え |
| Editor | Hierarchy、Inspector、Undo／Redo |

## 22. 未実装機能

次の機能は現在のコードにはまだ実装されていません。

### Programming／Scripting

- Scratch風Visual Scripting
- ノードグラフEditor
- ノード接続データのScene／Asset保存
- ノード実行Runtime
- ユーザー定義Script Componentの動的読み込み
- Hot Reload

### Physics／Animation

- Collider
- RigidBody
- 衝突判定と物理演算
- Animation Clip
- Animator／State Machine
- Bone／Skeleton
- Skinned Mesh

### Rendering

- 本物のGlobal Illumination
- Ray Tracing Reflection
- 任意のEffect追加・並べ替えに対応した汎用Post Effect Stack
- Motion VectorとHistory Bufferを使う本格TAA
- GTAO／HBAOなど法線を利用する高品質AO
- Mip Chain／Compute Blurを使う高品質Bloom
- Screen Space Reflection
- Color Grading LUT
- Particle System
- Scene ViewとGame Viewの同時描画
- 複数Cameraの登録と切り替え

### Editor／Asset

- 移動／回転／拡縮をMouse DragできるTransform Gizmo
- Scene View上のGizmo ClickによるGameObject選択
- Asset Browser専用Window
- Material Editor
- Animation Editor
- Visual Script Editor
- 複数SceneのAdditive編集

### Prefab

- Nested Prefab
- Prefab Property単位のOverride
- Override一覧と部分Revert

### Audio／Build

- 3D Audio
- Audio Mixer
- MP3／OggなどWAV以外のAudio読み込み
- Packaging／Install用のゲーム配布工程
- EditorからのBuild設定画面

### Architecture

- ECS
- Plugin System
- Network／Multiplayer

## 23. 簡易実装と現在の制限

次の機能は利用できますが、本格的なEngineと比較すると制限があります。

### Scene View／Game View

- 1枚のOffscreen Render Textureを共有し、Edit ModeではScene View、Play ModeではGame Viewへ表示します。
- Scene ViewとGame Viewを同時に描画することはできません。
- Floating WindowはEngineのMain Window内に限定しています。
- OS上の別Windowへ分離するImGui Multi-Viewportは、Layout復元時の安定性を優先して無効です。

### Gizmo

- LightとCameraの位置、向き、範囲を表示できます。
- GizmoはImGui DrawListによる画面Overlayで、3D Objectとしては描画していません。
- GizmoをMouse DragしてTransformを変更する機能はありません。
- Gizmo IconをClickしてGameObjectを選択する機能はありません。

### Camera

- Scene View用Editor CameraとPlay用Game Cameraは分離されています。
- Camera Componentは1 Sceneにつき1個までです。
- 複数CameraのPriority、切り替え、Render Texture出力はありません。

### Lighting

- Bounce Lightは弱いPoint Lightを生成する間接光の近似です。
- Bounce Lightはレイトレーシングや本物のGlobal Illuminationではありません。
- Shadowを生成するLightは、最初に有効なDirectional Light 1灯とPoint Light 1灯です。
- Directional CSMは2048×2048×4段、Point Shadow Cubeは1024×1024×6面で、分割距離とBiasは固定です。
- 複数の同種LightによるShadow、透過TextureのAlpha Test Shadowは未対応です。

### Model／Material

- OBJ／MTL、glTF／GLBはAssimp v6.0.5で読み込みます。
- 三角形化、頂点共有、法線生成、接線生成、左手座標系変換を読み込み時に行います。
- glTFの複数Mesh、Node親子階層、PBR Material、GLB内蔵画像に対応しています。
- Metallic-Roughness TextureはglTF 2.0仕様どおりGをRoughness、BをMetallicとして使用します。
- glTFのBone、1頂点最大4ウェイトのGPU Skinning、複数Animation Clipの再生・切り替えに対応しています。
- Animationごとの再生速度、Loop、Pause、Stop、Time操作とScene保存に対応しています。
- Bone上限は1Modelあたり128本です。Morph TargetとAnimation Blend／Cross Fadeは未対応です。
- IBLは通常Mip Mapを利用した近似で、Irradiance／Prefilter／BRDF LUTは未対応です。
- 半透明Object専用のSorting／Blend Pipelineはありません。

### Post Process

- TAAはHistory BufferとMotion Vectorを使わない、4フレーム周期の軽量Sub-pixel Resolveです。
- MAAは輪郭方向に沿って補間する簡易実装で、SMAAのArea／Search Textureは使用していません。
- SSAOはScene Depthの8方向比較による簡易方式で、G-Bufferの法線は使用していません。
- BloomはHDR Textureを25回Samplingする単一解像度方式で、Mip Chainや分離Blurは使用していません。
- Post Processの順番はAA、SSAO、Bloom、Tone Mappingの固定構成です。

### Prefab

- Prefab更新は子階層とComponentを元Prefabで置き換える方式です。
- InstanceごとのProperty Overrideは保持しません。
- Nested Prefabには対応していません。

### Input／Audio

- XInputは0番のGamepad 1台だけを使用します。
- Audio AssetはWAVに限定されています。
- 3D位置による音量／Pan制御やMixerはありません。

### Release／配布

- Release版はImGui Editorを含まず、`Resources/Scenes/MainScene.json`を読み込んで自動的にPlay Modeへ入ります。
- Release版で別のSceneを起動するScene選択設定はなく、起動Pathは現在固定です。
- Release実行ファイルの生成はできますが、Asset PackagingやInstaller生成はありません。

## 24. ビルド確認状況

直近の確認結果:

- Debug x64: ビルド成功、警告0、エラー0
- Release x64: ビルド成功、警告0、エラー0
- Prefab保存・複数生成・自動反映テスト: 成功
- Edit／Play Scene複製・変更破棄テスト: 成功
- Play切り替え時のLight二重登録防止テスト: 成功
- Release版の自動Play起動テスト: 成功
- Scene／Game ViewのOffscreen描画とDock Layout復元テスト: 成功
- Scene View Gizmo追加後の10秒間起動・描画テスト: 成功
- 保存済み`MainScene.json`（15 GameObject）のRelease読み込みテスト: 成功
- JSON読み込み後のRelease Playを10秒間実行: 成功
- Lambert／Half-Lambert／CurrentのHLSLコンパイル: 成功、警告0
- Lighting ModeのPlay複製・Stop復元自己テスト: 成功
- `lighting.mode`を読み込んだRelease描画を10秒間実行: 成功
- Directional Shadow Map／3×3 PCFのDXCコンパイル: 成功、警告0
- 4段Cascaded Shadow Map／Texture Array比較Sampling: 成功、警告0
- FXAA／TAA／MAA、SSAO、Bloom Shaderの実行時コンパイル: 成功、警告0
- Scene DepthのDSV／SRV切り替えを含むDebug 10秒起動: 成功
- CSM／Post Processを有効にしたRelease 10秒起動: 成功
- Shadow Map追加後のDebug描画を10秒間実行: 成功
- Shadow Map追加後の保存済みSceneによるRelease描画を10秒間実行: 成功
- Normal Map／環境光IBL追加後のDebugビルド: 成功、警告0、エラー0
- Normal Map／環境光IBL追加後のDebug起動・描画テスト: 成功
- Normal Map／環境光IBL追加後のReleaseビルド・保存済みScene起動テスト: 成功、警告0、エラー0
- PBR／HDR／Tone Mapping追加後のDebugビルド・5 Shaderコンパイル: 成功、警告0、エラー0
- PBR／HDR／Tone Mapping追加後のDebug起動・描画テスト: 成功
- PBR／HDR／Tone Mapping追加後のReleaseビルド・保存済みScene起動テスト: 成功、警告0、エラー0
- HDR有効領域UV補正後のDebug／Releaseビルド・起動テスト: 成功、警告0、エラー0
- Performance Window追加後のDebugビルド: 成功、警告0、エラー0
- FPS／Frame Time表示追加後のDebug起動を8秒間実行: 成功
- Debug起動時の`MainScene.json`自動読み込みとEdit Mode描画を10秒間実行: 成功
- Startup Scene／Shadow Map処理整理後のDebug・Release描画を各10秒間実行: 成功
- Assimp OBJ Importer追加後のDebug／Development／Releaseビルド: 成功、エラー0
- Assimpで既存`cube.obj`を読み込んだDebug起動を6秒間実行: 成功
- glTF（9 Mesh／12 Node）の複数Mesh・親子階層読み込みテスト: 成功
- GLB内蔵画像のメモリ読込・GPU Texture生成テスト: 成功
- glTF Metallic-Roughness TextureのLinear読込・Handle生成テスト: 成功
- glTF／GLB対応後のDebug／Development／Releaseビルド: 成功、エラー0
- `simple_skin.gltf`のBone／Weight／Animation Clip読み込み・Pose評価自己テスト: 成功
- Skeletal Animation追加後のDebug起動8秒・Development／Releaseビルド: 成功
