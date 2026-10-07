// Unlit textured geometry. Compiled to SPIR-V by glslang (and to DXIL by DXC
// once the Direct3D 12 backend exists).

#include "color.hlsli"

struct PushConstants {
    float4x4 mvp;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

VSOutput vs_main(VSInput input) {
    VSOutput output;
    output.position = mul(pc.mvp, float4(input.position, 1.0));
    output.uv = input.uv;
    return output;
}

[[vk::binding(0, 0)]] SamplerState texture_sampler;
[[vk::binding(1, 0)]] Texture2D base_texture;

float4 ps_main(VSOutput input) : SV_Target {
    const float4 c = base_texture.Sample(texture_sampler, input.uv);
    return float4(linear_to_srgb(c.rgb), c.a);
}
