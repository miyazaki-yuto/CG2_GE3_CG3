struct PixelShaderInput
{
    float4 position : SV_POSITION;
};

float4 main(PixelShaderInput input) : SV_TARGET
{
    // HDRへ1.0を超えるオレンジを出し、Bloom有効時には柔らかく発光させる。
    return float4(4.0f, 0.55f, 0.02f, 1.0f) + input.position.x * 0.0f;
}
