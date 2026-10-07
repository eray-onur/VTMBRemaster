// Output encoding. The window and render targets use plain 8-bit formats, so
// every pixel shader writes sRGB-encoded colour itself and blending happens on
// encoded values, in gamma space, as in the original game (docs/RENDERING.md).
// Shading stays in linear light; textures are decoded by their sRGB formats.

// Distance fog (worldspawn fogenable/fogcolor/fogstart/fogend, or the
// sky_camera's own for the 3D skybox): linear in view depth, blended on
// encoded colour like the original's fixed-function fog. Settings come as
// two float4: (colour rgb encoded 0-1, enabled 0/1) and (start, end, -, -).
float fog_amount(float view_depth, float4 fog_color, float4 fog_range) {
    if (fog_color.a < 0.5 || fog_range.y <= fog_range.x) {
        return 0.0;
    }
    return saturate((view_depth - fog_range.x) / (fog_range.y - fog_range.x));
}

float3 linear_to_srgb(float3 c);

// Opaque and alpha-blended output: towards the fog colour.
float3 fogged(float3 encoded, float4 fog_color, float amount) {
    return lerp(encoded, fog_color.rgb, amount);
}

float3 linear_to_srgb(float3 c) {
    c = saturate(c);
    return lerp(1.055 * pow(c, 1.0 / 2.4) - 0.055, c * 12.92, step(c, 0.0031308));
}
