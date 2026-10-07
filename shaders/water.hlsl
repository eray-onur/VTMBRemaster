// Water surfaces, approximated without reflection/refraction render targets:
// fog colour seen from above, reflection tint at grazing angles (Fresnel),
// rippled by two scrolling layers of the material's normal map.

#include "color.hlsli"

struct PushConstants {
    float4x4 mvp;
    float4 camera_time;  // xyz: camera position, w: time in seconds
    float4 fog_color;    // rgb, a: opacity
    float4 reflect_tint; // rgb, a: unused
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float3 world : TEXCOORD1;
};

VSOutput vs_main(VSInput input) {
    VSOutput output;
    output.position = mul(pc.mvp, float4(input.position, 1.0));
    output.uv = input.uv;
    output.world = input.position;
    return output;
}

[[vk::binding(0, 0)]] SamplerState texture_sampler;
[[vk::binding(1, 0)]] Texture2D normal_map;

float4 ps_main(VSOutput input) : SV_Target {
    const float t = pc.camera_time.w;
    const float3 n1 = normal_map.Sample(texture_sampler, input.uv + float2(0.020, 0.013) * t).xyz * 2.0 - 1.0;
    const float3 n2 = normal_map.Sample(texture_sampler, input.uv * 0.7 - float2(0.011, 0.017) * t).xyz * 2.0 - 1.0;
    const float3 normal = normalize(float3((n1.xy + n2.xy) * 0.5, 1.0));

    const float3 to_eye = normalize(pc.camera_time.xyz - input.world);
    const float fresnel = 0.02 + 0.98 * pow(1.0 - saturate(dot(to_eye, normal)), 5.0);
    // Without a reflection render target, stand in for the (dark, night-time)
    // reflected scene with a dim tint.
    float3 color = lerp(pc.fog_color.rgb, pc.reflect_tint.rgb * 0.05, fresnel);

    // A faint highlight from a light high above, so the ripples read at night.
    const float3 half_vec = normalize(to_eye + float3(0.0, 0.0, 1.0));
    color += pow(saturate(dot(normal, half_vec)), 80.0) * 0.15 * pc.reflect_tint.rgb;
    return float4(linear_to_srgb(color), pc.fog_color.a);
}
