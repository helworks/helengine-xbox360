Texture2D<float4> texture0 : register(t0);
SamplerState sampler0 : register(s0);

struct PixelInput
{
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
};

float4 main(PixelInput input) : SV_Target0
{
    float2 sampleUv = input.uv * input.uv;
    return texture0.Sample(sampler0, sampleUv) * input.color;
}
