#include "Object3d.hlsli"

// t0はTextureManagerが選んだ画像、s0はGraphicsが固定で用意したサンプラー
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

float4 main(VertexShaderOutput input) : SV_TARGET
{
    // テクスチャの色をサンプリング
    float4 textureColor = gTexture.Sample(gSampler, input.texcoord);
    
    float4 finalColor;
    
    // ライティングを有効にする場合。
    if (gEnableLighting != 0)
    {
        float3 normal = normalize(input.normal);
        
        // 2. ライトの方向を逆向きにする (表面から光源に向かうベクトルにするため)
        float3 lightDirection = normalize(-gDirectionalLight.direction);
        
        // 3. Half-Lambert反射の計算
        // 通常のLambertは内積が0以下になると急に真っ黒になる。
        // Half-Lambertでは -1～1 の内積を 0～1 へ移し、陰側にも滑らかな明るさを残す。
        float NdotL = dot(normal, lightDirection);
        float halfLambert = saturate(NdotL * 0.5f + 0.5f);

        // 二乗すると明るい面と暗い面の差が適度に戻り、立体感を保ちやすい。
        halfLambert *= halfLambert;
        
        // 4. 光の強さと色を掛け合わせる
        // テクスチャの色 * マテリアルの色 * ライトの色 * ライトの強度 * Half-Lambert係数
        finalColor.rgb = textureColor.rgb * gMaterialColor.rgb * gDirectionalLight.color.rgb * gDirectionalLight.intensity * halfLambert;
        
        // アルファ値はテクスチャとマテリアルのものをそのまま使う
        finalColor.a = textureColor.a * gMaterialColor.a;
    }
    // ライティングを無効にする場
    else
    {
        finalColor = textureColor * gMaterialColor;
    }
    
    return finalColor;
}
