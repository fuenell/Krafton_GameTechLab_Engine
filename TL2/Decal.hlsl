cbuffer ModelBuffer : register(b0)
{
    row_major float4x4 WorldMatrix;
}

cbuffer ViewProjBuffer : register(b1)
{
    row_major float4x4 ViewMatrix;
    row_major float4x4 ProjectionMatrix;
}

cbuffer HighLightBuffer : register(b2)
{
    int Picked;
    float3 Color;
    int X;
    int Y;
    int Z;
    int GIzmo;
}

struct VS_INPUT
{
    float3 position : POSITION; // Input position from vertex buffer
    float3 normal : NORMAL0;
    float4 color : COLOR; // Input color from vertex buffer
    float2 texCoord : TEXCOORD0;
};


Texture2D g_DiffuseTexColor : register(t0);
SamplerState g_Sample : register(s0);

struct FMaterial
{
    float3 DiffuseColor; // Kd
    float OpticalDensity; // Ni
    
    float3 AmbientColor; // Ka
    float Transparency; // Tr Or d
    
    float3 SpecularColor; // Ks
    float SpecularExponent; // Ns
    
    float3 EmissiveColor; // Ke
    uint IlluminationModel; // illum. Default illumination model to Phong for non-Pbr materials
    
    float3 TransmissionFilter; // Tf
    float dummy;
};

cbuffer ColorBuffer : register(b3)
{
    float4 LerpColor;
}

cbuffer PixelConstData : register(b4)
{
    FMaterial Material;
    bool HasMaterial; // 4 bytes
    bool HasTexture;
}
cbuffer PSScrollCB : register(b5)
{
    float2 UVScrollSpeed;
    float UVScrollTime;
    float _pad_scrollcb;
}

cbuffer DecalViewProjBuffer : register(b6)
{
    row_major float4x4 DecalViewProjectionMatrix;
}

struct PS_INPUT
{
    float4 position : SV_POSITION; // Transformed position to pass to the pixel shader
    float3 normal : NORMAL0;
    float4 color : COLOR; // Color to pass to the pixel shader
    float2 texCoord : TEXCOORD0;
    float3 decalPos : TEXCOORD1;
};

PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;
    
    // 상수버퍼를 통해 넘겨 받은 Offset을 더해서 버텍스를 이동 시켜 픽셀쉐이더로 넘김
    // float3 scaledPosition = input.position.xyz * Scale;
    // output.position = float4(Offset + scaledPosition, 1.0);
    
    float4x4 MVP = mul(mul(WorldMatrix, ViewMatrix), ProjectionMatrix);
    output.position = mul(float4(input.position, 1.0f), MVP);
    
    float4x4 DecalMVP = mul(WorldMatrix, DecalViewProjectionMatrix);
    output.decalPos = mul(float4(input.position, 1.0f), DecalMVP);
    
    // change color
    float4 c = input.color;

    // Pass the color to the pixel shader
    output.color = c;
    
    output.normal = input.normal;
    output.texCoord = input.texCoord;
    
    return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    if (input.decalPos.x < -1 || 1 < input.decalPos.x)
    {
        discard;
    }
    
    if (input.decalPos.y < -1 || 1 < input.decalPos.y)
    {
        discard;
    }
    
    if (input.decalPos.z < -1 || 1 < input.decalPos.z)
    {
        discard;
    }
    
    float4 pixel = g_DiffuseTexColor.Sample(g_Sample, (input.decalPos.xy + 1) * 0.5f);
    return pixel;
}

