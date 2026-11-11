#include <Helix/Font/Atlas/FontAtlas.h>
#include <Helix/Testing/Test.h>

using namespace helix;
using namespace helix::font;

namespace
{
    GlyphBitmap Filled(u32 w, u32 h, u8 value)
    {
        GlyphBitmap b;
        b.width = w, b.height = h, b.left = -1, b.top = -i32(h);
        b.pixels.Resize(usize(w) * h, value);
        return b;
    }
}

HX_TEST(Atlas, PacksGlyphsIntoPages)
{
    FontAtlas atlas(64, 64, 1, 1);
    Array<AtlasPlace> places;
    // 30 glyphs of 12 x 10: five per row of 64 with padding, five rows of 12 per page: two pages.
    for (u32 i = 0; i < 30; i++)
    {
        AtlasPlace p;
        HX_REQUIRE(atlas.Add(FontAtlas::Key(i, 16, 0), Filled(12, 10, u8(i + 1)), p));
        places.Push(p);
    }
    HX_CHECK(atlas.PageCount() == 2u);
    bool inside = true, apart = true, copied = true;
    for (usize i = 0; i < places.Size(); i++)
    {
        const AtlasPlace& a = places[i];
        inside = inside && a.x >= 1 && a.y >= 1 && a.x + a.width + 1 <= 64 && a.y + a.height + 1 <= 64 && a.left == -1 && a.top == -10;
        for (usize j = 0; j < i; j++)
        {
            const AtlasPlace& b = places[j];
            bool overlap = a.page == b.page && a.x < b.x + b.width + 1 && b.x < a.x + a.width + 1 && a.y < b.y + b.height + 1 && b.y < a.y + a.height + 1;
            apart = apart && !overlap;
        }
        const u8* page = atlas.PagePixels(a.page);
        for (u32 y = 0; y < a.height; y++)
            for (u32 x = 0; x < a.width; x++)
                copied = copied && page[(a.y + y) * 64 + a.x + x] == u8(i + 1);
    }
    HX_CHECK(inside && apart && copied);

    // The same key gives the same place; Find knows it.
    AtlasPlace again;
    HX_CHECK(atlas.Add(FontAtlas::Key(3, 16, 0), Filled(1, 1, 0), again) && again.x == places[3].x && again.page == places[3].page);
    HX_CHECK(atlas.Find(FontAtlas::Key(3, 16, 0)) != nullptr && atlas.Find(FontAtlas::Key(3, 17, 0)) == nullptr);
    // Too large, or of another channel count.
    AtlasPlace p;
    HX_CHECK(!atlas.Add(1000, Filled(63, 2, 1), p));
    GlyphBitmap rgba = Filled(2, 2, 1);
    rgba.channels = 4;
    rgba.pixels.Resize(16, u8(1));
    HX_CHECK(!atlas.Add(1001, rgba, p));

    u32 x0, y0, x1, y1;
    HX_CHECK(atlas.Dirty(0, x0, y0, x1, y1) && x0 == 1u && y0 == 1u && x1 <= 64u && y1 <= 64u);
    atlas.ClearDirty();
    HX_CHECK(!atlas.Dirty(0, x0, y0, x1, y1));
    atlas.Clear();
    HX_CHECK(atlas.PageCount() == 2u && atlas.Find(FontAtlas::Key(3, 16, 0)) == nullptr && atlas.PagePixels(0)[65] == 0);
    HX_CHECK(atlas.Add(7, Filled(12, 10, 9), p) && p.page == 0u && p.x == 1u && p.y == 1u);
}
