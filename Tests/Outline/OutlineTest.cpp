#include <Support/SampleFonts.h>

#include <Helix/Font/Outline/Outline.h>
#include <Helix/Testing/Test.h>

#include <cstdio>

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
    };

    bool Load(const FontSpec& spec, Loaded& l)
    {
        l.bytes = BuildFont(spec);
        String error;
        if (!l.font.Open(Span<const u8>(l.bytes), 0, error) || !l.outlines.Open(l.font, error))
        {
            std::printf("    %s\n", error.CStr());
            return false;
        }
        return true;
    }

    // The outline as text: "M x y L x y Q cx cy x y C ..." with integers.
    String Text(const Outline& o)
    {
        String s;
        usize p = 0;
        auto pt = [&](const Point& q) {
            char b[48];
            std::snprintf(b, sizeof(b), " %g %g", double(q.x), double(q.y));
            s.Append(b);
        };
        for (Verb v : o.verbs)
        {
            if (!s.Empty())
                s.Append(' ');
            switch (v)
            {
            case Verb::Move: s.Append('M'), pt(o.points[p++]); break;
            case Verb::Line: s.Append('L'), pt(o.points[p++]); break;
            case Verb::Quad: s.Append('Q'), pt(o.points[p]), pt(o.points[p + 1]), p += 2; break;
            case Verb::Cubic: s.Append('C'), pt(o.points[p]), pt(o.points[p + 1]), pt(o.points[p + 2]), p += 3; break;
            }
        }
        return s;
    }

    bool Is(const Outlines& outlines, u32 glyph, StringView expected)
    {
        Outline o;
        String error;
        if (!outlines.Get(glyph, o, error))
        {
            std::printf("    glyph %u: %s\n", glyph, error.CStr());
            return false;
        }
        String text = Text(o);
        if (text == expected)
            return true;
        std::printf("    glyph %u:\n      %s\n    expected\n      %.*s\n", glyph, text.CStr(), int(expected.Size()), expected.Data());
        return false;
    }
}

HX_TEST(Outline, TrueTypeContours)
{
    Loaded l;
    HX_REQUIRE(Load(TrueTypeSpec(), l));
    HX_CHECK(Is(l.outlines, 0, ""));
    HX_CHECK(Is(l.outlines, 1, ""));
    HX_CHECK(Is(l.outlines, 2, "M 50 0 L 50 700 L 550 700 L 550 0 L 50 0 M 150 100 L 450 100 L 450 600 L 150 600 L 150 100"));
    // Only off-curve points: on-curve points implied between each two.
    HX_CHECK(Is(l.outlines, 3, "M 450 175 Q 300 0 150 175 Q 0 350 150 525 Q 300 700 450 525 Q 600 350 450 175"));
    HX_CHECK(Is(l.outlines, 4, "M 50 0 L 50 700 L 300 700 Q 550 700 550 350 Q 550 0 300 0 L 50 0"));
    Outline o;
    String error;
    HX_REQUIRE(l.outlines.Get(3, o, error));
    HX_CHECK(o.ContourCount() == 1u && o.xMin == 0 && o.xMax == 600 && o.yMin == 0 && o.yMax == 700);
    HX_CHECK(!l.outlines.Get(8, o, error) && error == StringView("glyph 8 of 8"));
}

HX_TEST(Outline, CompositeGlyphs)
{
    Loaded l;
    HX_REQUIRE(Load(TrueTypeSpec(), l));
    HX_CHECK(Is(l.outlines, 6,
                "M 50 0 L 50 700 L 550 700 L 550 0 L 50 0 M 150 100 L 450 100 L 450 600 L 150 600 L 150 100 "
                "M 250 750 L 250 850 L 350 850 L 350 750 L 250 750"));
    // x' = 0.5 x - 0.5 y + 400, y' = 0.5 x + 0.5 y.
    HX_CHECK(Is(l.outlines, 7, "M 425 25 L 75 375 L 200 500 Q 325 625 500 450 Q 675 275 550 150 L 425 25"));

    FontSpec s = TrueTypeSpec();
    s.glyphs.Resize(11);
    // Point matching: the acute's point 0 on A's point 2.
    Component a{2};
    Component matched;
    matched.glyph = 5;
    matched.matchPoints = true;
    matched.parentPoint = 2;
    matched.childPoint = 0;
    s.glyphs[8].components = {a, matched};
    // A scaled offset: x and y scales of 1.5 and 0.5 (2.14 numbers stay below 2), offset (10, 10) scaled with them.
    Component scaled;
    scaled.glyph = 5;
    scaled.transform = 2;
    scaled.a = 1.5f, scaled.d = 0.5f;
    scaled.dx = 10, scaled.dy = 10;
    scaled.scaledOffset = true;
    s.glyphs[9].components = {scaled};
    // A composite that holds itself.
    s.glyphs[10].components = {Component{10}};
    HX_REQUIRE(Load(s, l));
    HX_CHECK(Is(l.outlines, 8,
                "M 50 0 L 50 700 L 550 700 L 550 0 L 50 0 M 150 100 L 450 100 L 450 600 L 150 600 L 150 100 "
                "M 550 700 L 550 800 L 650 800 L 650 700 L 550 700"));
    HX_CHECK(Is(l.outlines, 9, "M 15 5 L 15 55 L 165 55 L 165 5 L 15 5"));
    Outline o;
    String error;
    HX_CHECK(!l.outlines.Get(10, o, error) && error.View().EndsWith("composite glyphs nested too deep") && o.Empty());

    // Long loca offsets read the same glyphs.
    FontSpec wide = TrueTypeSpec();
    wide.longLoca = true;
    HX_REQUIRE(Load(wide, l));
    HX_CHECK(Is(l.outlines, 4, "M 50 0 L 50 700 L 300 700 Q 550 700 550 350 Q 550 0 300 0 L 50 0"));
}

HX_TEST(Outline, CffCharstrings)
{
    Loaded l;
    HX_REQUIRE(Load(CffSpec(), l));
    HX_CHECK(l.font.IsCff());
    HX_CHECK(Is(l.outlines, 0, ""));
    // The width before rmoveto is not a coordinate.
    HX_CHECK(Is(l.outlines, 1, "M 50 0 L 150 0 L 150 100 L 50 100"));
    HX_CHECK(Is(l.outlines, 2,
                "M 0 0 C 10 20 40 60 90 120 C 100 120 120 150 120 190 C 120 200 140 230 180 235 C 190 242 210 272 250 272 "
                "C 250 282 270 312 270 352"));
    HX_CHECK(Is(l.outlines, 3, "M 0 0 L 100 0 L 100 100"));
}

HX_TEST(Outline, CffOperators)
{
    FontSpec s = CffSpec();
    s.glyphs.Resize(9);
    // Hints: two hstems and a vstem with a width, then a hintmask of one byte (3 stems).
    s.glyphs[4].charstring = Charstring().Num(500).Num(0).Num(10).Num(20).Num(10).Op(1).Num(0).Num(5).Op(3).Op(19).Op(0xE0).Num(5).Num(5).Op(21).Num(10).Op(6).Op(14).bytes;
    // Arithmetic: 3 4 add (7), 2 mul (14), dup, add (28), 4 div (7), neg (-7), abs (7); 7 0 rmoveto, then hlineto 1.
    s.glyphs[5].charstring =
        Charstring().Num(3).Num(4).Op12(10).Num(2).Op12(24).Op12(27).Op12(10).Num(4).Op12(12).Op12(14).Op12(9).Num(0).Op(21).Num(1).Op(6).Op(14).bytes;
    // put / get, exch, index, roll and ifelse: computes (1, 2) and draws a line to it.
    s.glyphs[6].charstring = Charstring()
                                 .Num(0)
                                 .Num(0)
                                 .Op(21)
                                 .Num(2)
                                 .Num(0)
                                 .Op12(20) // transient[0] = 2
                                 .Num(1)
                                 .Num(0)
                                 .Op12(21) // 1 2
                                 .Op12(28) // 2 1
                                 .Op12(28) // 1 2
                                 .Num(9)
                                 .Num(0)
                                 .Op12(29) // 1 2 9 9
                                 .Op12(18) // 1 2 9
                                 .Op12(18) // 1 2
                                 .Num(5)
                                 .Num(6)
                                 .Num(1)
                                 .Num(2)
                                 .Op12(22) // 1 2 5 (1 <= 2)
                                 .Op12(18) // 1 2
                                 .Num(2)
                                 .Num(1)
                                 .Op12(30) // roll by 1: 2 1
                                 .Op12(28) // 1 2
                                 .Op(5)
                                 .Op(14)
                                 .bytes;
    // flex and hflex.
    s.glyphs[7].charstring = Charstring()
                                 .Num(0)
                                 .Num(0)
                                 .Op(21)
                                 .Num(1)
                                 .Num(2)
                                 .Num(3)
                                 .Num(4)
                                 .Num(5)
                                 .Num(6)
                                 .Num(7)
                                 .Num(8)
                                 .Num(9)
                                 .Num(10)
                                 .Num(11)
                                 .Num(12)
                                 .Num(50)
                                 .Op12(35)
                                 .Num(1)
                                 .Num(2)
                                 .Num(3)
                                 .Num(4)
                                 .Num(5)
                                 .Num(6)
                                 .Num(7)
                                 .Op12(34)
                                 .Op(14)
                                 .bytes;
    // A fixed point number and rcurveline / rlinecurve.
    s.glyphs[8].charstring = Charstring()
                                 .Fixed(0.5f)
                                 .Num(0)
                                 .Op(21)
                                 .Num(1)
                                 .Num(1)
                                 .Num(1)
                                 .Num(1)
                                 .Num(1)
                                 .Num(1)
                                 .Num(10)
                                 .Num(0)
                                 .Op(24)
                                 .Num(0)
                                 .Num(10)
                                 .Num(1)
                                 .Num(1)
                                 .Num(1)
                                 .Num(1)
                                 .Num(1)
                                 .Num(1)
                                 .Op(25)
                                 .Op(14)
                                 .bytes;
    Loaded l;
    HX_REQUIRE(Load(s, l));
    HX_CHECK(Is(l.outlines, 4, "M 5 5 L 15 5"));
    HX_CHECK(Is(l.outlines, 5, "M 7 0 L 8 0"));
    HX_CHECK(Is(l.outlines, 6, "M 0 0 L 1 2"));
    HX_CHECK(Is(l.outlines, 7,
                "M 0 0 C 1 2 4 6 9 12 C 16 20 25 30 36 42 C 37 42 39 45 43 45 C 48 45 54 42 61 42"));
    HX_CHECK(Is(l.outlines, 8, "M 0.5 0 C 1.5 1 2.5 2 3.5 3 L 13.5 3 L 13.5 13 C 14.5 14 15.5 15 16.5 16"));
}

HX_TEST(Outline, CffSeacAndCid)
{
    // seac: with the predefined charset, glyph g has string id g; 'A' is code 65, string id 34, and acute code 194,
    // string id 125.
    FontSpec s = CffSpec();
    s.glyphs.Resize(127);
    for (u32 g = 4; g < 127; g++)
        s.glyphs[g].charstring = Charstring().Op(14).bytes;
    s.glyphs[34].charstring = Charstring().Num(0).Num(0).Op(21).Num(100).Op(6).Op(14).bytes;
    s.glyphs[125].charstring = Charstring().Num(0).Num(0).Op(21).Num(0).Num(50).Op(5).Op(14).bytes;
    s.glyphs[126].charstring = Charstring().Num(30).Num(700).Num(65).Num(194).Op(14).bytes;
    Loaded l;
    HX_REQUIRE(Load(s, l));
    HX_CHECK(Is(l.outlines, 126, "M 0 0 L 100 0 M 30 700 L 30 750"));

    // CID keyed: glyphs 0 and 1 use font DICT 0, the others 1; both reach the local subroutines.
    FontSpec cid = CffSpec();
    cid.cidFds = {0, 0, 1, 1};
    HX_REQUIRE(Load(cid, l));
    HX_CHECK(Is(l.outlines, 1, "M 50 0 L 150 0 L 150 100 L 50 100"));
    HX_CHECK(Is(l.outlines, 3, "M 0 0 L 100 0 L 100 100"));
}

HX_TEST(Outline, DamagedCharstringsAreErrors)
{
    FontSpec s = CffSpec();
    s.glyphs.Resize(8);
    // A subroutine that calls itself, a call of a missing subroutine, an unknown operator, a stack overflow.
    s.localSubrs.Push(Charstring().Num(-106).Op(10).Op(11).bytes);
    s.glyphs[4].charstring = Charstring().Num(-106).Op(10).Op(14).bytes;
    s.glyphs[5].charstring = Charstring().Num(50).Op(10).Op(14).bytes;
    s.glyphs[6].charstring = Charstring().Op(2).bytes;
    Charstring deep;
    for (int i = 0; i < 49; i++)
        deep.Num(1);
    s.glyphs[7].charstring = deep.Op(14).bytes;
    Loaded l;
    HX_REQUIRE(Load(s, l));
    Outline o;
    String error;
    HX_CHECK(!l.outlines.Get(4, o, error) && error == StringView("glyph 4: subroutines nested too deep"));
    HX_CHECK(!l.outlines.Get(5, o, error) && error == StringView("glyph 5: a call of a missing subroutine"));
    HX_CHECK(!l.outlines.Get(6, o, error) && error == StringView("glyph 6: charstring operator 2"));
    HX_CHECK(!l.outlines.Get(7, o, error) && error == StringView("glyph 7: a charstring stack overflow"));
}
