struct PixelInput
{
    [[vk::location(0)]] nointerpolation float4 color : COLOR0;
};

float4 main(PixelInput input) : SV_Target0
{
    return input.color;
}
