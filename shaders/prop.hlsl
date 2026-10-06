// Studio models (props), lit per vertex from a Source-style ambient cube:
// six colours for +X, -X, +Y, -Y, +Z, -Z in model space, blended by the
// squared components of the vertex normal.

struct PushConstants {
    float4x4 mvp;
    uint4 cube_a;   // 18 half-floats: rgb for +X, -X, +Y, -Y, +Z, -Z ...
    uint4 cube_b;
    uint cube_c;
    float alpha_ref;
    uint flags;
    float padding;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

static const uint kAlphaTest = 1;
static const uint kUnlit = 4;
static const uint kSelfIllum = 8;
static const uint kTranslucent = 16;

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float3 normal : NORMAL;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float3 light : COLOR0;
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
    output.uv = input.uv;
    const float3 n = normalize(input.normal);
    const float3 n2 = n * n;
    output.light = n2.x * cube_color(n.x >= 0 ? 0 : 1) +
                   n2.y * cube_color(n.y >= 0 ? 2 : 3) +
                   n2.z * cube_color(n.z >= 0 ? 4 : 5);
    return output;
}

[[vk::binding(0, 0)]] SamplerState texture_sampler;
[[vk::binding(1, 0)]] Texture2D base_texture;

float4 ps_main(VSOutput input) : SV_Target {
    float4 base = base_texture.Sample(texture_sampler, input.uv);
    if (pc.flags & kAlphaTest) {
        clip(base.a - pc.alpha_ref);
    }
    float3 color = (pc.flags & kUnlit) ? base.rgb : base.rgb * input.light;
    if (pc.flags & kSelfIllum) {
        color = lerp(color, base.rgb, base.a);
    }
    return float4(color, (pc.flags & kTranslucent) ? base.a : 1.0);
}
