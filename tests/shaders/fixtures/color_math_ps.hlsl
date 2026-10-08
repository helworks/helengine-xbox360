cbuffer Material : register(b0)
{
    float4 factor;
    float4 bias;
};

struct PixelInput
{
    [[vk::location(0)]] float4 color : COLOR0;
};

float4 main(PixelInput input) : SV_Target0
{
    return input.color * factor + bias - bias;
}
