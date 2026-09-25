struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

// Vertex Bufferを用意せず、SV_VertexIDだけで画面全体を覆う三角形を作る。
VertexShaderOutput main(uint vertexId : SV_VertexID)
{
    VertexShaderOutput output;
    float2 texcoord = float2(
        (vertexId << 1) & 2,
        vertexId & 2);
    output.position = float4(
        texcoord.x * 2.0f - 1.0f,
        1.0f - texcoord.y * 2.0f,
        0.0f,
        1.0f);
    output.texcoord = texcoord;
    return output;
}
