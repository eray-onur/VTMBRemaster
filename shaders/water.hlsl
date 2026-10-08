// Water surfaces, as the original's DX9 water draws them (docs/RENDERING.md):
// the refraction (the scene below the surface, in the water's fog) times the
// refraction tint, plus the reflection (the scene mirrored about the surface)
// weighted by a Fresnel term. Both render targets are sampled at the pixel's
// screen position, pushed by the dudv map: its offset is a displacement along
// the surface, in world units times the material's amount, projected to the
// screen. Colours combine on encoded values, as in the original.

#include "color.hlsli"

struct PushConstants {
    float4x4 mvp;
    float4 refract; // Tint (encoded), amount.
    float4 reflect; // Tint (encoded; the original's DX9 shader does not apply it), amount.
    float4 bump_u;  // dudv and normal map transform, u row; w: flags.
    float4 bump_v;  // v row.
};
[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

static const uint kReflect = 1;
static const uint kRefract = 2;

// Camera position and time, then the world fog (2 elements; see color.hlsli).
[[vk::binding(0, 1)]] StructuredBuffer<float4> water_data;

struct VSInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;      // Surface texture coordinates.
    [[vk::location(1)]] float2 bump_uv : TEXCOORD1; // dudv and normal map coordinates.
    [[vk::location(2)]] float3 world : TEXCOORD2;
    [[vk::location(3)]] float4 clip : TEXCOORD3;
};

VSOutput vs_main(VSInput input) {
    VSOutput output;
    output.position = mul(pc.mvp, float4(input.position, 1.0));
    output.uv = input.uv;
    output.bump_uv = float2(dot(pc.bump_u.xyz, float3(input.uv, 1.0)), dot(pc.bump_v.xyz, float3(input.uv, 1.0)));
    output.world = input.position;
    output.clip = output.position;
    return output;
}

[[vk::binding(0, 0)]] SamplerState texture_sampler;
[[vk::binding(1, 0)]] Texture2D dudv_map;
[[vk::binding(2, 0)]] Texture2D normal_map;
[[vk::binding(3, 0)]] Texture2D reflection; // Drawn mirrored left to right (keeps the triangle winding).
[[vk::binding(4, 0)]] Texture2D refraction;

float4 ps_main(VSOutput input) : SV_Target {
    const uint flags = (uint)pc.bump_u.w;
    const float3 eye = water_data[0].xyz;

    // The surface's tangent frame: directions of increasing u and v, from the
    // screen-space derivatives (exact on a flat face), and its normal.
    const float3 dpdx = ddx(input.world), dpdy = ddy(input.world);
    const float2 dudx = ddx(input.uv), dudy = ddy(input.uv);
    const float det = dudx.x * dudy.y - dudx.y * dudy.x;
    float3 t = (dpdx * dudy.y - dpdy * dudx.y) * sign(det);
    float3 b = (dpdy * dudx.x - dpdx * dudy.x) * sign(det);
    t = dot(t, t) > 1e-12 ? normalize(t) : float3(1, 0, 0);
    b = dot(b, b) > 1e-12 ? normalize(b) : float3(0, 1, 0);
    const float3 n = float3(0, 0, 1);

    const float2 offset = dudv_map.Sample(texture_sampler, input.bump_uv).rg * 2.0 - 1.0;
    const float3 displacement = offset.x * t + offset.y * b;
    // Screen position (0-1, y down) and the displacement's screen offset.
    const float2 screen = input.clip.xy / input.clip.w * 0.5 + 0.5;
    const float2 shift = mul(pc.mvp, float4(displacement, 0.0)).xy / input.clip.w;

    float3 color = pc.refract.rgb; // The fog colour when there is no refraction.
    if (flags & kRefract) {
        const float2 uv = saturate(screen + shift * pc.refract.a);
        color = linear_to_srgb(refraction.Sample(texture_sampler, uv).rgb) * pc.refract.rgb;
    }

    // Fresnel: (1 - cos)^4 between the view direction and the normal-mapped normal.
    float3 reflected = float3(0, 0, 0);
    if (flags & kReflect) {
        const float3 nm = normal_map.Sample(texture_sampler, input.bump_uv).xyz * 2.0 - 1.0;
        const float3 normal = t * nm.x + b * nm.y + n * nm.z;
        const float c = 1.0 - max(dot(normalize(eye - input.world), normal), 0.0);
        const float fresnel = (c * c) * (c * c);
        float2 uv = saturate(screen + shift * pc.reflect.a);
        uv.x = 1.0 - uv.x;
        reflected = linear_to_srgb(reflection.Sample(texture_sampler, uv).rgb) * fresnel;
    }

    const float4 fog_color = water_data[1];
    const float fog = fog_amount(input.clip.w, input.world.z, fog_color, water_data[2]);
    return float4(fogged(color, fog_color, fog) + reflected * (1.0 - fog), 1.0);
}
