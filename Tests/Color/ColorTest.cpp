#include <Support/SampleLayout.h>

#include <Helix/Font/Color/Color.h>
#include <Helix/Image/Codecs/Png.h>
#include <Helix/Testing/Test.h>

#include <cstdio>

using namespace helix;
using namespace helix::font;
using namespace helix::font::testing;


HX_TEST(Color, BitmapStrikes)
{
    image::Image small = Picture(4, 3, 1), large = Picture(8, 6, 2);
    Array<u8> png20, png40;
    image::EncodePng(small, png20);
    image::EncodePng(large, png40);
    FontSpec s = TrueTypeSpec();
    AddBitmaps(s, png20, png40);
    Array<u8> bytes = BuildFont(s);
    Font f;
    String error;
    HX_REQUIRE(f.Open(Span<const u8>(bytes), 0, error));
    ColorBitmaps c;
    HX_REQUIRE(c.Read(f, error));
    HX_REQUIRE(c.Strikes().Size() == 2u);
    HX_CHECK(c.Strikes()[0].ppemY == 20 && c.Strikes()[1].ppemY == 40 && c.Strikes()[0].bitDepth == 32);

    GlyphBitmap b;
    u32 strike = 9;
    HX_REQUIRE(c.Get(3, 20, b, strike, error));
    HX_CHECK(strike == 0u && b.width == 4u && b.height == 3u && b.channels == 4u && b.left == 1 && b.top == -3);
    HX_CHECK(b.pixels.Size() == small.pixels.Size() && std::memcmp(b.pixels.Data(), small.pixels.Data(), small.pixels.Size()) == 0);
    u32 advance = 0;
    HX_CHECK(c.Advance(3, 0, advance) && advance == 6u);
    // Glyph 2 has both strikes: 30 ppem takes the 40 one, 10 the 20 one, 100 the largest.
    HX_REQUIRE(c.Get(2, 30, b, strike, error));
    HX_CHECK(strike == 1u && b.width == 8u && b.left == 2 && b.top == -6 && std::memcmp(b.pixels.Data(), large.pixels.Data(), large.pixels.Size()) == 0);
    HX_CHECK(c.Advance(2, 1, advance) && advance == 10u);
    HX_CHECK(c.Get(2, 10, b, strike, error) && strike == 0u);
    HX_CHECK(c.Get(2, 100, b, strike, error) && strike == 1u);
    HX_CHECK(!c.Get(1, 20, b, strike, error) && error == StringView("glyph 1 has no color bitmap"));
}

HX_TEST(Color, LayeredGlyphs)
{
    FontSpec s = TrueTypeSpec();
    AddColorLayers(s);
    Array<u8> bytes = BuildFont(s);
    Font f;
    Outlines o;
    String error;
    HX_REQUIRE(f.Open(Span<const u8>(bytes), 0, error) && o.Open(f, error));
    ColorLayers c;
    HX_REQUIRE(c.Read(f, error));
    HX_CHECK(c.PaletteCount() == 2u && c.PaletteEntries() == 1u);
    HX_CHECK(c.Color(0, 0) == 0xFF0000FFu && c.Color(1, 0) == 0xFFFF0000u && c.Color(2, 0) == 0u);
    Span<const ColorLayer> layers = c.Layers(6);
    HX_REQUIRE(layers.Size() == 2u);
    HX_CHECK(layers[0].glyph == 2 && layers[1].paletteIndex == 0xFFFF && c.Layers(2).Empty());

    RasterOptions r;
    r.scale = 0.03125f;
    GlyphBitmap b;
    HX_REQUIRE(RenderColorGlyph(c, o, 6, 1, 0xFF00FF00u, r, b, error));
    HX_CHECK(b.channels == 4u && b.left == 0 && b.top == -22 && b.width == 18u && b.height == 22u);
    // The A body in blue, its hole clear, the acute layer (glyph 5 at the origin, 0..3.1 pixels) in the green text color
    // over the A.
    auto px = [&](i32 x, i32 y) { return b.Row(u32(y - b.top)) + 4 * u32(x - b.left); };
    const u8* body = px(5, -1);
    const u8* hole = px(9, -11);
    const u8* acute = px(2, -2);
    HX_CHECK(body[0] == 0 && body[2] == 255 && body[3] == 255);
    HX_CHECK(hole[3] == 0);
    HX_CHECK(acute[1] == 255 && acute[0] == 0 && acute[3] == 255);
    HX_CHECK(!RenderColorGlyph(c, o, 2, 0, 0, r, b, error) && error == StringView("glyph 2 has no color layers"));
}
