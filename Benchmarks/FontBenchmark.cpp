#include <Support/SampleLayout.h>

#include <Helix/Font/Atlas/FontAtlas.h>
#include <Helix/Font/Layout/Layout.h>
#include <Helix/Font/Outline/Outline.h>
#include <Helix/Testing/Bench.h>

using namespace helix;
using namespace helix::font;
using namespace helix::font::testing;

namespace
{
    struct Loaded
    {
        Array<u8> bytes;
        Font font;
        Outlines outlines;
        Outline o, d;
    };

    Loaded& Sample()
    {
        static Loaded l = [] {
            Loaded s;
            s.bytes = BuildFont(FullSpec());
            String error;
            s.font.Open(Span<const u8>(s.bytes), 0, error);
            s.outlines.Open(s.font, error);
            s.outlines.Get(3, s.o, error);
            s.outlines.Get(2, s.d, error);
            return s;
        }();
        return l;
    }
}

// Reading the outline of a composite glyph (A, an accent; three contours).
HX_BENCH(OutlineGlyf)
{
    Loaded& l = Sample();
    Outline o;
    String error;
    while (state.KeepRunning())
    {
        l.outlines.Get(6, o, error);
        helix::testing::DoNotOptimize(o.points.Data());
    }
}

HX_BENCH(OutlineCff)
{
    static Array<u8> bytes = BuildFont(CffSpec());
    Font f;
    Outlines outlines;
    String error;
    f.Open(Span<const u8>(bytes), 0, error);
    outlines.Open(f, error);
    Outline o;
    while (state.KeepRunning())
    {
        outlines.Get(2, o, error);
        helix::testing::DoNotOptimize(o.points.Data());
    }
}

// Coverage of 'O' (four quadratics) at 32 pixels per em.
HX_BENCH(Rasterize32)
{
    Loaded& l = Sample();
    RasterOptions r;
    r.scale = 32.0f / 1000;
    GlyphBitmap b;
    String error;
    state.SetItemsPerIteration(1);
    while (state.KeepRunning())
    {
        Rasterize(l.o, r, b, error);
        helix::testing::DoNotOptimize(b.pixels.Data());
    }
}

// Distance fields of 'A' (a square with a hole) at 32 pixels, 4 pixels of padding.
HX_BENCH(SignedDistanceField32)
{
    Loaded& l = Sample();
    DistanceOptions d;
    d.scale = 32.0f / 1000;
    GlyphBitmap b;
    String error;
    while (state.KeepRunning())
    {
        SignedDistanceField(l.d, d, b, error);
        helix::testing::DoNotOptimize(b.pixels.Data());
    }
}

HX_BENCH(MultiChannelDistanceField32)
{
    Loaded& l = Sample();
    DistanceOptions d;
    d.scale = 32.0f / 1000;
    GlyphBitmap b;
    String error;
    while (state.KeepRunning())
    {
        MultiChannelDistanceField(l.o, d, b, error);
        helix::testing::DoNotOptimize(b.pixels.Data());
    }
}

// 1000 glyphs of 20 x 24 into 512 x 512 pages.
HX_BENCH(AtlasFill)
{
    GlyphBitmap b;
    b.width = 20, b.height = 24;
    b.pixels.Resize(20 * 24, u8(128));
    state.SetItemsPerIteration(1000);
    while (state.KeepRunning())
    {
        FontAtlas atlas(512, 512, 1, 1);
        AtlasPlace p;
        for (u32 i = 0; i < 1000; i++)
            atlas.Add(i, b, p);
        helix::testing::DoNotOptimize(atlas.PagePixels(0));
    }
}

HX_BENCH(LayoutRead)
{
    Loaded& l = Sample();
    Layout g;
    String error;
    while (state.KeepRunning())
    {
        g.Read(l.font, Tag("GPOS"), error);
        helix::testing::DoNotOptimize(g.lookups.Data());
    }
}
