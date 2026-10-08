struct PixelInput
{
    [[vk::location(0)]] float4 color : COLOR0;
};

cbuffer Material : register(b0)
{
    float4 divisor;
};

float4 main(PixelInput input) : SV_Target0
{
    return input.color / divisor;
}
