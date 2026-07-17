#include "Object3d.hlsli"

// t0はTextureManagerが選んだ画像、s0はGraphicsが固定で用意したサンプラー
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// Half-Lambertを共通関数にし、SunとPoint Lightの両方で同じ陰影を使う。
float CalculateHalfLambert(float3 normal, float3 lightDirection)
{
    float halfLambert = saturate(dot(normal, lightDirection) * 0.5f + 0.5f);
    return halfLambert * halfLambert;
}

float4 main(VertexShaderOutput input) : SV_TARGET
{
    // 元のUVを拡大縮小・回転・平行移動してからテクスチャを読み取る。
    // z=0、w=1にすることで、4x4行列の平行移動成分も適用できる。
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gUVTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    
    float4 finalColor;
    
    // ライティングを有効にする場合。
    if (gEnableLighting != 0)
    {
        float3 normal = normalize(input.normal);

        // 複数ライトの寄与を加算してから、テクスチャとマテリアルの色へ掛ける。
        float3 accumulatedLight = float3(0.0f, 0.0f, 0.0f);

        if (gDirectionalLight.enabled != 0)
        {
            // Sunは位置を持たず、全ての場所へ同じ方向から光が届く。
            float3 sunDirection = normalize(-gDirectionalLight.direction);
            float sunDiffuse = CalculateHalfLambert(normal, sunDirection);
            accumulatedLight +=
                gDirectionalLight.color.rgb *
                gDirectionalLight.intensity *
                sunDiffuse;
        }

        if (gPointLight.enabled != 0)
        {
            // ピクセルからPoint Lightへ向かうベクトルを求める。
            float3 toLight = gPointLight.position - input.worldPosition;
            float distanceToLight = length(toLight);
            float3 pointDirection = toLight / max(distanceToLight, 0.0001f);

            // radiusの外側では0。内側ではdecayに応じて滑らかに減衰させる。
            float normalizedDistance = distanceToLight / max(gPointLight.radius, 0.0001f);
            float attenuation = pow(
                saturate(1.0f - normalizedDistance),
                max(gPointLight.decay, 0.0001f));
            float pointDiffuse = CalculateHalfLambert(normal, pointDirection);

            accumulatedLight +=
                gPointLight.color.rgb *
                gPointLight.intensity *
                pointDiffuse *
                attenuation;
        }

        finalColor.rgb =
            textureColor.rgb * gMaterialColor.rgb * accumulatedLight;
        
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
