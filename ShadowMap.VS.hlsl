cbuffer ShadowTransformBuffer : register(b0)
{
    float4x4 gLightWorldViewProjection;
}

struct ShadowVertexInput
{
    float4 position : POSITION0;
};

float4 main(ShadowVertexInput input) : SV_POSITION
{
    return mul(input.position, gLightWorldViewProjection);
}
