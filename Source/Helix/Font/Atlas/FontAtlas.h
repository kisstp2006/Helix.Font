#pragma once

#include <Helix/Core/Containers/HashMap.h>
#include <Helix/Font/Raster/Raster.h>

namespace helix::font
{
    // Where a glyph bitmap sits in an atlas: its page, its pixel rectangle there, and its offset from the glyph origin.
    struct AtlasPlace
    {
        u32 page = 0;
        u32 x = 0, y = 0, width = 0, height = 0;
        i32 left = 0, top = 0;
    };

    // A dynamic glyph atlas: bitmaps (coverage, distance fields or color, one channel count per atlas) packed into pages
    // as they are asked for, through the stb_rect_pack copy (skyline). A full page starts a new one. Pages remember the
    // rectangle written since the last upload.
    class FontAtlas
    {
    public:
        FontAtlas(u32 pageWidth, u32 pageHeight, u32 channels, u32 padding = 1);
        ~FontAtlas();
        FontAtlas(const FontAtlas&) = delete;
        FontAtlas& operator=(const FontAtlas&) = delete;

        // A key from a glyph, a size and a kind (coverage, SDF, MSDF, color...), for the caller's own scheme.
        static u64 Key(u32 glyph, u32 size, u32 kind) { return u64(glyph) | u64(size & 0xFFFFFF) << 32 | u64(kind & 0xFF) << 56; }

        // The place of the bitmap stored under `key`, packing it when new. False when it is larger than a page or has
        // another channel count.
        bool Add(u64 key, const GlyphBitmap& bitmap, AtlasPlace& out);
        const AtlasPlace* Find(u64 key) const { return places_.Find(key); }

        u32 PageCount() const { return u32(pages_.Size()); }
        u32 PageWidth() const { return width_; }
        u32 PageHeight() const { return height_; }
        u32 Channels() const { return channels_; }
        const u8* PagePixels(u32 page) const;
        // The rectangle of a page written since ClearDirty, false when nothing was.
        bool Dirty(u32 page, u32& x0, u32& y0, u32& x1, u32& y1) const;
        void ClearDirty();
        // Every glyph removed, every page empty (kept for reuse).
        void Clear();

    private:
        struct Page;
        bool Pack(Page& page, u32 w, u32 h, u32& x, u32& y);

        u32 width_, height_, channels_, padding_;
        Array<Page*> pages_;
        HashMap<u64, AtlasPlace> places_;
    };
}
