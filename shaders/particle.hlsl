// Particles: camera-facing quads built from per-particle shader data (4 float4
// per particle: position and width; height, roll and align flag; additive
// colour and mask; velocity). Drawn with premultiplied blending, so a particle
// both adds light (colour) and darkens what is behind it (mask).

#include "color.hlsli"

struct PushConstants {
    float4x4 view_projection;
    float4 right; // Camera right, world space; w: fog start.
    float4 up;    // Camera up, world space; w: fog end.
    float4 eye;   // Camera position; w: water z (water fog).
    float4 fog;   // rgb: fog colour (encoded), a: mode (color.hlsli).
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

[[vk::binding(0, 1)]] StructuredBuffer<float4> particles;

struct VSInput {
    [[vk::location(0)]] float3 corner : POSITION; // x, y in -1..1; z = particle index in this draw.
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] nointerpolation float4 color : COLOR0; // rgb additive, a mask.
    [[vk::location(2)]] float2 depth_z : TEXCOORD1; // View depth, world z (for fog).
};

VSOutput vs_main(VSInput input) {
    const uint i = (uint)input.corner.z * 4;
    const float4 d0 = particles[i], d1 = particles[i + 1], d2 = particles[i + 2], d3 = particles[i + 3];
    const float3 center = d0.xyz;
    const float half_w = d0.w * 0.5, half_h = d1.x * 0.5;
    float3 world;
    if (d1.z > 0.5 && dot(d3.xyz, d3.xyz) > 1e-6) {
        // movealign: the quad's height runs along the motion, its width across the view.
        const float3 axis = normalize(d3.xyz);
        float3 side = cross(axis, normalize(pc.eye.xyz - center));
        side = dot(side, side) > 1e-8 ? normalize(side) : pc.right.xyz;
        world = center + side * (input.corner.x * half_w) + axis * (input.corner.y * half_h);
    } else {
        const float s = sin(radians(d1.y)), c = cos(radians(d1.y));
        const float2 r = float2(c * input.corner.x - s * input.corner.y, s * input.corner.x + c * input.corner.y);
        world = center + pc.right.xyz * (r.x * half_w) + pc.up.xyz * (r.y * half_h);
    }
    VSOutput output;
    output.position = mul(pc.view_projection, float4(world, 1.0));
    output.uv = float2(input.corner.x * 0.5 + 0.5, 0.5 - input.corner.y * 0.5);
    output.color = d2;
    output.depth_z = float2(output.position.w, world.z);
    return output;
}

[[vk::binding(0, 0)]] SamplerState texture_sampler;
[[vk::binding(1, 0)]] Texture2D sprite;

float4 ps_main(VSOutput input) : SV_Target {
    const float4 t = sprite.Sample(texture_sampler, input.uv);
    // The sprite's colour (already dark where it is transparent) adds light,
    // scaled on encoded values as in the original; its alpha only drives the
    // masking of the background (particles/000_template.txt).
    // In fog the added light fades out and the masking turns towards the fog colour.
    const float fog = fog_amount(input.depth_z.x, input.depth_z.y, pc.fog, float4(pc.right.w, pc.up.w, pc.eye.w, pc.eye.z));
    const float mask = t.a * input.color.a;
    const float3 added = t.rgb * input.color.rgb * (1.0 - fog);
    return float4(added + pc.fog.rgb * mask * fog, mask);
}
