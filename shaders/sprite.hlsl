// Sprites (env_sprite): one textured quad each, its axes built on the CPU from
// the material's orientation mode. Drawn with premultiplied blending, so one
// pipeline covers additive sprites (colour added, nothing masked) and
// alpha-blended ones (colour times alpha, background masked by alpha).

#include "color.hlsli"

struct PushConstants {
    float4x4 view_projection;
    float4 center; // World position of the sprite's origin point; w: 1 = additive, 0 = alpha-blended.
    float4 right;  // Full width along the quad, world units; w: the origin's u in the texture.
    float4 up;     // Full height (world up of the picture); w: the origin's v.
    float4 color;  // rgb: render colour (encoded 0-1); a: brightness or opacity (renderamt / 255, faded).
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

// The fog (2 elements; see color.hlsli).
[[vk::binding(0, 1)]] StructuredBuffer<float4> fog_data;

struct VSInput {
    [[vk::location(0)]] float3 corner : POSITION; // x, y in 0..1 (u, v of the picture).
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float2 depth_z : TEXCOORD1; // View depth, world z (for fog).
};

VSOutput vs_main(VSInput input) {
    const float2 uv = input.corner.xy;
    const float3 world = pc.center.xyz + pc.right.xyz * (uv.x - pc.right.w) - pc.up.xyz * (uv.y - pc.up.w);
    VSOutput output;
    output.position = mul(pc.view_projection, float4(world, 1.0));
    output.uv = uv;
    output.depth_z = float2(output.position.w, world.z);
    return output;
}

[[vk::binding(0, 0)]] SamplerState texture_sampler;
[[vk::binding(1, 0)]] Texture2D sprite;

float4 ps_main(VSOutput input) : SV_Target {
    const float4 t = sprite.Sample(texture_sampler, input.uv);
    const float fog = fog_amount(input.depth_z.x, input.depth_z.y, fog_data[0], fog_data[1]);
    const float a = t.a * pc.color.a;
    const float3 colour = linear_to_srgb(t.rgb) * pc.color.rgb;
    if (pc.center.w > 0.5) {
        // Additive (SRC_ALPHA, ONE in the original): fades out in fog.
        return float4(colour * a * (1.0 - fog), 0.0);
    }
    // Alpha-blended: towards the fog colour.
    return float4(fogged(colour, fog_data[0], fog) * a, a);
}
