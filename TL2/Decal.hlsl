cbuffer ModelBuffer : register(b0)
{
    row_major float4x4 WorldMatrix;
}

cbuffer ViewProjBuffer : register(b1)
{
    row_major float4x4 ViewMatrix;
    row_major float4x4 ProjectionMatrix;
}

cbuffer DecalViewProjBuffer : register(b6)
{
    row_major float4x4 DecalViewProjectionMatrix;
}

Texture2D g_DiffuseTexColor : register(t0);
SamplerState g_Sample : register(s0);

struct VS_INPUT
{
    float3 position : POSITION; // Input position from vertex buffer
    float3 normal : NORMAL0;
    float4 color : COLOR; // Input color from vertex buffer
    float2 texCoord : TEXCOORD0;
};

struct PS_INPUT
{
    float4 position : SV_POSITION; // Transformed position to pass to the pixel shader
    float3 decalPos : TEXCOORD1;
};

PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;
    
    float4x4 MVP = mul(mul(WorldMatrix, ViewMatrix), ProjectionMatrix);
    output.position = mul(float4(input.position, 1.0f), MVP);
    
    float4x4 DecalMVP = mul(WorldMatrix, DecalViewProjectionMatrix);
    output.decalPos = mul(float4(input.position, 1.0f), DecalMVP);
    
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

