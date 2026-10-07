// BSP world surfaces: base texture(s) modulated by the baked lightmap.

#include "color.hlsli"

struct PushConstants {
    float4x4 mvp;
    float alpha_ref;
    float light_scale;
    uint flags;
    uint params_index; // Row of the material params table (after the light styles).
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

static const uint kAlphaTest = 1;
static const uint kBlendLayers = 2;
static const uint kUnlit = 4;
static const uint kSelfIllum = 8;
static const uint kTranslucent = 16;
static const uint kModulate = 32;
static const uint kMultiplyLayers = 64;
static const uint kAdditive = 128;

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float2 lightmap_uv : TEXCOORD1;
    [[vk::location(3)]] float blend : TEXCOORD2;
    [[vk::location(4)]] uint4 styles : TEXCOORD3;
    [[vk::location(5)]] float style_step : TEXCOORD4;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float2 lightmap_uv : TEXCOORD1;
    [[vk::location(2)]] float blend : TEXCOORD2;
    [[vk::location(3)]] nointerpolation uint4 styles : TEXCOORD3;
    [[vk::location(4)]] nointerpolation float style_step : TEXCOORD4;
    [[vk::location(5)]] float2 uv2 : TEXCOORD5;
    [[vk::location(6)]] float view_depth : TEXCOORD6;
};

// Brightness of each of the 64 light styles (4 per element), the fog (2
// elements; see color.hlsli), then the material params table: 6 elements per
// row (texture transform u and v rows, colour and alpha, self-illumination
// tint, second-texture transform u and v rows).
[[vk::binding(0, 1)]] StructuredBuffer<float4> style_values;
static const uint kFogStart = 16;
static const uint kParamsStart = 18;

float style_value(uint style) {
    return style_values[style >> 2][style & 3];
}

float4 material_param(uint k) {
    return style_values[kParamsStart + pc.params_index * 6 + k];
}

VSOutput vs_main(VSInput input) {
    VSOutput output;
    output.position = mul(pc.mvp, float4(input.position, 1.0));
    const float4 tu = material_param(0);
    const float4 tv = material_param(1);
    output.uv = float2(dot(tu.xyz, float3(input.uv, 1.0)), dot(tv.xyz, float3(input.uv, 1.0)));
    output.uv2 = float2(dot(material_param(4).xyz, float3(input.uv, 1.0)), dot(material_param(5).xyz, float3(input.uv, 1.0)));
    output.lightmap_uv = input.lightmap_uv;
    output.view_depth = output.position.w;
    output.blend = input.blend;
    output.styles = input.styles;
    output.style_step = input.style_step;
    return output;
}

[[vk::binding(0, 0)]] SamplerState texture_sampler;
[[vk::binding(1, 0)]] Texture2D base_texture;
[[vk::binding(2, 0)]] Texture2D base_texture2;
[[vk::binding(3, 0)]] Texture2D lightmap;

float4 ps_main(VSOutput input) : SV_Target {
    float4 base = base_texture.Sample(texture_sampler, input.uv);
    if (pc.flags & kBlendLayers) {
        base = lerp(base, base_texture2.Sample(texture_sampler, input.uv), input.blend);
    }
    if (pc.flags & kMultiplyLayers) {
        base *= base_texture2.Sample(texture_sampler, input.uv2);
    }
    if (pc.flags & kAlphaTest) {
        clip(base.a - pc.alpha_ref);
    }
    const float4 fog_color = style_values[kFogStart];
    const float fog = fog_amount(input.view_depth, fog_color, style_values[kFogStart + 1]);
    if (pc.flags & kModulate) {
        // The blend computes 2 * src * dst on encoded values: the original's
        // "2 x texture" in gamma space, exactly. Fog fades it to neutral (0.5).
        return float4(lerp(linear_to_srgb(base.rgb), float3(0.5, 0.5, 0.5), fog), 1.0);
    }
    float3 light = float3(1.0, 1.0, 1.0);
    if (!(pc.flags & kUnlit)) {
        light = float3(0, 0, 0);
        [unroll] for (uint k = 0; k < 4; ++k) {
            const uint style = input.styles[k];
            if (style < 64) {
                const float2 uv = input.lightmap_uv + float2(input.style_step * k, 0);
                light += lightmap.Sample(texture_sampler, uv).rgb * style_value(style);
            }
        }
        // The original stores the combined lightmap in an 8-bit texture as
        // luxel^(1/2.2) / 2 and doubles it in the shader, so anything above
        // 2^2.2 saturates (docs/RENDERING.md).
        light = min(light, 4.5948) * pc.light_scale;
    }
    const float4 tint = material_param(2); // $color, $alpha.
    float3 color = base.rgb * tint.rgb * light;
    if (pc.flags & kSelfIllum) {
        color = lerp(color, base.rgb * material_param(3).rgb, base.a);
    }
    const float3 encoded = linear_to_srgb(color);
    // Additive surfaces fade out in fog; the rest blend towards its colour.
    const float3 result = (pc.flags & kAdditive) ? encoded * (1.0 - fog) : fogged(encoded, fog_color, fog);
    return float4(result, (pc.flags & kTranslucent) ? base.a * tint.a : 1.0);
}
