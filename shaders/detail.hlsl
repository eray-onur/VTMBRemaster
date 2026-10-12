// Detail props: world-space vertices baked when the map loads, each carrying
// its prop's light colour (linear, lightmap units). Lit colour = base x light,
// whether the material is lit or unlit (docs/RENDERING.md#detail-props).

#include "color.hlsli"

struct PushConstants {
    float4x4 view_projection;
    uint flags;
    float alpha_ref;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

static const uint kAlphaTest = 1;
static const uint kTranslucent = 2;

// The fog (2 elements; see color.hlsli).
[[vk::binding(0, 1)]] StructuredBuffer<float4> fog_data;

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float3 light : COLOR0;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float3 light : COLOR0;
    [[vk::location(2)]] float2 depth_z : TEXCOORD1; // View depth, world z (for fog).
};

VSOutput vs_main(VSInput input) {
    VSOutput output;
    output.position = mul(pc.view_projection, float4(input.position, 1.0));
    output.uv = input.uv;
    output.light = input.light;
    output.depth_z = float2(output.position.w, input.position.z);
    return output;
}

[[vk::binding(0, 0)]] SamplerState texture_sampler;
[[vk::binding(1, 0)]] Texture2D base_texture;

float4 ps_main(VSOutput input) : SV_Target {
    const float4 base = base_texture.Sample(texture_sampler, input.uv);
    if (pc.flags & kAlphaTest) {
        clip(base.a - pc.alpha_ref);
    }
    const float fog = fog_amount(input.depth_z.x, input.depth_z.y, fog_data[0], fog_data[1]);
    const float3 color = fogged(linear_to_srgb(base.rgb * input.light), fog_data[0], fog);
    return float4(color, (pc.flags & kTranslucent) ? base.a : 1.0);
}
