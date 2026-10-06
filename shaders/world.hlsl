// BSP world surfaces: base texture(s) modulated by the baked lightmap.

struct PushConstants {
    float4x4 mvp;
    float alpha_ref;
    float light_scale;
    uint flags;
    float padding;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

static const uint kAlphaTest = 1;
static const uint kBlendLayers = 2;
static const uint kUnlit = 4;
static const uint kSelfIllum = 8;
static const uint kTranslucent = 16;

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float2 lightmap_uv : TEXCOORD1;
    [[vk::location(3)]] float blend : TEXCOORD2;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float2 lightmap_uv : TEXCOORD1;
    [[vk::location(2)]] float blend : TEXCOORD2;
};

VSOutput vs_main(VSInput input) {
    VSOutput output;
    output.position = mul(pc.mvp, float4(input.position, 1.0));
    output.uv = input.uv;
    output.lightmap_uv = input.lightmap_uv;
    output.blend = input.blend;
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
    if (pc.flags & kAlphaTest) {
        clip(base.a - pc.alpha_ref);
    }
    float3 light = float3(1.0, 1.0, 1.0);
    if (!(pc.flags & kUnlit)) {
        light = lightmap.Sample(texture_sampler, input.lightmap_uv).rgb * pc.light_scale;
    }
    float3 color = base.rgb * light;
    if (pc.flags & kSelfIllum) {
        color = lerp(color, base.rgb, base.a);
    }
    return float4(color, (pc.flags & kTranslucent) ? base.a : 1.0);
}
