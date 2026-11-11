#pragma once

#include <Helix/Font/Outline/Outline.h>

namespace helix::font
{
    // A glyph image: `channels` bytes per pixel, rows top first. Its top left pixel corner is at (left, top) in pixels
    // from the glyph origin, x right and y down (so a glyph above the baseline has a negative top).
    struct GlyphBitmap
    {
        u32 width = 0, height = 0, channels = 1;
        i32 left = 0, top = 0;
        Array<u8> pixels;

        u8* Row(u32 y) { return pixels.Data() + usize(y) * width * channels; }
        const u8* Row(u32 y) const { return pixels.Data() + usize(y) * width * channels; }
    };

    // No glyph bitmap is wider or taller.
    inline constexpr u32 kMaxGlyphPixels = 4096;

    struct RasterOptions
    {
        // Pixels per font unit: pixel size / units per em.
        float scale = 1;
        // A subpixel offset of the origin, in pixels.
        float shiftX = 0, shiftY = 0;
        // Oversampling as for packed atlases: the bitmap is `oversampleX` times wider (`oversampleY` taller) and box
        // filtered, so sampling it bilinearly at 1 / oversample steps places the glyph between pixels. 1 to 8.
        u32 oversampleX = 1, oversampleY = 1;
    };

    // 8 bit coverage of an outline (nonzero fill) through the stb_truetype rasterizer.
    bool Rasterize(const Outline& outline, const RasterOptions& options, GlyphBitmap& out, String& error);

    struct DistanceOptions
    {
        float scale = 1;
        // Pixels around the glyph, so the field reaches beyond its edges.
        u32 padding = 4;
        // The distance in pixels from the edge to where the field reaches 0 (outside) or 255 (inside); the edge is 128.
        float range = 4;
        float shiftX = 0, shiftY = 0;
    };

    // A signed distance field: 128 on the edge, larger inside, the exact distance to every line, quadratic and cubic.
    bool SignedDistanceField(const Outline& outline, const DistanceOptions& options, GlyphBitmap& out, String& error);

    // A multi channel signed distance field (RGB, with the true distance field in alpha): edges colored at the corners so
    // the median of the three channels keeps corners sharp when the field is magnified, with clashes between texels
    // corrected.
    bool MultiChannelDistanceField(const Outline& outline, const DistanceOptions& options, GlyphBitmap& out, String& error);

    // The median of three channels, as a shader reconstructs the MSDF.
    inline float Median(float a, float b, float c) { return a < b ? (b < c ? b : (a < c ? c : a)) : (a < c ? a : (b < c ? c : b)); }
}
