#include <Support/SampleLayout.h>

#include <Helix/Font/Atlas/FontAtlas.h>
#include <Helix/Font/Color/Color.h>
#include <Helix/Font/Layout/Kern.h>
#include <Helix/Font/Layout/Layout.h>
#include <Helix/Font/Outline/Outline.h>
#include <Helix/Testing/Fuzz.h>

using namespace helix;
using namespace helix::font;
using namespace helix::font::testing;

namespace
{
    const Array<u8>& TrueTypeFont()
    {
        static const Array<u8> bytes = BuildFont(FullSpec());
        return bytes;
    }

    const Array<u8>& CffFont()
    {
        static const Array<u8> bytes = [] {
            FontSpec s = CffSpec();
            s.cidFds = {0, 1, 1, 0};
            return BuildFont(s);
        }();
        return bytes;
    }

    // The font with the bytes of some of its tables mutated in place, so the directory stays whole and the readers of
    // those tables see the damage.
    Array<u8> MutateTables(helix::testing::FuzzRound& round, const Array<u8>& font, std::initializer_list<const char*> tags)
    {
        Array<u8> bytes = font;
        Bytes b(bytes.Data(), bytes.Size());
        u16 count = b.U16(4);
        for (const char* tag : tags)
        {
            if (round.Below(2) == 0 && tags.size() > 1)
                continue;
            u32 want = u32(u8(tag[0])) << 24 | u32(u8(tag[1])) << 16 | u32(u8(tag[2])) << 8 | u8(tag[3]);
            for (u32 i = 0; i < count; i++)
                if (b.U32(12 + 16 * usize(i)) == want)
                {
                    u32 offset = b.U32(20 + 16 * usize(i)), length = b.U32(24 + 16 * usize(i));
                    usize n = round.Mutate(bytes.Data() + offset, length, length);
                    for (usize k = n; k < length; k++)
                        bytes[offset + k] = 0;
                }
        }
        return bytes;
    }

    // Everything that reads a font, on one font.
    u64 ReadAll(helix::testing::FuzzRound& round, Span<const u8> bytes)
    {
        Font f;
        String error;
        round.Step("open");
        if (!f.Open(bytes, 0, error))
            return 0;
        round.Step("metrics and names");
        for (u32 cp : {32u, 65u, 79u, 0xC1u, 0x1F600u})
            f.GlyphIndex(cp), f.GlyphIndex(cp, 0xFE0F);
        f.ForEachCodepoint([](void*, u32, u32) {}, nullptr);
        for (u32 g = 0; g < 10; g++)
            f.HMetrics(g), f.GlyphName(g);
        f.Name(kNameFamily);
        round.Step("outlines");
        Outlines o;
        u64 drawn = 0;
        if (o.Open(f, error))
        {
            Outline outline;
            for (u32 g = 0; g < f.GlyphCount() && g < 64; g++)
                if (o.Get(g, outline, error) && !outline.Empty())
                {
                    drawn++;
                    GlyphBitmap bitmap;
                    RasterOptions r;
                    r.scale = 0.02f;
                    r.oversampleX = 1 + round.Below(3);
                    Rasterize(outline, r, bitmap, error);
                    if (g < 4)
                    {
                        DistanceOptions d;
                        d.scale = 0.01f;
                        d.padding = 2;
                        SignedDistanceField(outline, d, bitmap, error);
                        MultiChannelDistanceField(outline, d, bitmap, error);
                    }
                }
        }
        round.Step("kern and layout");
        KernTable kern;
        kern.Read(f, error);
        kern.Get(2, 3);
        Layout gsub, gpos;
        GlyphDefinitions gdef;
        if (gsub.Read(f, Tag("GSUB"), error))
            for (u32 s = 0; s < gsub.coverages.Size(); s++)
                gsub.CoverageIndex(s, 20);
        if (gpos.Read(f, Tag("GPOS"), error))
            for (u32 s = 0; s < gpos.classDefs.Size(); s++)
                gpos.ClassOf(s, 30);
        gdef.Read(f, error);
        gdef.InMarkSet(0, 60);
        round.Step("color");
        ColorLayers colr;
        if (colr.Read(f, error))
        {
            GlyphBitmap b;
            RasterOptions r;
            r.scale = 0.02f;
            RenderColorGlyph(colr, o, 6, 1, 0xFFFFFFFFu, r, b, error);
        }
        ColorBitmaps cbdt;
        if (cbdt.Read(f, error))
        {
            GlyphBitmap b;
            u32 strike, advance;
            for (u32 g : {2u, 3u})
                if (cbdt.Get(g, 30, b, strike, error))
                    cbdt.Advance(g, strike, advance);
        }
        return drawn + 1;
    }
}

// The whole TrueType sample (with layout, color layers and bitmaps) mutated, directory and all.
HX_FUZZ(Font)
{
    Array<u8> bytes = TrueTypeFont();
    usize size = bytes.Size();
    bytes.Resize(size + 64);
    bytes.Resize(round.Mutate(bytes.Data(), size, bytes.Size()));
    return ReadAll(round, Span<const u8>(bytes));
}

HX_FUZZ(Glyf)
{
    Array<u8> bytes = MutateTables(round, TrueTypeFont(), {"glyf", "loca", "head", "maxp", "hmtx"});
    return ReadAll(round, Span<const u8>(bytes));
}

HX_FUZZ(Cff)
{
    Array<u8> bytes = MutateTables(round, CffFont(), {"CFF "});
    return ReadAll(round, Span<const u8>(bytes));
}

HX_FUZZ(Cmap)
{
    Array<u8> bytes = MutateTables(round, TrueTypeFont(), {"cmap", "name", "post", "OS/2", "kern"});
    return ReadAll(round, Span<const u8>(bytes));
}

HX_FUZZ(Layout)
{
    Array<u8> bytes = MutateTables(round, TrueTypeFont(), {"GSUB", "GPOS", "GDEF"});
    return ReadAll(round, Span<const u8>(bytes));
}

HX_FUZZ(Color)
{
    Array<u8> bytes = MutateTables(round, TrueTypeFont(), {"COLR", "CPAL", "CBLC", "CBDT"});
    return ReadAll(round, Span<const u8>(bytes));
}

// Outlines of random points through the rasterizer, the distance fields and the atlas.
HX_FUZZ(Raster)
{
    Outline o;
    u32 verbs = 1 + round.Below(24);
    auto point = [&] { return Point{float(i32(round.Below(2000)) - 1000) / 100.0f, float(i32(round.Below(2000)) - 1000) / 100.0f}; };
    for (u32 i = 0; i < verbs; i++)
    {
        u32 kind = round.Below(5);
        if (i == 0 || kind == 0)
            o.MoveTo(point());
        else if (kind == 1)
            o.LineTo(point());
        else if (kind == 2)
            o.QuadTo(point(), point());
        else
            o.CubicTo(point(), point(), point());
    }
    o.ComputeBounds();
    String error;
    GlyphBitmap b;
    RasterOptions r;
    r.scale = float(1 + round.Below(30)) / 10.0f;
    r.shiftX = float(round.Below(100)) / 100.0f;
    r.oversampleY = 1 + round.Below(4);
    round.Step("rasterize");
    bool ok = Rasterize(o, r, b, error);
    DistanceOptions d;
    d.scale = r.scale / 4;
    d.padding = round.Below(4);
    round.Step("distance fields");
    SignedDistanceField(o, d, b, error);
    MultiChannelDistanceField(o, d, b, error);
    round.Step("atlas");
    FontAtlas atlas(64, 64, 4, 1);
    AtlasPlace place;
    atlas.Add(1, b, place);
    return ok ? 1 : 0;
}
