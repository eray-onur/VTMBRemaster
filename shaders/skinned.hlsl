// Skinned (animated) studio models. Per-draw shader data holds the model-space
// ambient cube (6 float4), one 3x4 skinning matrix per bone (3 float4 rows
// each), then a material params row per mesh (texture transform u and v rows,
// colour and alpha, self-illumination tint).

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
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

[[vk::binding(0, 1)]] StructuredBuffer<float4> shader_data;

static const uint kAlphaTest = 1;
static const uint kUnlit = 4;
static const uint kSelfIllum = 8;
static const uint kTranslucent = 16;
static const uint kEyes = 32;
static const uint kAdditive = 128;

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
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float3 light : COLOR0;
    [[vk::location(2)]] float2 depth_z : TEXCOORD1; // View depth, world z (for fog).
};

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
    [unroll] for (uint k = 0; k < 3; ++k) {
        const float w = input.weights[k];
        if (w > 0) {
            position += w * transform(input.bones[k], float4(bind_position, 1.0));
            normal += w * transform(input.bones[k], float4(input.normal, 0.0));
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
    const float4 tu = shader_data[pc.params_row];
    const float4 tv = shader_data[pc.params_row + 1];
    output.uv = float2(dot(tu.xyz, float3(input.uv, 1.0)), dot(tv.xyz, float3(input.uv, 1.0)));
    const float3 n = normalize(normal);
    const float3 n2 = n * n;
    output.light = n2.x * shader_data[n.x >= 0 ? 0 : 1].rgb +
                   n2.y * shader_data[n.y >= 0 ? 2 : 3].rgb +
                   n2.z * shader_data[n.z >= 0 ? 4 : 5].rgb;
    return output;
}

[[vk::binding(0, 0)]] SamplerState texture_sampler;
[[vk::binding(1, 0)]] Texture2D base_texture;
[[vk::binding(2, 0)]] Texture2D iris_texture; // Eyes only; the base texture otherwise.

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
    const float4 tint = shader_data[pc.params_row + 2]; // $color, $alpha.
    float3 color = base.rgb * tint.rgb * ((pc.flags & kUnlit) ? float3(1, 1, 1) : input.light);
    if (pc.flags & kSelfIllum) {
        color = lerp(color, base.rgb * shader_data[pc.params_row + 3].rgb, base.a);
    }
    const float4 fog_color = shader_data[kFogStart];
    const float fog = fog_amount(input.depth_z.x, input.depth_z.y, fog_color, shader_data[kFogStart + 1]);
    const float3 encoded = linear_to_srgb(color);
    const float3 result = (pc.flags & kAdditive) ? encoded * (1.0 - fog) : fogged(encoded, fog_color, fog);
    return float4(result, (pc.flags & kTranslucent) ? base.a * tint.a : 1.0);
}
