
// 頂点シェーダーへの入力
struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0; 
};

// 頂点シェーダーからの出力（ピクセルシェーダーへの入力）
struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0; 
};