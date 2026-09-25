#include "Object3d.hlsli"

// t0はTextureManagerが選んだ画像、s0はGraphicsが固定で用意したサンプラー
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);
Texture2DArray<float> gDirectionalShadowMap : register(t1);
SamplerComparisonState gShadowSampler : register(s1);
Texture2D<float4> gNormalTexture : register(t2);
Texture2D<float4> gEnvironmentTexture : register(t3);
TextureCube<float> gPointShadowMap : register(t4);

static const float kPi = 3.14159265358979323846f;

float2 DirectionToEnvironmentUV(float3 direction)
{
    direction = normalize(direction);
    float longitude = atan2(direction.z, direction.x) + gEnvironmentRotation;
    float latitude = acos(clamp(direction.y, -1.0f, 1.0f));
    return float2(frac(longitude / (2.0f * kPi) + 0.5f), latitude / kPi);
}

float3 SampleEnvironment(float3 direction, float mipLevel)
{
    return gEnvironmentTexture.SampleLevel(
        gSampler, DirectionToEnvironmentUV(direction), mipLevel).rgb;
}

float3 ApplyNormalMap(
    float3 interpolatedNormal,
    float4 interpolatedTangent,
    float2 uv)
{
    float normalLengthSquared = dot(
        interpolatedNormal, interpolatedNormal);
    float3 normal = normalLengthSquared > 0.000001f
        ? interpolatedNormal * rsqrt(normalLengthSquared)
        : float3(0.0f, 1.0f, 0.0f);
    float3 mappedNormal = normal;
    if (gNormalMapEnabled != 0)
    {

    // 補間と非均一Scaleで少し傾いたTangentをNormalへ直交させ直す。
        float3 tangent = interpolatedTangent.xyz -
            normal * dot(interpolatedTangent.xyz, normal);
        float tangentLengthSquared = dot(tangent, tangent);
        if (tangentLengthSquared >= 0.000001f)
        {
            tangent *= rsqrt(tangentLengthSquared);
            float handedness = interpolatedTangent.w < 0.0f
                ? -1.0f
                : 1.0f;
            float3 bitangent = normalize(cross(normal, tangent)) *
                handedness;
            float3 tangentNormal =
                gNormalTexture.Sample(gSampler, uv).xyz * 2.0f - 1.0f;
            mappedNormal = normalize(
                tangent * tangentNormal.x +
                bitangent * tangentNormal.y +
                normal * tangentNormal.z);
        }
    }
    return mappedNormal;
}

float CalculateLambert(float3 normal, float3 lightDirection)
{
    return saturate(dot(normal, lightDirection));
}

// Half-Lambertを共通関数にし、SunとPoint Lightの両方で同じ陰影を使う。
float CalculateHalfLambert(float3 normal, float3 lightDirection)
{
    float halfLambert = saturate(dot(normal, lightDirection) * 0.5f + 0.5f);
    return halfLambert * halfLambert;
}

float CalculateSceneDiffuse(float3 normal, float3 lightDirection)
{
    float diffuse = CalculateHalfLambert(normal, lightDirection);
    if (gLightingMode == kLightingModeLambert)
    {
        diffuse = CalculateLambert(normal, lightDirection);
    }

    return diffuse;
}

// Directional Shadow Mapを使った陰影を計算する。
float CalculateDirectionalShadow(
    float3 worldPosition,
    float3 normal,
    float3 surfaceToLight)
{
    float shadowFactor = 1.0f;
    if (gDirectionalShadowEnabled != 0)
    {
        float cameraDistance = distance(worldPosition, gCameraPosition);
        int cascadeIndex = 0;
        cascadeIndex += cameraDistance > gDirectionalShadowCascadeSplits.x;
        cascadeIndex += cameraDistance > gDirectionalShadowCascadeSplits.y;
        cascadeIndex += cameraDistance > gDirectionalShadowCascadeSplits.z;
        cascadeIndex = min(
            cascadeIndex,
            max(gDirectionalShadowCascadeCount - 1, 0));
        float4 lightClipPosition = mul(
            float4(worldPosition, 1.0f),
            gDirectionalShadowViewProjections[cascadeIndex]);
        if (lightClipPosition.w > 0.0f)
        {
            float3 lightNdc = lightClipPosition.xyz / lightClipPosition.w;
            float2 shadowUv = float2(
                lightNdc.x * 0.5f + 0.5f,
                -lightNdc.y * 0.5f + 0.5f);
            bool isInsideShadowMap =
                shadowUv.x > 0.0f && shadowUv.x < 1.0f &&
                shadowUv.y > 0.0f && shadowUv.y < 1.0f &&
                lightNdc.z > 0.0f && lightNdc.z < 1.0f;
            if (isInsideShadowMap)
            {
                float slopeFactor =
                    1.0f - saturate(dot(normal, surfaceToLight));
                float receiverBias = max(
                    gDirectionalShadowBias * slopeFactor,
                    gDirectionalShadowBias * 0.25f);
                float comparisonDepth = lightNdc.z - receiverBias;
                const float2 texel = gDirectionalShadowTexelSize;
                float visibility = 0.0f;
                [unroll]
                for (int offsetY = -1; offsetY <= 1; ++offsetY)
                {
                    [unroll]
                    for (int offsetX = -1; offsetX <= 1; ++offsetX)
                    {
                        visibility += gDirectionalShadowMap.SampleCmpLevelZero(
                            gShadowSampler,
                            float3(
                                shadowUv + float2(offsetX, offsetY) * texel,
                                cascadeIndex),
                            comparisonDepth);
                    }
                }
                shadowFactor = visibility / 9.0f;
            }
        }
    }
    return shadowFactor;
}

float CalculatePointShadow(
    float3 worldPosition,
    float3 normal,
    float3 surfaceToLight)
{
    float shadowFactor = 1.0f;
    if (gPointShadowEnabled != 0)
    {
        float3 lightToSurface =
            worldPosition - gPointShadowPosition;
        float distanceToLight = length(lightToSurface);
        if (distanceToLight > gPointShadowNearClip &&
            distanceToLight < gPointShadowFarClip)
        {

    // Cube各面のPerspective Depthは半径ではなく、選ばれた面の主軸距離で決まる。
    float majorAxisDepth = max(
        abs(lightToSurface.x),
        max(abs(lightToSurface.y), abs(lightToSurface.z)));
    float depthRange =
        gPointShadowFarClip - gPointShadowNearClip;
    float comparisonDepth =
        gPointShadowFarClip / depthRange -
        (gPointShadowNearClip * gPointShadowFarClip) /
        (depthRange * max(majorAxisDepth, 0.0001f));
    float slopeFactor =
        1.0f - saturate(dot(normal, surfaceToLight));
    comparisonDepth -= gPointShadowBias * (0.5f + slopeFactor);

    float3 sampleDirection =
        lightToSurface / max(distanceToLight, 0.0001f);
    float3 helperAxis = abs(sampleDirection.y) < 0.99f
        ? float3(0.0f, 1.0f, 0.0f)
        : float3(1.0f, 0.0f, 0.0f);
    float3 sampleRight = normalize(cross(helperAxis, sampleDirection));
    float3 sampleUp = cross(sampleDirection, sampleRight);
    float directionOffset = gPointShadowTexelSize * 2.0f;

    // Cube方向を少しずつずらした9 Sampleで、Point Lightの影の縁を柔らかくする。
    float visibility = 0.0f;
    [unroll]
    for (int offsetY = -1; offsetY <= 1; ++offsetY)
    {
        [unroll]
        for (int offsetX = -1; offsetX <= 1; ++offsetX)
        {
            float3 offsetDirection = normalize(
                sampleDirection +
                sampleRight * (float(offsetX) * directionOffset) +
                sampleUp * (float(offsetY) * directionOffset));
            visibility += gPointShadowMap.SampleCmpLevelZero(
                gShadowSampler,
                offsetDirection,
                comparisonDepth);
        }
    }
            shadowFactor = visibility / 9.0f;
        }
    }
    return shadowFactor;
}

// Blinn-Phong方式で鏡面反射を求める。
// Strengthは明るさ、Shininessはハイライトの鋭さを制御する
float CalculateSpecular(
    float3 normal,
    float3 lightDirection,
    float3 viewDirection)
{
    // 光が当たっていない裏面にはスペキュラを発生させない。
    float normalDotLight = saturate(dot(normal, lightDirection));
    float3 halfVectorSource = lightDirection + viewDirection;
    float halfVectorLengthSquared = max(
        dot(halfVectorSource, halfVectorSource), 0.00000001f);
    float3 halfVector = halfVectorSource * rsqrt(halfVectorLengthSquared);
    // OBJにNsがあればその値を優先し、MTLがない描画物はシーン共通値を使う。
    float effectiveShininess = gMaterialSpecularShininess > 0.0f
        ? gMaterialSpecularShininess
        : gSpecularShininess;
    float specularFactor = pow(
        saturate(dot(normal, halfVector)),
        max(effectiveShininess, 1.0f));
    // stepで裏面を0にする。分岐を使わないためGPUでも扱いやすい。
    float frontFaceMask = step(0.0001f, normalDotLight);
    return specularFactor * max(gSpecularStrength, 0.0f) * frontFaceMask;
}

float3 FresnelSchlick(float cosineTheta, float3 reflectanceAtNormal)
{
    return reflectanceAtNormal +
        (1.0f - reflectanceAtNormal) *
        pow(1.0f - saturate(cosineTheta), 5.0f);
}

float3 FresnelSchlickRoughness(
    float cosineTheta,
    float3 reflectanceAtNormal,
    float roughness)
{
    return reflectanceAtNormal +
        (max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness),
            reflectanceAtNormal) - reflectanceAtNormal) *
        pow(1.0f - saturate(cosineTheta), 5.0f);
}

float DistributionGGX(
    float3 normal,
    float3 halfVector,
    float roughness)
{
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float normalDotHalf = saturate(dot(normal, halfVector));
    float denominatorPart =
        normalDotHalf * normalDotHalf * (alphaSquared - 1.0f) + 1.0f;
    return alphaSquared /
        max(kPi * denominatorPart * denominatorPart, 0.000001f);
}

float GeometrySchlickGGX(float normalDotDirection, float roughness)
{
    float remappedRoughness = roughness + 1.0f;
    float k = remappedRoughness * remappedRoughness / 8.0f;
    return normalDotDirection /
        max(normalDotDirection * (1.0f - k) + k, 0.000001f);
}

float GeometrySmith(
    float3 normal,
    float3 viewDirection,
    float3 lightDirection,
    float roughness)
{
    return GeometrySchlickGGX(
            saturate(dot(normal, viewDirection)), roughness) *
        GeometrySchlickGGX(
            saturate(dot(normal, lightDirection)), roughness);
}

// Cook-Torrance BRDF。Texture色をBase ColorとしてMetallic/Roughness方式で評価する。
float3 EvaluatePbrLight(
    float3 normal,
    float3 viewDirection,
    float3 lightDirection,
    float3 baseColor,
    float metallic,
    float roughness)
{
    float normalDotLight = saturate(dot(normal, lightDirection));
    float normalDotView = saturate(dot(normal, viewDirection));
    float3 result = float3(0.0f, 0.0f, 0.0f);
    if (normalDotLight > 0.0f && normalDotView > 0.0f)
    {
        float3 halfVectorSource = viewDirection + lightDirection;
        float3 halfVector = halfVectorSource /
            max(length(halfVectorSource), 0.0001f);
        float3 reflectanceAtNormal = lerp(
            float3(0.04f, 0.04f, 0.04f), baseColor, metallic);
        float3 fresnel = FresnelSchlick(
            saturate(dot(halfVector, viewDirection)),
            reflectanceAtNormal);
        float distribution = DistributionGGX(
            normal, halfVector, roughness);
        float geometry = GeometrySmith(
            normal, viewDirection, lightDirection, roughness);
        float3 specular = distribution * geometry * fresnel /
            max(4.0f * normalDotView * normalDotLight, 0.0001f);
        float3 diffuseWeight =
            (1.0f - fresnel) * (1.0f - metallic);
        result = (diffuseWeight * baseColor / kPi + specular) *
            normalDotLight;
    }
    return result;
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
        float3 normal = ApplyNormalMap(
            input.normal, input.tangent, transformedUV.xy);

        // ピクセルからカメラへ向かう単位ベクトル。長さ0によるNaNを防ぐ。
        float3 toCamera = gCameraPosition - input.worldPosition;
        float3 viewDirection = toCamera / max(length(toCamera), 0.0001f);

        // 複数ライトの寄与を加算してから、テクスチャとマテリアルの色へ掛ける。
        float3 accumulatedLight = float3(0.0f, 0.0f, 0.0f);
        float3 accumulatedSpecular = float3(0.0f, 0.0f, 0.0f);
        float3 environmentDiffuse = float3(0.0f, 0.0f, 0.0f);
        float3 environmentSpecular = float3(0.0f, 0.0f, 0.0f);
        float3 pbrDirectLight = float3(0.0f, 0.0f, 0.0f);
        float3 surfaceColor =
            textureColor.rgb * gMaterialColor.rgb;
        float metallic = saturate(gMaterialMetallic);
        float roughness = clamp(gMaterialRoughness, 0.04f, 1.0f);

        [unroll]
        for (int directionalIndex = 0;
            directionalIndex < kMaxDirectionalLights;
            ++directionalIndex)
        {
            DirectionalLight directionalLight =
                gDirectionalLights[directionalIndex];
            if (directionalLight.enabled == 0)
            {
                continue;
            }

            // Sunは位置を持たず、全ての場所へ同じ方向から光が届く。
            float3 sunDirection = normalize(-directionalLight.direction);
            float sunDiffuse = CalculateSceneDiffuse(normal, sunDirection);
            float shadowVisibility = directionalIndex ==
                gDirectionalShadowLightIndex
                ? CalculateDirectionalShadow(
                    input.worldPosition, normal, sunDirection)
                : 1.0f;
            if (gLightingMode == kLightingModePBR)
            {
                pbrDirectLight += EvaluatePbrLight(
                    normal,
                    viewDirection,
                    sunDirection,
                    surfaceColor,
                    metallic,
                    roughness) *
                    directionalLight.color.rgb *
                    directionalLight.intensity *
                    shadowVisibility;
            }
            else
            {
                accumulatedLight +=
                    directionalLight.color.rgb *
                    directionalLight.intensity *
                    sunDiffuse *
                    shadowVisibility;
                if (gLightingMode == kLightingModeCurrent)
                {
                    float sunSpecular = CalculateSpecular(
                        normal, sunDirection, viewDirection);
                    accumulatedSpecular +=
                        directionalLight.color.rgb *
                        directionalLight.intensity *
                        sunSpecular *
                        shadowVisibility;
                }
            }
        }

        [loop]
        for (int pointIndex = 0;
            pointIndex < kMaxPointLights;
            ++pointIndex)
        {
            PointLight pointLight = gPointLights[pointIndex];
            if (pointLight.enabled == 0)
            {
                continue;
            }

            // ピクセルからPoint Lightへ向かうベクトルを求める。
            float3 toLight = pointLight.position - input.worldPosition;
            float distanceToLight = length(toLight);
            float3 pointDirection = toLight / max(distanceToLight, 0.0001f);

            // radiusの外側では0。内側ではdecayに応じて滑らかに減衰させる。
            float normalizedDistance = distanceToLight / max(pointLight.radius, 0.0001f);
            float attenuation = pow(
                saturate(1.0f - normalizedDistance),
                max(pointLight.decay, 0.0001f));
            float pointDiffuse = CalculateSceneDiffuse(normal, pointDirection);
            float pointShadowVisibility =
                pointIndex == gPointShadowLightIndex
                ? CalculatePointShadow(
                    input.worldPosition, normal, pointDirection)
                : 1.0f;

            if (gLightingMode == kLightingModePBR)
            {
                pbrDirectLight += EvaluatePbrLight(
                    normal,
                    viewDirection,
                    pointDirection,
                    surfaceColor,
                    metallic,
                    roughness) *
                    pointLight.color.rgb *
                    pointLight.intensity *
                    attenuation *
                    pointShadowVisibility;
            }
            else
            {
                accumulatedLight +=
                    pointLight.color.rgb *
                    pointLight.intensity *
                    pointDiffuse *
                    attenuation *
                    pointShadowVisibility;
                if (gLightingMode == kLightingModeCurrent)
                {
                    float pointSpecular = CalculateSpecular(
                        normal, pointDirection, viewDirection);
                    accumulatedSpecular +=
                        pointLight.color.rgb *
                        pointLight.intensity *
                        pointSpecular *
                        attenuation *
                        pointShadowVisibility;
                }
            }
        }

        // モデル表面からの反射光を、複数の弱い点光源として近似する。
        // 間接光なのでスペキュラには加えず、拡散反射成分だけへ加算する。
        [loop]
        for (int bounceIndex = 0;
            bounceIndex < min(gBounceLightCount, kMaxBounceLights);
            ++bounceIndex)
        {
            BounceLight bounceLight = gBounceLights[bounceIndex];
            if (bounceLight.enabled == 0)
            {
                continue;
            }

            float3 toBounceLight =
                bounceLight.position - input.worldPosition;
            float distanceToBounceLight = length(toBounceLight);
            float3 bounceDirection = toBounceLight /
                max(distanceToBounceLight, 0.0001f);

            float normalizedBounceDistance = distanceToBounceLight /
                max(bounceLight.radius, 0.0001f);
            float bounceAttenuation = pow(
                saturate(1.0f - normalizedBounceDistance),
                max(bounceLight.decay, 0.0001f));

            // CurrentのBounceは従来通りLambert。Half-Lambert選択時だけ柔らかくする。
            if (gLightingMode == kLightingModePBR)
            {
                pbrDirectLight +=
                    surfaceColor *
                    (1.0f - metallic) *
                    CalculateLambert(normal, bounceDirection) /
                    kPi *
                    bounceLight.color.rgb *
                    bounceLight.intensity *
                    bounceAttenuation;
            }
            else
            {
                float bounceDiffuse =
                    gLightingMode == kLightingModeHalfLambert
                    ? CalculateHalfLambert(normal, bounceDirection)
                    : CalculateLambert(normal, bounceDirection);
                accumulatedLight +=
                    bounceLight.color.rgb *
                    bounceLight.intensity *
                    bounceDiffuse *
                    bounceAttenuation;
            }
        }

        if (gEnvironmentEnabled != 0 && gEnvironmentIntensity > 0.0f)
        {
            uint environmentWidth = 0;
            uint environmentHeight = 0;
            uint environmentMipCount = 1;
            gEnvironmentTexture.GetDimensions(
                0, environmentWidth, environmentHeight, environmentMipCount);
            float maximumMip = max(float(environmentMipCount) - 1.0f, 0.0f);

            if (gLightingMode == kLightingModePBR)
            {
                float3 reflectanceAtNormal = lerp(
                    float3(0.04f, 0.04f, 0.04f),
                    surfaceColor,
                    metallic);
                float normalDotView =
                    saturate(dot(normal, viewDirection));
                float3 fresnel = FresnelSchlickRoughness(
                    normalDotView,
                    reflectanceAtNormal,
                    roughness);
                float3 diffuseWeight =
                    (1.0f - fresnel) * (1.0f - metallic);
                environmentDiffuse =
                    SampleEnvironment(normal, maximumMip * 0.75f) *
                    diffuseWeight *
                    max(gEnvironmentIntensity, 0.0f);
                float3 reflectionDirection =
                    reflect(-viewDirection, normal);
                environmentSpecular = SampleEnvironment(
                    reflectionDirection, roughness * maximumMip) *
                    fresnel *
                    max(gEnvironmentIntensity, 0.0f);
            }
            else
            {
                // 高いMipを使い、空全体から回り込む柔らかな拡散環境光として近似する。
                float3 diffuseEnvironment = SampleEnvironment(
                    normal, maximumMip * 0.75f);
                environmentDiffuse = diffuseEnvironment *
                    max(gEnvironmentIntensity, 0.0f);
                if (gLightingMode == kLightingModeCurrent)
                {
                    float effectiveShininess =
                        gMaterialSpecularShininess > 0.0f
                        ? gMaterialSpecularShininess
                        : gSpecularShininess;
                    float legacyRoughness = saturate(sqrt(
                        2.0f /
                        (max(effectiveShininess, 1.0f) + 2.0f)));
                    float3 reflectionDirection =
                        reflect(-viewDirection, normal);
                    float3 reflectedEnvironment = SampleEnvironment(
                        reflectionDirection,
                        legacyRoughness * maximumMip);
                    float viewFresnel = pow(
                        1.0f -
                        saturate(dot(normal, viewDirection)),
                        5.0f);
                    float3 fresnel = gMaterialSpecularColor.rgb +
                        (1.0f - gMaterialSpecularColor.rgb) *
                        viewFresnel;
                    environmentSpecular =
                        reflectedEnvironment *
                        fresnel *
                        max(gEnvironmentIntensity, 0.0f);
                }
            }
        }

        if (gLightingMode == kLightingModePBR)
        {
            finalColor.rgb =
                pbrDirectLight +
                surfaceColor * environmentDiffuse +
                environmentSpecular;
        }
        else
        {
            finalColor.rgb =
                surfaceColor *
                    (accumulatedLight + environmentDiffuse) +
                accumulatedSpecular * gMaterialSpecularColor.rgb +
                environmentSpecular;
        }
        
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
