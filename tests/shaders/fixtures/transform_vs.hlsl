cbuffer Transform : register(b0)
{
    column_major float4x4 clipFromObject;
    float4 tint;
};

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(3)]] float4 color : COLOR0;
    [[vk::location(2)]] float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    output.position = mul(clipFromObject, float4(input.position, 1.0));
    output.color = input.color * tint;
    output.uv = input.uv;
    return output;
}
