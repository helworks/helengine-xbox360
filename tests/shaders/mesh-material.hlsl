cbuffer Transform : register(b0) { float4x4 mvp; };
cbuffer Material : register(b0) { float4 baseColor; };
struct Input { float3 position : POSITION; };
struct Output { float4 position : SV_POSITION; };
Output VS(Input input) {
    Output output;
    output.position = mul(float4(input.position, 1.0), mvp);
    return output;
}
float4 PS(Output input) : SV_TARGET { return baseColor; }
