// Ropes and cables (keyframe_rope, move_rope): a strip along the rope's
// curve, turned to face the camera about the rope's direction. Cable
// materials multiply the base texture by the light at the rope's nodes
// (encoded, clamped to $minlight-$maxlight) and by the normal map's facing,
// as the original's cable shader does (docs/RENDERING.md#ropes-and-cables).

#include "color.hlsli"

struct PushConstants {
    float4x4 view_projection;
    float4 eye;     // xyz: camera position.
    uint flags;
    float alpha_ref;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

static const uint kAlphaTest = 1;
static const uint kUnlit = 2;

// The fog (2 elements; see color.hlsli).
[[vk::binding(0, 1)]] StructuredBuffer<float4> fog_data;

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION; // On the rope's centre line.
    [[vk::location(1)]] float3 tangent : TANGENT;   // Along the rope.
    [[vk::location(2)]] float2 uv : TEXCOORD0;      // u: 0 or 1 across the rope; v: along it.
    [[vk::location(3)]] float3 light : COLOR0;      // Encoded light, already clamped.
    [[vk::location(4)]] float width : TEXCOORD1;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float3 light : COLOR0;
    [[vk::location(2)]] float2 depth_z : TEXCOORD1; // View depth, world z (for fog).
};

VSOutput vs_main(VSInput input) {
    const float3 t = normalize(input.tangent);
    float3 side = cross(t, pc.eye.xyz - input.position);
    if (dot(side, side) < 1e-6) { // Looking straight along the rope.
        side = cross(t, abs(t.z) < 0.9 ? float3(0, 0, 1) : float3(1, 0, 0));
    }
    side = normalize(side);
    const float3 world = input.position + side * (input.uv.x - 0.5) * input.width;
    VSOutput output;
    output.position = mul(pc.view_projection, float4(world, 1.0));
    output.uv = input.uv;
    output.light = input.light;
    output.depth_z = float2(output.position.w, world.z);
    return output;
}

[[vk::binding(0, 0)]] SamplerState texture_sampler;
[[vk::binding(1, 0)]] Texture2D base_texture;
[[vk::binding(2, 0)]] Texture2D bumpmap; // Raw values; white without one.

float4 ps_main(VSOutput input) : SV_Target {
    const float4 base = base_texture.Sample(texture_sampler, input.uv);
    if (pc.flags & kAlphaTest) {
        clip(base.a - pc.alpha_ref);
    }
    float3 color = to_linear(base.rgb);
    if (!(pc.flags & kUnlit)) {
        // The strip faces the camera, so the normal map's facing is its z.
        const float facing = saturate(bumpmap.Sample(texture_sampler, input.uv).b * 2.0 - 1.0);
        color *= pow(input.light * facing, 2.2); // Encoded light x facing, in linear terms.
    }
    const float fog = fog_amount(input.depth_z.x, input.depth_z.y, fog_data[0], fog_data[1]);
    return float4(fogged(to_gamma(color), fog_data[0], fog), 1.0);
}
