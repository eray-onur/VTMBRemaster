// Skinned (animated) studio models. Per-draw shader data holds the model-space
// ambient cube (6 float4) followed by one 3x4 skinning matrix per bone
// (3 float4 rows each).

struct PushConstants {
    float4x4 mvp;
    float alpha_ref;
    uint flags;
    float2 padding;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

[[vk::binding(0, 1)]] StructuredBuffer<float4> shader_data;

static const uint kAlphaTest = 1;
static const uint kUnlit = 4;
static const uint kSelfIllum = 8;
static const uint kTranslucent = 16;
static const uint kBoneBase = 6;

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float3 normal : NORMAL;
    [[vk::location(3)]] uint4 bones : BLENDINDICES;
    [[vk::location(4)]] float4 weights : BLENDWEIGHT;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float3 light : COLOR0;
};

float3 transform(uint bone, float4 v) {
    const uint row = kBoneBase + bone * 3;
    return float3(dot(shader_data[row], v), dot(shader_data[row + 1], v), dot(shader_data[row + 2], v));
}

VSOutput vs_main(VSInput input) {
    float3 position = float3(0, 0, 0);
    float3 normal = float3(0, 0, 0);
    [unroll] for (uint k = 0; k < 3; ++k) {
        const float w = input.weights[k];
        if (w > 0) {
            position += w * transform(input.bones[k], float4(input.position, 1.0));
            normal += w * transform(input.bones[k], float4(input.normal, 0.0));
        }
    }

    VSOutput output;
    output.position = mul(pc.mvp, float4(position, 1.0));
    output.uv = input.uv;
    const float3 n = normalize(normal);
    const float3 n2 = n * n;
    output.light = n2.x * shader_data[n.x >= 0 ? 0 : 1].rgb +
                   n2.y * shader_data[n.y >= 0 ? 2 : 3].rgb +
                   n2.z * shader_data[n.z >= 0 ? 4 : 5].rgb;
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
