#include <Support/SampleFonts.h>

#include <Helix/Font/Font/Font.h>
#include <Helix/Font/Layout/Kern.h>
#include <Helix/Testing/Test.h>

#include <cstdio>

using namespace helix;
using namespace helix::font;
using namespace helix::font::testing;

namespace
{
    bool OpenFont(const Array<u8>& bytes, Font& f, u32 index = 0)
    {
        String error;
        bool ok = f.Open(Span<const u8>(bytes), index, error);
        if (!ok)
            std::printf("    %s\n", error.CStr());
        return ok;
    }
}

HX_TEST(Font, ReadsTheTablesOfATrueTypeFont)
{
    Array<u8> bytes = BuildFont(TrueTypeSpec());
    Font f;
    HX_REQUIRE(OpenFont(bytes, f));
    HX_CHECK(Font::CountFonts(bytes) == 1u);
    HX_CHECK(!f.IsCff() && f.HasTable(Tag("glyf")) && !f.HasTable(Tag("GSUB")));
    HX_CHECK(f.GlyphCount() == 8u && f.Head().unitsPerEm == 1000 && f.Head().indexToLocFormat == 0);
    HX_CHECK(f.Head().xMin == 0 && f.Head().yMax == 700);
    HX_CHECK(f.Hhea().ascender == 800 && f.Hhea().descender == -200 && f.Hhea().lineGap == 90 && f.Hhea().numberOfHMetrics == 8);
    const Os2Table& o = f.Os2();
    HX_CHECK(o.present && o.version == 4 && o.weightClass == 700 && o.widthClass == 5 && o.strikeoutSize == 18 && o.familyClass == 20);
    HX_CHECK(o.panose[9] == 9 && o.unicodeRange[3] == 8u && o.vendorID == Tag("HLX ") && o.typoAscender == 800 && o.winDescent == 250);
    HX_CHECK(o.xHeight == 480 && o.capHeight == 700 && o.breakChar == 32 && o.maxContext == 3);
    HX_CHECK(f.Post().italicAngle == -12.0f && f.Post().underlinePosition == -100 && f.Post().underlineThickness == 50);
    HX_CHECK(f.Name(kNameFamily) == StringView("Helix Test") && f.Name(kNameFullName) == StringView("Helix Test Regular"));
    HX_CHECK(f.Name(1) == StringView("Helix Test") && f.Name(9).Empty());
    bool mac = false;
    for (const NameRecord& r : f.Names())
        mac = mac || (r.platform == 1 && r.id == 1 && r.text == StringView("Mac Family"));
    HX_CHECK(mac);
    HX_CHECK(f.GlyphName(2) == StringView("A") && f.GlyphName(3) == StringView("O") && f.GlyphName(6) == StringView("Aacute"));
    HX_CHECK(f.GlyphName(0) == StringView(".notdef") && f.GlyphName(100).Empty());
    HX_CHECK(StandardGlyphName(257) == StringView("dcroat") && StandardGlyphName(36) == StringView("A") && StandardGlyphName(258).Empty());
    GlyphMetrics m = f.HMetrics(2);
    HX_CHECK(m.advance == 600 && m.leftSideBearing == 50);
    HX_CHECK(f.HMetrics(1).advance == 250 && f.HMetrics(99).advance == 0);
    HX_CHECK(f.GlyphIndex('A') == 2u && f.GlyphIndex(' ') == 1u && f.GlyphIndex(0xC1) == 6u && f.GlyphIndex(0x1F600) == 7u);
    HX_CHECK(f.GlyphIndex('B') == 0u && f.GlyphIndex(0x1F601) == 0u);

    KernTable k;
    String error;
    HX_REQUIRE(k.Read(f, error));
    HX_CHECK(k.Pairs().Size() == 2u && k.Get(2, 3) == -40 && k.Get(4, 2) == 25 && k.Get(3, 2) == 0);
}

HX_TEST(Font, CharacterMapFormats)
{
    for (bool array : {false, true})
    {
        FontSpec s = TrueTypeSpec();
        s.cmapGlyphArray = array;
        s.cmapFormat6 = true;
        s.cmap.Push({66, 3});
        s.cmap.Push({67, 4});
        s.variations = {{0xFE0F, 65, 0}, {0xFE0E, 65, 3}, {0xE0100, 0x1F600, 2}};
        Array<u8> bytes = BuildFont(s);
        Font f;
        HX_REQUIRE(OpenFont(bytes, f));
        HX_CHECK(f.GlyphIndex('A') == 2u && f.GlyphIndex('B') == 3u && f.GlyphIndex('C') == 4u && f.GlyphIndex('D') == 4u);
        HX_CHECK(f.GlyphIndex(0x1F600) == 7u);
        // Default and own glyphs of variation sequences, and sequences the font does not list.
        HX_CHECK(f.GlyphIndex('A', 0xFE0F) == 2u && f.GlyphIndex('A', 0xFE0E) == 3u && f.GlyphIndex(0x1F600, 0xE0100) == 2u);
        HX_CHECK(f.GlyphIndex('B', 0xFE0F) == 0u && f.GlyphIndex('A', 0xFE00) == 0u);
        struct Seen
        {
            u32 count = 0, last = 0;
            bool ordered = true;
        } seen;
        f.ForEachCodepoint(
            [](void* c, u32 cp, u32 g) {
                Seen& s2 = *static_cast<Seen*>(c);
                s2.ordered = s2.ordered && (s2.count == 0 || cp > s2.last) && g != 0;
                s2.last = cp;
                s2.count++;
            },
            &seen);
        HX_CHECK(seen.count == 9u && seen.ordered && seen.last == 0x1F600u);
    }
}

HX_TEST(Font, ReadsCollections)
{
    FontSpec a = TrueTypeSpec(), b = TrueTypeSpec();
    b.unitsPerEm = 2048;
    b.longLoca = true;
    Array<u8> ttc = BuildCollection({BuildFont(a), BuildFont(b)});
    HX_CHECK(Font::CountFonts(ttc) == 2u);
    Font first, second;
    HX_REQUIRE(OpenFont(ttc, first, 0) && OpenFont(ttc, second, 1));
    HX_CHECK(first.Head().unitsPerEm == 1000 && second.Head().unitsPerEm == 2048 && second.Head().indexToLocFormat == 1);
    HX_CHECK(second.GlyphIndex('O') == 3u && second.Name(kNameFamily) == StringView("Helix Test"));
    Font third;
    String error;
    HX_CHECK(!third.Open(Span<const u8>(ttc), 2, error) && error == StringView("font 2 of a collection of 2"));
}

HX_TEST(Font, RefusesDamagedFiles)
{
    Array<u8> good = BuildFont(TrueTypeSpec());
    auto refused = [&](auto change, StringView what) {
        Array<u8> bytes = good;
        change(bytes);
        Font f;
        String e;
        bool ok = f.Open(Span<const u8>(bytes), 0, e);
        if (ok || !e.View().StartsWith(what))
        {
            std::printf("    %s (expected %.*s)\n", ok ? "opened" : e.CStr(), int(what.Size()), what.Data());
            return false;
        }
        return true;
    };
    HX_CHECK(refused([](Array<u8>& b) { b[0] = 'X'; }, "not a TrueType or OpenType font"));
    HX_CHECK(refused([](Array<u8>& b) { b[4] = 0x7F; }, "a table directory outside the file"));
    // The first record's length.
    HX_CHECK(refused([](Array<u8>& b) { b[12 + 12] = 0x7F; }, "table '"));
    HX_CHECK(Font::CountFonts(Span<const u8>(good.Data(), 3)) == 0u);
    Font f;
    String e;
    HX_CHECK(!f.Open(Span<const u8>(good), 1, e) && e == StringView("font 1 of a file with one font"));
    // Every cut opens or is refused; none reads outside.
    for (usize n = 0; n < good.Size(); n += 3)
    {
        Font cut;
        String error;
        cut.Open(Span<const u8>(good.Data(), n), 0, error);
        cut.GlyphIndex('A');
        cut.HMetrics(2);
        cut.GlyphName(3);
    }
}
