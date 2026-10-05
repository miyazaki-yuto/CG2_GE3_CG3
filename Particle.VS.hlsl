cbuffer CameraConstants : register(b0)
{
    row_major float4x4 gViewProjection;
    float3 gCameraRight;
    float gRightPadding;
    float3 gCameraUp;
    float gUpPadding;
};

struct VertexShaderInput
{
    float2 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 instancePosition : POSITION1;
    float2 instanceSize : TEXCOORD1;
    float instanceRotation : TEXCOORD2;
    float4 instanceColor : COLOR0;
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    float sine = sin(input.instanceRotation);
    float cosine = cos(input.instanceRotation);
    float2 scaledPosition = input.position * input.instanceSize;
    float2 rotatedPosition = float2(
        scaledPosition.x * cosine - scaledPosition.y * sine,
        scaledPosition.x * sine + scaledPosition.y * cosine);
    float3 worldPosition = input.instancePosition +
        gCameraRight * rotatedPosition.x +
        gCameraUp * rotatedPosition.y;
    output.position = mul(float4(worldPosition, 1.0f), gViewProjection);
    output.texcoord = input.texcoord;
    output.color = input.instanceColor;
    return output;
}
