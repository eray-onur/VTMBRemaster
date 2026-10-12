// Studio models (props), lit per vertex from a Source-style ambient cube:
// six colours for +X, -X, +Y, -Y, +Z, -Z in model space, blended by the
// squared components of the vertex normal, plus up to two local lights
// (local_light in color.hlsli). With a $bumpmap both are evaluated per pixel
// with the bumped normal instead.

#include "color.hlsli"

struct PushConstants {
    float4x4 mvp;
    uint4 cube_a;   // 18 half-floats: rgb for +X, -X, +Y, -Y, +Z, -Z (with kLights: without the local lights) ...
    uint4 cube_b;
    uint cube_c;
    float alpha_ref;
    uint flags;
    uint params_index; // Row of the material params table; bits 16-31: the instance's block.
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
static const uint kBump = 2048;         // $bumpmap: light and reflect about the bumped normal.
static const uint kDetail = 8192;       // $detail, at the untransformed coordinates times $detailscale.
static const uint kLights = 4096;       // The instance's block holds its local lights.
static const uint kFadeShift = 24;      // 8 bits: 255 - the static prop fade's opacity.

// The fog (2 elements; see color.hlsli), the eye (xyz; w: element where the
// instance blocks start), then the material params table: 8 elements per row
// (MaterialParams), then one block per instance: its local lights (8
// elements), then its model-to-world rows (3 elements).
[[vk::binding(0, 1)]] StructuredBuffer<float4> material_params;
static const uint kEye = 2;
static const uint kParamsStart = 3;
static const uint kBlockSize = 11;

float4 material_param(uint k) {
    return material_params[kParamsStart + (pc.params_index & 0xFFFF) * 8 + k];
}

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float3 normal : NORMAL;
    [[vk::location(3)]] float4 tangent : TANGENT; // Texture u axis; w: binormal = cross(normal, tangent) * w.
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float3 light : COLOR0;
    [[vk::location(2)]] float2 uv2 : TEXCOORD1;
    [[vk::location(3)]] float2 depth_z : TEXCOORD2; // View depth, world z (for fog).
    [[vk::location(4)]] float3 world : TEXCOORD3;      // World position and normal (envmaps only).
    [[vk::location(5)]] float3 world_normal : NORMAL;
    [[vk::location(6)]] float3 model_normal : TEXCOORD4; // For bump-mapped materials.
    [[vk::location(7)]] float4 model_tangent : TANGENT;
    [[vk::location(8)]] float3 model_position : TEXCOORD5; // For bump-mapped materials.
    [[vk::location(9)]] float2 detail_uv : TEXCOORD6;
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

// The ambient cube's light for a unit model-space normal.
float3 cube_light(float3 n) {
    const float3 n2 = n * n;
    return n2.x * cube_color(n.x >= 0 ? 0 : 1) + n2.y * cube_color(n.y >= 0 ? 2 : 3) + n2.z * cube_color(n.z >= 0 ? 4 : 5);
}

uint instance_block() {
    return (uint)material_params[kEye].w + (pc.params_index >> 16) * kBlockSize;
}

// The instance's model-to-world rows (envmapped instances only).
float3x4 model_to_world() {
    const uint block = instance_block() + 8;
    return float3x4(material_params[block], material_params[block + 1], material_params[block + 2]);
}

// The ambient cube and the local lights at a model-space point, unit normal.
float3 model_light(float3 position, float3 n) {
    float3 light = cube_light(n);
    if (pc.flags & kLights) {
        const uint b = instance_block();
        [unroll] for (uint i = 0; i < 2; ++i) {
            light += local_light(material_params[b + i * 4], material_params[b + i * 4 + 1],
                                 material_params[b + i * 4 + 2], material_params[b + i * 4 + 3], position, n);
        }
    }
    return light;
}

VSOutput vs_main(VSInput input) {
    VSOutput output;
    output.position = mul(pc.mvp, float4(input.position, 1.0));
    output.depth_z = float2(output.position.w, dot(pc.model_z, float4(input.position, 1.0)));
    output.world = float3(0, 0, 0);
    output.world_normal = float3(0, 0, 1);
    if (pc.flags & kEnvmap) {
        const float3x4 m = model_to_world();
        output.world = mul(m, float4(input.position, 1.0));
        output.world_normal = mul(m, float4(input.normal, 0.0));
    }
    output.model_normal = input.normal;
    output.model_tangent = input.tangent;
    output.model_position = input.position;
    const float4 tu = material_param(0);
    const float4 tv = material_param(1);
    output.uv = float2(dot(tu.xyz, float3(input.uv, 1.0)), dot(tv.xyz, float3(input.uv, 1.0)));
    output.detail_uv = input.uv * material_param(7).y;
    output.uv2 = float2(dot(material_param(4).xyz, float3(input.uv, 1.0)), dot(material_param(5).xyz, float3(input.uv, 1.0)));
    output.light = model_light(input.position, normalize(input.normal));
    return output;
}

[[vk::binding(0, 0)]] SamplerState texture_sampler;
[[vk::binding(1, 0)]] Texture2D base_texture;
[[vk::binding(2, 0)]] Texture2D base_texture2; // UnlitTwoTexture's second layer; the base otherwise.
[[vk::binding(3, 0)]] TextureCube envmap;
[[vk::binding(4, 0)]] Texture2D envmap_mask; // Raw values.
[[vk::binding(5, 0)]] Texture2D bumpmap;     // Tangent-space normal map, raw values.
[[vk::binding(6, 0)]] Texture2D detail;      // Raw values.

float4 ps_main(VSOutput input) : SV_Target {
    float4 base = base_texture.Sample(texture_sampler, input.uv);
    if (pc.flags & kMultiplyLayers) {
        base *= base_texture2.Sample(texture_sampler, input.uv2);
    }
    if (pc.flags & kAlphaTest) {
        clip(base.a - pc.alpha_ref);
    }
    float3 light = input.light;
    float4 bump = float4(0.0, 0.0, 1.0, 1.0);
    float3 bumped = float3(0.0, 0.0, 1.0);
    if (pc.flags & kBump) {
        bump = bumpmap.Sample(texture_sampler, input.uv);
        bumped = bumped_normal(bump.xyz * 2.0 - 1.0, input.model_normal, input.model_tangent);
        light = model_light(input.model_position, bumped);
    }
    const float4 tint = material_param(2); // $color, $alpha.
    const float3 albedo = to_linear(base.rgb);
    float3 color = albedo * tint.rgb * ((pc.flags & kUnlit) ? float3(1, 1, 1) : light);
    if (pc.flags & kSelfIllum) {
        color = lerp(color, albedo * material_param(3).rgb, base.a);
    }
    const float fog = fog_amount(input.depth_z.x, input.depth_z.y, material_params[0], material_params[1]);
    float3 encoded = to_gamma(color);
    if (pc.flags & kDetail) {
        encoded = apply_detail(encoded, detail.Sample(texture_sampler, input.detail_uv).rgb);
    }
    if ((pc.flags & kEnvmap) && (pc.flags & kBump)) {
        // About the bumped normal, masked by the normal map's alpha before the
        // tint and contrast, as for bump-mapped world surfaces.
        const float3 normal = mul(model_to_world(), float4(bumped, 0.0));
        const float3 env = envmap_reflection(normal, material_params[kEye].xyz - input.world, envmap, texture_sampler);
        encoded = saturate(encoded + envmap_term(env * bump.a, material_param(6), material_param(7).x));
    } else if (pc.flags & kEnvmap) {
        encoded = add_envmap(encoded, input.world_normal, material_params[kEye].xyz - input.world, envmap, envmap_mask,
                             texture_sampler, input.uv, base.a, (pc.flags >> kEnvmapMaskShift) & 3, material_param(6),
                             material_param(7).x);
    }
    const float3 result = (pc.flags & kAdditive) ? encoded * (1.0 - fog) : fogged(encoded, material_params[0], fog);
    const float fade = 1.0 - (float)(pc.flags >> kFadeShift) / 255.0;
    return float4(result, ((pc.flags & kTranslucent) ? base.a * tint.a : 1.0) * fade);
}
