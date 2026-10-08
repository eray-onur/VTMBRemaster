// Output encoding. The window and render targets use plain 8-bit formats, so
// every pixel shader writes sRGB-encoded colour itself and blending happens on
// encoded values, in gamma space, as in the original game (docs/RENDERING.md).
// Shading stays in linear light; textures are decoded by their sRGB formats.

// Distance fog (worldspawn fogenable/fogcolor/fogstart/fogend, or the
// sky_camera's own for the 3D skybox): linear in view depth, blended on
// encoded colour like the original's fixed-function fog. Settings come as
// two float4: (colour rgb encoded 0-1, mode) and (start, end, water z, eye z).
// Mode 0: no fog; 1: distance fog; 2: water fog, used while drawing what lies
// below a water surface: the distance counts only the part of the view ray
// below the water (z < water z), as in the original's vertex shaders.
float fog_amount(float view_depth, float world_z, float4 fog_color, float4 fog_range) {
    if (fog_color.a < 0.5 || fog_range.y <= fog_range.x) {
        return 0.0;
    }
    if (fog_color.a > 1.5) {
        const float under = max(fog_range.z - world_z, 0.0) / max(fog_range.w - world_z, 1e-3);
        return saturate((view_depth * saturate(under) - fog_range.x) / (fog_range.y - fog_range.x));
    }
    // Linear in depth, then 1 - (1 - f)^2, as if fogged twice: measured
    // against the original on ch_hub_1 and sm_hub_1 (docs/RENDERING.md).
    const float f = saturate((view_depth - fog_range.x) / (fog_range.y - fog_range.x));
    return 1.0 - (1.0 - f) * (1.0 - f);
}

float3 linear_to_srgb(float3 c);

// Opaque and alpha-blended output: towards the fog colour.
float3 fogged(float3 encoded, float4 fog_color, float amount) {
    return lerp(encoded, fog_color.rgb, amount);
}

// Environment map term, on encoded colour as in the original: the cube map's
// colour times $envmaptint, then $envmapcontrast (towards its square) and
// $envmapsaturation (from grey). `tint_contrast`: tint rgb, contrast.
float3 envmap_term(float3 encoded_env, float4 tint_contrast, float saturation) {
    float3 c = encoded_env * tint_contrast.rgb;
    c += tint_contrast.a * (c * c - c);
    const float grey = dot(c, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0));
    return lerp(float3(grey, grey, grey), c, saturation);
}

float3 linear_to_srgb(float3 c) {
    c = saturate(c);
    return lerp(1.055 * pow(c, 1.0 / 2.4) - 0.055, c * 12.92, step(c, 0.0031308));
}
