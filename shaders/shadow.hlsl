// Character shadows projected onto the world (docs/RENDERING.md#character-shadows).
// The receivers are world triangles near the character; each pixel looks up
// the character's silhouette (drawn from the light into a render target),
// softened, faded along the projection, and darkens what is behind it by the
// shadow colour. The blend multiplies by 2 x the output, on encoded values,
// as the original's Shadow shader does (lerp(1, shadow colour, alpha)).

#include "color.hlsli"

struct PushConstants {
    float4x4 mvp;
    float4 shadow_u;   // Silhouette clip x = dot(shadow_u, (world, 1)).
    float4 shadow_v;   // Silhouette clip y.
    float4 along;      // Distance along the projection from the caster's centre = dot(along, (world, 1)).
    float4 extent;     // x: the caster's half-depth along the projection; y: fade distance beyond it; z: texel size.
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

// The fog (2 elements; see color.hlsli).
[[vk::binding(0, 1)]] StructuredBuffer<float4> shader_data;

// Measured in the original (sm_hub_1, the barrel fire): a shadow's core keeps
// 0.28 / 0.37 / 0.41 of the encoded colour.
static const float3 kShadowColor = float3(0.285, 0.375, 0.415);
// Shadow_DX8's $falloffamount: the shadow is this much lighter at its far end.
static const float kFalloff = 0.9;

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 world : TEXCOORD0;
    [[vk::location(1)]] float depth : TEXCOORD1;
};

VSOutput vs_main(VSInput input) {
    VSOutput output;
    output.position = mul(pc.mvp, float4(input.position, 1.0));
    output.world = input.position;
    output.depth = output.position.w;
    return output;
}

[[vk::binding(0, 0)]] SamplerState shadow_sampler; // Clamped.
[[vk::binding(1, 0)]] Texture2D silhouette;       // Alpha: covered by the character.

float4 ps_main(VSOutput input) : SV_Target {
    const float4 p = float4(input.world, 1.0);
    const float2 uv = float2(dot(pc.shadow_u, p), dot(pc.shadow_v, p)) * 0.5 + 0.5;
    const float s = dot(pc.along, p);
    if (any(uv < 0.0) || any(uv > 1.0) || s < -pc.extent.x) {
        discard; // Outside the silhouette's square, or nearer the light than the caster.
    }
    // A 3 x 3 box of taps, 1.5 texels apart, for the soft edge.
    float coverage = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y) {
        [unroll] for (int x = -1; x <= 1; ++x) {
            coverage += silhouette.SampleLevel(shadow_sampler, uv + float2(x, y) * pc.extent.z * 1.5, 0).a;
        }
    }
    coverage /= 9.0;
    const float t = saturate((s - pc.extent.x) / pc.extent.y);
    const float alpha = coverage * (1.0 - kFalloff * t) * (s > pc.extent.x + pc.extent.y ? 0.0 : 1.0);
    // 0.5 is neutral for the x2 modulate blend; fog fades the shadow to it.
    const float fog = fog_amount(input.depth, input.world.z, shader_data[0], shader_data[1]);
    const float3 factor = lerp(float3(1.0, 1.0, 1.0), kShadowColor, alpha);
    return float4(lerp(factor * 0.5, float3(0.5, 0.5, 0.5), fog), 1.0);
}
