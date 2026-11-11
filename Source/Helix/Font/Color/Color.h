#pragma once

#include <Helix/Font/Raster/Raster.h>

namespace helix::font
{
    struct BitmapStrike
    {
        u16 ppemX = 0, ppemY = 0;
        u16 firstGlyph = 0, lastGlyph = 0;
        u8 bitDepth = 0;
        // Offsets of the strike's index subtables and their count, in CBLC.
        u32 indexArray = 0, indexCount = 0;
    };

    // Color bitmap glyphs of CBLC / CBDT: PNG images (image formats 17, 18 and 19) in strikes of fixed sizes.
    class ColorBitmaps
    {
    public:
        bool Read(const Font& font, String& error);
        bool Empty() const { return strikes_.Empty(); }
        const Array<BitmapStrike>& Strikes() const { return strikes_; }

        // The glyph from the strike closest to `ppem` (the smallest at least as large, else the largest), as RGBA8 at
        // the strike's size; `strike` tells which. False with an error when the glyph has no bitmap or its data is
        // damaged.
        bool Get(u32 glyph, u32 ppem, GlyphBitmap& out, u32& strike, String& error) const;
        // The horizontal advance of the glyph in the strike, in pixels.
        bool Advance(u32 glyph, u32 strike, u32& advance) const;

    private:
        bool Locate(u32 glyph, const BitmapStrike& s, usize& data, usize& size, u16& imageFormat, usize& bigMetrics) const;

        Bytes cblc_, cbdt_;
        Array<BitmapStrike> strikes_;
    };

    struct ColorLayer
    {
        u16 glyph = 0;
        // A palette entry, 0xFFFF for the text color.
        u16 paletteIndex = 0;
    };

    // COLR version 0 glyphs (layers of outline glyphs, each in one color) with the CPAL palettes.
    class ColorLayers
    {
    public:
        bool Read(const Font& font, String& error);
        bool Empty() const { return bases_.Empty(); }
        // The layers of a glyph, bottom first; empty when it has none.
        Span<const ColorLayer> Layers(u32 glyph) const;
        u32 PaletteCount() const { return palettes_; }
        u32 PaletteEntries() const { return entries_; }
        // RGBA, red in the low byte; transparent black outside the palettes.
        u32 Color(u32 palette, u32 entry) const;

    private:
        struct Base
        {
            u16 glyph;
            u32 first;
            u16 count;
        };
        Array<Base> bases_;
        Array<ColorLayer> layers_;
        // Per palette, the index of its first color.
        Array<u16> paletteStarts_;
        Array<u32> colors_;
        u32 palettes_ = 0, entries_ = 0;
    };

    // Renders a COLR glyph into RGBA8 (straight alpha): its layers rasterized and drawn over each other in their colors,
    // `foreground` (RGBA, red in the low byte) where a layer asks for the text color.
    bool RenderColorGlyph(const ColorLayers& colors, const Outlines& outlines, u32 glyph, u32 palette, u32 foreground, const RasterOptions& options,
                          GlyphBitmap& out, String& error);
}
