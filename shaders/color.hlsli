// Output encoding. The window and render targets use plain 8-bit formats, so
// every pixel shader writes sRGB-encoded colour itself and blending happens on
// encoded values, in gamma space, as in the original game (docs/RENDERING.md).
// Shading stays in linear light; textures are decoded by their sRGB formats.

float3 linear_to_srgb(float3 c) {
    c = saturate(c);
    return lerp(1.055 * pow(c, 1.0 / 2.4) - 0.055, c * 12.92, step(c, 0.0031308));
}
