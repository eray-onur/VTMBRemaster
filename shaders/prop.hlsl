// Studio models (props), lit per vertex from a Source-style ambient cube:
// six colours for +X, -X, +Y, -Y, +Z, -Z in model space, blended by the
// squared components of the vertex normal.

#include "color.hlsli"

struct PushConstants {
    float4x4 mvp;
    uint4 cube_a;   // 18 half-floats: rgb for +X, -X, +Y, -Y, +Z, -Z ...
    uint4 cube_b;
    uint cube_c;
    float alpha_ref;
    uint flags;
    uint params_index; // Row of the material params table.
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

static const uint kAlphaTest = 1;
static const uint kUnlit = 4;
static const uint kSelfIllum = 8;
static const uint kTranslucent = 16;
static const uint kMultiplyLayers = 64;

// Material params table: 6 elements per row (texture transform u and v rows,
// colour and alpha, self-illumination tint, second-texture transform u and v rows).
[[vk::binding(0, 1)]] StructuredBuffer<float4> material_params;

float4 material_param(uint k) {
    return material_params[pc.params_index * 6 + k];
}

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float3 normal : NORMAL;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float3 light : COLOR0;
    [[vk::location(2)]] float2 uv2 : TEXCOORD1;
};

float cube_half(uint i) {
    uint words[9] = {pc.cube_a.x, pc.cube_a.y, pc.cube_a.z, pc.cube_a.w,
                     pc.cube_b.x, pc.cube_b.y, pc.cube_b.z, pc.cube_b.w, pc.cube_c};
    const uint w = words[i >> 1];
    return f16tof32((i & 1) ? (w >> 16) : (w & 0xFFFF));
}

float3 cube_color(uint face) {
    return float3(cube_half(face * 3), cube_half(face * 3 + 1), cube_half(face * 3 + 2));
}

VSOutput vs_main(VSInput input) {
    VSOutput output;
    output.position = mul(pc.mvp, float4(input.position, 1.0));
    const float4 tu = material_param(0);
    const float4 tv = material_param(1);
    output.uv = float2(dot(tu.xyz, float3(input.uv, 1.0)), dot(tv.xyz, float3(input.uv, 1.0)));
    output.uv2 = float2(dot(material_param(4).xyz, float3(input.uv, 1.0)), dot(material_param(5).xyz, float3(input.uv, 1.0)));
    const float3 n = normalize(input.normal);
    const float3 n2 = n * n;
    output.light = n2.x * cube_color(n.x >= 0 ? 0 : 1) +
                   n2.y * cube_color(n.y >= 0 ? 2 : 3) +
                   n2.z * cube_color(n.z >= 0 ? 4 : 5);
    return output;
}

[[vk::binding(0, 0)]] SamplerState texture_sampler;
[[vk::binding(1, 0)]] Texture2D base_texture;
[[vk::binding(2, 0)]] Texture2D base_texture2; // UnlitTwoTexture's second layer; the base otherwise.

float4 ps_main(VSOutput input) : SV_Target {
    float4 base = base_texture.Sample(texture_sampler, input.uv);
    if (pc.flags & kMultiplyLayers) {
        base *= base_texture2.Sample(texture_sampler, input.uv2);
    }
    if (pc.flags & kAlphaTest) {
        clip(base.a - pc.alpha_ref);
    }
    const float4 tint = material_param(2); // $color, $alpha.
    float3 color = base.rgb * tint.rgb * ((pc.flags & kUnlit) ? float3(1, 1, 1) : input.light);
    if (pc.flags & kSelfIllum) {
        color = lerp(color, base.rgb * material_param(3).rgb, base.a);
    }
    return float4(linear_to_srgb(color), (pc.flags & kTranslucent) ? base.a * tint.a : 1.0);
}
