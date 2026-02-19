// ShaderW0.hlsl 소스 파일 맨위에 아래 상수버퍼 선언을 추가 하세요.
cbuffer constants : register(b0) // b0은 0번째 상수 버퍼라는 뜻
{
    float3 Offset;
    float Scale;
}

struct VS_INPUT
{
    float4 position : POSITION; // Input position from vertex buffer
    float4 color : COLOR; // Input color from vertex buffer
};

struct PS_INPUT
{
    float4 position : SV_POSITION; // Transformed position to pass to the pixel shader
    float4 color : COLOR; // Color to pass to the pixel shader
};

PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;

    // 스케일 적용하고 위치 오프셋 적용
    // 상수버퍼를 통해 넘겨 받은 Offset을 더해서 버텍스를 이동 시켜 픽셀쉐이더로 넘김
    float4 scaled = input.position * float4(Scale, Scale, Scale, 1.0);
    output.position = scaled + float4(Offset, 0);

    output.color = input.color;
    return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    // Output the color directly
    return input.color;
}
