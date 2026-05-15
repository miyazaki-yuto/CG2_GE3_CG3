struct Material
{
    float4 color;
};

// 資料通りのConstantBufferを使ったやり方はエラーが出たので少し古いやり方で
cbuffer gMaterial : register(b0)
{
    Material gMaterialData;
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main()
{
    PixelShaderOutput output;
    
    output.color = gMaterialData.color;
    
    return output;
};