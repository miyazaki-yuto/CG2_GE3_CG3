#include "Object3d.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

float4 main(VertexShaderOutput input) : SV_TARGET
{
    // テクスチャの色をサンプリング
    float4 textureColor = gTexture.Sample(gSampler, input.texcoord);
    
    float4 finalColor;
    
    // ライティングを有効にする場合 (3Dオブジェクトなど)
    if (gEnableLighting != 0)
    {
        float3 normal = normalize(input.normal);
        
        // 2. ライトの方向を逆向きにする (表面から光源に向かうベクトルにするため)
        float3 lightDirection = normalize(-gDirectionalLight.direction);
        
        // 3. ランバート反射の計算 (法線とライト方向の内積)
        // 角度が90度を超えた場合(裏側)は0未満になるため、saturate関数で 0.0 ～ 1.0 にクランプする
        float NdotL = saturate(dot(normal, lightDirection));
        
        // 4. 光の強さと色を掛け合わせる
        // テクスチャの色 * マテリアルの色 * ライトの色 * ライトの強度 * 反射率(cosθ)
        finalColor.rgb = textureColor.rgb * gMaterialColor.rgb * gDirectionalLight.color.rgb * gDirectionalLight.intensity * NdotL;
        
        // アルファ値はテクスチャとマテリアルのものをそのまま使う
        finalColor.a = textureColor.a * gMaterialColor.a;
    }
    // ライティングを無効にする場合 (Spriteなど)
    else
    {
        // 陰影をつけず、テクスチャカラーとマテリアルカラーをそのまま出力
        finalColor = textureColor * gMaterialColor;
    }
    
    return finalColor;
}