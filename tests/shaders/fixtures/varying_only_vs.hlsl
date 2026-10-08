struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
};

struct VertexOutput
{
    [[vk::location(0)]] float4 color : COLOR0;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    output.color = float4(input.position, 1.0);
    return output;
}
