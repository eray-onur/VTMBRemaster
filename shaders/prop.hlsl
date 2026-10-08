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
    uint params_index; // Row of the material params table; bits 16-31: the instance's envmap block.
    float4 model_z;    // Model-to-world z row (world z = dot(model_z, (position, 1))), for water fog.
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

static const uint kAlphaTest = 1;
static const uint kUnlit = 4;
static const uint kSelfIllum = 8;
static const uint kTranslucent = 16;
static const uint kMultiplyLayers = 64;
static const uint kAdditive = 128;
static const uint kEnvmap = 256;
static const uint kEnvmapMaskShift = 9; // 2 bits, as for the world.

// The fog (2 elements; see color.hlsli), the eye (xyz; w: element where the
// envmap blocks start), then the material params table: 8 elements per row
// (MaterialParams), then one envmap block per instance that needs one: its
// model-to-world rows (3 elements).
[[vk::binding(0, 1)]] StructuredBuffer<float4> material_params;
static const uint kEye = 2;
static const uint kParamsStart = 3;

float4 material_param(uint k) {
    return material_params[kParamsStart + (pc.params_index & 0xFFFF) * 8 + k];
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
    [[vk::location(3)]] float2 depth_z : TEXCOORD2; // View depth, world z (for fog).
    [[vk::location(4)]] float3 world : TEXCOORD3;      // World position and normal (envmaps only).
    [[vk::location(5)]] float3 world_normal : NORMAL;
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
    output.depth_z = float2(output.position.w, dot(pc.model_z, float4(input.position, 1.0)));
    output.world = float3(0, 0, 0);
    output.world_normal = float3(0, 0, 1);
    if (pc.flags & kEnvmap) {
        const uint block = (uint)material_params[kEye].w + (pc.params_index >> 16) * 3;
        const float4 r0 = material_params[block], r1 = material_params[block + 1], r2 = material_params[block + 2];
        const float4 p = float4(input.position, 1.0);
        output.world = float3(dot(r0, p), dot(r1, p), dot(r2, p));
        output.world_normal = float3(dot(r0.xyz, input.normal), dot(r1.xyz, input.normal), dot(r2.xyz, input.normal));
    }
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
[[vk::binding(3, 0)]] TextureCube envmap;
[[vk::binding(4, 0)]] Texture2D envmap_mask; // Raw values.

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
    const float fog = fog_amount(input.depth_z.x, input.depth_z.y, material_params[0], material_params[1]);
    float3 encoded = linear_to_srgb(color);
    if (pc.flags & kEnvmap) {
        encoded = add_envmap(encoded, input.world_normal, material_params[kEye].xyz - input.world, envmap, envmap_mask,
                             texture_sampler, input.uv, base.a, (pc.flags >> kEnvmapMaskShift) & 3, material_param(6),
                             material_param(7).x);
    }
    const float3 result = (pc.flags & kAdditive) ? encoded * (1.0 - fog) : fogged(encoded, material_params[0], fog);
    return float4(result, (pc.flags & kTranslucent) ? base.a * tint.a : 1.0);
}
