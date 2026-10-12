// Skinned (animated) studio models. Per-draw shader data holds the model-space
// ambient cube (6 float4), the fog (2), one 3x4 skinning matrix per bone (3
// float4 rows each), then a material params row per mesh (8 float4: texture
// transform u and v rows, colour and alpha, self-illumination tint, ...;
// MaterialParams), then the optional blocks named in the push constants.

#include "color.hlsli"

struct PushConstants {
    float4x4 mvp;
    float alpha_ref;
    uint flags;
    uint params_row; // Element where this mesh's material params start.
    uint flex_base;  // Element of flex slot 0's offset (float4 per slot); 0 when no flex is active.
    float4 model_z;  // Model-to-world z row, for water fog.
    uint cloth_base;  // Element of cloth particle 0's position (float4 per particle); 0: no cloth.
    uint cloth_count; // Particles; their normals follow the positions.
    uint envmap_block; // Element of the eye (xyz), then the model-to-world rows (3); 0: none.
    uint lights_block; // Element of the two local lights (8 float4; local_light in color.hlsli); 0: none.
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

[[vk::binding(0, 1)]] StructuredBuffer<float4> shader_data;

static const uint kAlphaTest = 1;
static const uint kUnlit = 4;
static const uint kSelfIllum = 8;
static const uint kTranslucent = 16;
static const uint kEyes = 32;
static const uint kAdditive = 128;
static const uint kEnvmap = 256;
static const uint kEnvmapMaskShift = 9; // 2 bits, as for the world.
static const uint kBump = 2048;         // $bumpmap: light and reflect about the bumped normal.
static const uint kDetail = 8192;       // $detail, at the untransformed coordinates times $detailscale.
static const uint kShadowBuild = 16384; // The silhouette for the character's shadow: alpha 1 where covered.

// Iris size as a fraction of the eye mesh's UV square, measured on the
// original (the iris spans about 0.58 of the eye opening; docs/RENDERING.md).
static const float kIrisSize = 0.58;
static const uint kFogStart = 6;
static const uint kBoneBase = 8;

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float3 normal : NORMAL;
    [[vk::location(3)]] uint4 bones : BLENDINDICES;
    [[vk::location(4)]] float4 weights : BLENDWEIGHT;
    [[vk::location(5)]] float flex_slot : TEXCOORD1; // -1: no flex moves this vertex.
    // Cloth: ±(particle + 1), negative for the back of a double-sided card; 0: skinned.
    [[vk::location(6)]] float cloth : TEXCOORD2;
    [[vk::location(7)]] float4 tangent : TANGENT; // Texture u axis; w: binormal = cross(normal, tangent) * w.
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float3 light : COLOR0;
    [[vk::location(2)]] float2 depth_z : TEXCOORD1; // View depth, world z (for fog).
    [[vk::location(3)]] float3 world : TEXCOORD2;      // World position and normal (envmaps only).
    [[vk::location(4)]] float3 world_normal : NORMAL;
    [[vk::location(5)]] float3 model_normal : TEXCOORD3; // Skinned; for bump-mapped materials.
    [[vk::location(6)]] float4 model_tangent : TANGENT;
    [[vk::location(7)]] float3 model_position : TEXCOORD4; // Skinned; for bump-mapped materials.
    [[vk::location(8)]] float2 detail_uv : TEXCOORD5;
};

// The model-space ambient cube's light for a unit normal.
float3 cube_light(float3 n) {
    const float3 n2 = n * n;
    return n2.x * shader_data[n.x >= 0 ? 0 : 1].rgb + n2.y * shader_data[n.y >= 0 ? 2 : 3].rgb +
           n2.z * shader_data[n.z >= 0 ? 4 : 5].rgb;
}

// The ambient cube and the local lights at a model-space point, unit normal.
float3 model_light(float3 position, float3 n) {
    float3 light = cube_light(n);
    if (pc.lights_block != 0) {
        const uint b = pc.lights_block;
        [unroll] for (uint i = 0; i < 2; ++i) {
            light += local_light(shader_data[b + i * 4], shader_data[b + i * 4 + 1], shader_data[b + i * 4 + 2],
                                 shader_data[b + i * 4 + 3], position, n);
        }
    }
    return light;
}

// Model-space direction to world space (envmapped instances only).
float3 direction_to_world(float3 d) {
    return float3(dot(shader_data[pc.envmap_block + 1].xyz, d), dot(shader_data[pc.envmap_block + 2].xyz, d),
                  dot(shader_data[pc.envmap_block + 3].xyz, d));
}

float3 transform(uint bone, float4 v) {
    const uint row = kBoneBase + bone * 3;
    return float3(dot(shader_data[row], v), dot(shader_data[row + 1], v), dot(shader_data[row + 2], v));
}

VSOutput vs_main(VSInput input) {
    // Facial flexes move bind-pose positions before skinning.
    float3 bind_position = input.position;
    if (pc.flex_base != 0 && input.flex_slot >= 0) {
        bind_position += shader_data[pc.flex_base + (uint)input.flex_slot].xyz;
    }
    float3 position = float3(0, 0, 0);
    float3 normal = float3(0, 0, 0);
    float3 tangent = float3(0, 0, 0);
    [unroll] for (uint k = 0; k < 3; ++k) {
        const float w = input.weights[k];
        if (w > 0) {
            position += w * transform(input.bones[k], float4(bind_position, 1.0));
            normal += w * transform(input.bones[k], float4(input.normal, 0.0));
            tangent += w * transform(input.bones[k], float4(input.tangent.xyz, 0.0));
        }
    }
    // Simulated cloth replaces the skinned position (both in model space).
    if (pc.cloth_base != 0 && input.cloth != 0) {
        const uint particle = (uint)(abs(input.cloth) - 0.5);
        position = shader_data[pc.cloth_base + particle].xyz;
        const float3 n = shader_data[pc.cloth_base + pc.cloth_count + particle].xyz;
        if (dot(n, n) > 1e-6) {
            normal = n * sign(input.cloth);
        }
    }

    VSOutput output;
    output.position = mul(pc.mvp, float4(position, 1.0));
    output.depth_z = float2(output.position.w, dot(pc.model_z, float4(position, 1.0)));
    output.world = float3(0, 0, 0);
    output.world_normal = float3(0, 0, 1);
    if (pc.envmap_block != 0) {
        const float4 r0 = shader_data[pc.envmap_block + 1], r1 = shader_data[pc.envmap_block + 2],
                     r2 = shader_data[pc.envmap_block + 3];
        const float4 p = float4(position, 1.0);
        output.world = float3(dot(r0, p), dot(r1, p), dot(r2, p));
        output.world_normal = float3(dot(r0.xyz, normal), dot(r1.xyz, normal), dot(r2.xyz, normal));
    }
    output.model_normal = normal;
    output.model_tangent = float4(tangent, input.tangent.w);
    output.model_position = position;
    const float4 tu = shader_data[pc.params_row];
    const float4 tv = shader_data[pc.params_row + 1];
    output.uv = float2(dot(tu.xyz, float3(input.uv, 1.0)), dot(tv.xyz, float3(input.uv, 1.0)));
    output.detail_uv = input.uv * shader_data[pc.params_row + 7].y;
    output.light = model_light(position, normalize(normal));
    return output;
}

[[vk::binding(0, 0)]] SamplerState texture_sampler;
[[vk::binding(1, 0)]] Texture2D base_texture;
[[vk::binding(2, 0)]] Texture2D iris_texture; // Eyes only; the base texture otherwise.
[[vk::binding(3, 0)]] TextureCube envmap;
[[vk::binding(4, 0)]] Texture2D envmap_mask; // Raw values.
[[vk::binding(5, 0)]] Texture2D bumpmap;     // Tangent-space normal map, raw values.
[[vk::binding(6, 0)]] Texture2D detail;      // Raw values.

float4 ps_main(VSOutput input) : SV_Target {
    float4 base = base_texture.Sample(texture_sampler, input.uv);
    if (pc.flags & kEyes) {
        // The eye mesh's UVs are a front projection of the eyeball, centred on
        // where it looks; the iris disc sits in the middle.
        const float2 iris_uv = (input.uv - 0.5) / kIrisSize + 0.5;
        const float4 iris = iris_texture.Sample(texture_sampler, iris_uv);
        const float inside = all(iris_uv >= 0.0) && all(iris_uv <= 1.0) ? 1.0 : 0.0; // The sampler repeats.
        base.rgb = lerp(base.rgb, iris.rgb, iris.a * inside);
    }
    if (pc.flags & kAlphaTest) {
        clip(base.a - pc.alpha_ref);
    }
    if (pc.flags & kShadowBuild) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    float3 light = input.light;
    float4 bump = float4(0.0, 0.0, 1.0, 1.0);
    float3 bumped = float3(0.0, 0.0, 1.0);
    if (pc.flags & kBump) {
        bump = bumpmap.Sample(texture_sampler, input.uv);
        bumped = bumped_normal(bump.xyz * 2.0 - 1.0, input.model_normal, input.model_tangent);
        light = model_light(input.model_position, bumped);
    }
    const float4 tint = shader_data[pc.params_row + 2]; // $color, $alpha.
    float3 color = base.rgb * tint.rgb * ((pc.flags & kUnlit) ? float3(1, 1, 1) : light);
    if (pc.flags & kSelfIllum) {
        color = lerp(color, base.rgb * shader_data[pc.params_row + 3].rgb, base.a);
    }
    const float4 fog_color = shader_data[kFogStart];
    const float fog = fog_amount(input.depth_z.x, input.depth_z.y, fog_color, shader_data[kFogStart + 1]);
    float3 encoded = linear_to_srgb(color);
    if (pc.flags & kDetail) {
        encoded = apply_detail(encoded, detail.Sample(texture_sampler, input.detail_uv).rgb);
    }
    if ((pc.flags & kEnvmap) && pc.envmap_block != 0 && (pc.flags & kBump)) {
        // About the bumped normal, masked by the normal map's alpha before the
        // tint and contrast, as for bump-mapped world surfaces.
        const float3 env = envmap_reflection(direction_to_world(bumped), shader_data[pc.envmap_block].xyz - input.world,
                                             envmap, texture_sampler);
        encoded = saturate(encoded + envmap_term(env * bump.a, shader_data[pc.params_row + 6],
                                                 shader_data[pc.params_row + 7].x));
    } else if ((pc.flags & kEnvmap) && pc.envmap_block != 0) {
        encoded = add_envmap(encoded, input.world_normal, shader_data[pc.envmap_block].xyz - input.world, envmap,
                             envmap_mask, texture_sampler, input.uv, base.a, (pc.flags >> kEnvmapMaskShift) & 3,
                             shader_data[pc.params_row + 6], shader_data[pc.params_row + 7].x);
    }
    const float3 result = (pc.flags & kAdditive) ? encoded * (1.0 - fog) : fogged(encoded, fog_color, fog);
    return float4(result, (pc.flags & kTranslucent) ? base.a * tint.a : 1.0);
}
