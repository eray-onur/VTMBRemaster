// Studio models (props). Lighting is a single per-instance tint for now,
// taken from the baked lightmap below the prop.

struct PushConstants {
    float4x4 mvp;
    float4 tint;      // rgb: light, a: alpha-test reference
    uint flags;
    float3 padding;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

static const uint kAlphaTest = 1;
static const uint kUnlit = 4;
static const uint kSelfIllum = 8;
static const uint kTranslucent = 16;

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

VSOutput vs_main(VSInput input) {
    VSOutput output;
    output.position = mul(pc.mvp, float4(input.position, 1.0));
    output.uv = input.uv;
    return output;
}

[[vk::binding(0, 0)]] SamplerState texture_sampler;
[[vk::binding(1, 0)]] Texture2D base_texture;

float4 ps_main(VSOutput input) : SV_Target {
    float4 base = base_texture.Sample(texture_sampler, input.uv);
    if (pc.flags & kAlphaTest) {
        clip(base.a - pc.tint.a);
    }
    float3 color = (pc.flags & kUnlit) ? base.rgb : base.rgb * pc.tint.rgb;
    if (pc.flags & kSelfIllum) {
        color = lerp(color, base.rgb, base.a);
    }
    return float4(color, (pc.flags & kTranslucent) ? base.a : 1.0);
}
