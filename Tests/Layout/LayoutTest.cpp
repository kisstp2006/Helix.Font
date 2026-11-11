#include <Support/SampleLayout.h>

#include <Helix/Font/Layout/Layout.h>
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
    };

    bool Load(Loaded& l, const Array<u8>& gsub, const Array<u8>& gpos)
    {
        FontSpec s = TrueTypeSpec();
        s.AddTable("GSUB", gsub);
        s.AddTable("GPOS", gpos);
        s.AddTable("GDEF", SampleGdef());
        l.bytes = BuildFont(s);
        String error;
        if (!l.font.Open(Span<const u8>(l.bytes), 0, error))
        {
            std::printf("    %s\n", error.CStr());
            return false;
        }
        return true;
    }

    Array<u16> Glyphs(const Layout& l, LayoutList list)
    {
        Array<u16> out;
        for (u32 i = 0; i < list.count; i++)
            out.Push(l.u16s[list.begin + i]);
        return out;
    }

    bool Same(const Array<u16>& a, std::initializer_list<u16> b)
    {
        if (a.Size() != b.size())
            return false;
        usize i = 0;
        for (u16 v : b)
            if (a[i++] != v)
                return false;
        return true;
    }
}

HX_TEST(Layout, ScriptsFeaturesAndLookups)
{
    Loaded f;
    HX_REQUIRE(Load(f, SampleGsub(), SampleGpos()));
    Layout g;
    String error;
    HX_REQUIRE(g.Read(f.font, Tag("GSUB"), error));
    HX_CHECK(!g.gpos && g.scripts.Size() == 1u && g.features.Size() == 2u && g.lookups.Size() == 10u);
    const LayoutScript* latn = g.FindScript(Tag("latn"));
    HX_REQUIRE(latn != nullptr && latn->defaultLanguage != kNoIndex && latn->languages.count == 1u);
    const LayoutLanguage& def = g.languages[latn->defaultLanguage];
    const LayoutLanguage& trk = g.languages[latn->languages.begin];
    HX_CHECK(def.requiredFeature == 0xFFFF && def.features.count == 2u && trk.tag == Tag("TRK ") && trk.requiredFeature == 1);
    HX_CHECK(g.indices[trk.features.begin] == 0);
    const LayoutFeature* liga = g.FindFeature(Tag("liga"));
    HX_REQUIRE(liga && liga->lookups.count == 10u);
    HX_CHECK(g.FindFeature(Tag("kern"))->lookups.count == 1u && g.FindFeature(Tag("smcp")) == nullptr && g.FindScript(Tag("cyrl")) == nullptr);
    // The extension is resolved to the type it holds.
    HX_CHECK(g.lookups[6].type == 1 && g.lookups[8].flags == 0x10 && g.lookups[8].markFilteringSet == 3);
}

HX_TEST(Layout, EverySubstitutionType)
{
    Loaded f;
    HX_REQUIRE(Load(f, SampleGsub(), SampleGpos()));
    Layout g;
    String error;
    HX_REQUIRE(g.Read(f.font, Tag("GSUB"), error));
    auto sub = [&](u32 lookup) -> const LayoutSubtable& { return g.subtables[g.lookups[lookup].subtables.begin]; };

    const LayoutSubtable& single = sub(0);
    HX_CHECK(single.format == 1 && single.delta == 5 && g.CoverageIndex(single.coverage, 11) == 1u && g.CoverageIndex(single.coverage, 13) == kNoIndex);
    const LayoutSubtable& list = sub(1);
    HX_CHECK(list.format == 2 && Same(Glyphs(g, list.data), {30, 31}) && g.CoverageIndex(list.coverage, 22) == 1u && g.CoverageIndex(list.coverage, 21) == kNoIndex);
    HX_CHECK(Same(Glyphs(g, g.lists[sub(2).data.begin]), {41, 42, 43}));
    const LayoutSubtable& alt = sub(3);
    HX_CHECK(alt.data.count == 2u && Same(Glyphs(g, g.lists[alt.data.begin]), {60, 61}) && Same(Glyphs(g, g.lists[alt.data.begin + 1]), {62}));
    HX_CHECK(g.CoverageIndex(alt.coverage, 51) == 1u);
    const LayoutList& ligs = g.lists[sub(4).data.begin];
    HX_REQUIRE(ligs.count == 2u);
    HX_CHECK(g.ligatures[ligs.begin].glyph == 80 && Same(Glyphs(g, g.ligatures[ligs.begin].components), {71, 72}));
    HX_CHECK(g.ligatures[ligs.begin + 1].glyph == 81 && Same(Glyphs(g, g.ligatures[ligs.begin + 1].components), {71}));

    const LayoutSubtable& chained = sub(5);
    HX_REQUIRE(chained.format == 3 && chained.data.count == 1u);
    const SequenceRule& r = g.rules[chained.data.begin];
    HX_CHECK(r.backtrack.count == 1u && r.input.count == 2u && r.lookahead.count == 1u && r.records.count == 2u);
    HX_CHECK(g.CoverageIndex(g.u16s[r.input.begin + 1], 3) == 0u && g.CoverageIndex(g.u16s[r.lookahead.begin], 4) == 0u);
    HX_CHECK(g.records[r.records.begin + 1].sequenceIndex == 1 && g.records[r.records.begin + 1].lookupIndex == 1);
    HX_CHECK(chained.coverage == g.u16s[r.input.begin]);

    HX_CHECK(sub(6).delta == -1 && g.CoverageIndex(sub(6).coverage, 90) == 0u && g.lookups[6].subtables.count == 2u);
    const LayoutSubtable& second = g.subtables[g.lookups[6].subtables.begin + 1];
    HX_CHECK(second.type == 1 && second.delta == -2 && g.CoverageIndex(second.coverage, 91) == 0u);
    const LayoutSubtable& reverse = sub(7);
    HX_CHECK(reverse.type == 8 && Same(Glyphs(g, reverse.data2), {110, 111}) && g.rules[reverse.data.begin].backtrack.count == 1u);

    const LayoutSubtable& classes = sub(8);
    HX_CHECK(classes.format == 2 && g.ClassOf(classes.classDef1, 6) == 2 && g.ClassOf(classes.classDef1, 7) == 0 && classes.data.count == 3u);
    HX_CHECK(g.lists[classes.data.begin].count == 0u && g.lists[classes.data.begin + 1].count == 1u);
    const SequenceRule& cr = g.rules[g.lists[classes.data.begin + 1].begin];
    HX_CHECK(Same(Glyphs(g, cr.input), {2}) && cr.records.count == 1u);

    const LayoutSubtable& glyphs = sub(9);
    const SequenceRule& gr = g.rules[g.lists[glyphs.data.begin].begin];
    HX_CHECK(Same(Glyphs(g, gr.backtrack), {1}) && Same(Glyphs(g, gr.input), {8}) && Same(Glyphs(g, gr.lookahead), {9}));
    HX_CHECK(g.records[gr.records.begin].sequenceIndex == 1 && g.records[gr.records.begin].lookupIndex == 4);
}

HX_TEST(Layout, EveryPositioningType)
{
    Loaded f;
    HX_REQUIRE(Load(f, SampleGsub(), SampleGpos()));
    Layout p;
    String error;
    HX_REQUIRE(p.Read(f.font, Tag("GPOS"), error));
    HX_CHECK(p.gpos && p.lookups.Size() == 10u);
    auto sub = [&](u32 lookup) -> const LayoutSubtable& { return p.subtables[p.lookups[lookup].subtables.begin]; };

    const ValueRecord& v = p.values[sub(0).data.begin];
    HX_CHECK(v.xPlacement == -10 && v.xAdvance == 20 && v.yPlacement == 0);
    HX_CHECK(sub(1).data.count == 2u && p.values[sub(1).data.begin + 1].xAdvance == 6);

    const LayoutList& pairs = p.lists[sub(2).data.begin];
    HX_REQUIRE(pairs.count == 2u);
    HX_CHECK(p.pairs[pairs.begin + 1].second == 22 && p.values[p.pairs[pairs.begin + 1].value1].xAdvance == -40 && p.pairs[pairs.begin].value2 == kNoIndex);

    const LayoutSubtable& classPairs = sub(3);
    HX_CHECK(classPairs.count1 == 2u && classPairs.count2 == 2u && classPairs.data.count == 8u);
    u32 c1 = p.ClassOf(classPairs.classDef1, 31), c2 = p.ClassOf(classPairs.classDef2, 40);
    usize at = classPairs.data.begin + 2 * (usize(c1) * classPairs.count2 + c2);
    HX_CHECK(p.values[at].xAdvance == -50 && p.values[at + 1].xPlacement == 7);

    const LayoutSubtable& cursive = sub(4);
    HX_CHECK(p.anchors[cursive.data.begin].present && p.anchors[cursive.data.begin].y == 20 && !p.anchors[cursive.data.begin + 1].present);

    const LayoutSubtable& markBase = sub(5);
    HX_CHECK(markBase.count1 == 2u && markBase.data.count == 2u && markBase.data2.count == 2u);
    const MarkRecord& second = p.marks[markBase.data.begin + 1];
    HX_CHECK(second.markClass == 1 && p.anchors[second.anchor].point == 5 && p.anchors[second.anchor].x == 3);
    HX_CHECK(p.anchors[markBase.data2.begin].x == 100 && !p.anchors[markBase.data2.begin + 1].present);
    HX_CHECK(p.CoverageIndex(markBase.coverage2, 70) == 0u);

    const LayoutSubtable& markLig = sub(6);
    const LayoutList& components = p.lists[markLig.data2.begin];
    HX_CHECK(components.count == 2u && p.anchors[components.begin + 1].x == 20);
    const LayoutSubtable& markMark = sub(7);
    HX_CHECK(p.anchors[markMark.data2.begin].y == 2 && p.anchors[p.marks[markMark.data.begin].anchor].x == 1);

    HX_CHECK(p.lookups[8].type == 2 && p.lists[sub(8).data.begin].count == 2u);
    const LayoutSubtable& context = sub(9);
    HX_CHECK(context.format == 3 && p.records[p.rules[context.data.begin].records.begin].lookupIndex == 2);
}

HX_TEST(Layout, GlyphDefinitions)
{
    Loaded f;
    HX_REQUIRE(Load(f, SampleGsub(), SampleGpos()));
    GlyphDefinitions d;
    String error;
    HX_REQUIRE(d.Read(f.font, error));
    HX_CHECK(d.GlyphClass(60) == 3 && d.GlyphClass(61) == 3 && d.GlyphClass(70) == 1 && d.GlyphClass(5) == 0);
    HX_CHECK(d.MarkAttachClass(60) == 1 && d.MarkAttachClass(61) == 0);
    HX_CHECK(d.MarkSetCount() == 1u && d.InMarkSet(0, 60) && !d.InMarkSet(0, 61) && !d.InMarkSet(1, 60));
}

HX_TEST(Layout, DamagedTablesAreErrors)
{
    Array<u8> good = SampleGsub();
    Array<u8> v2 = good;
    v2[1] = 2;
    Loaded f;
    HX_REQUIRE(Load(f, v2, SampleGpos()));
    Layout g;
    String error;
    HX_CHECK(!g.Read(f.font, Tag("GSUB"), error) && error == StringView("GSUB: layout table version 2.0") && g.Empty());
    HX_CHECK(Load(f, Array<u8>(), SampleGpos()) && g.Read(f.font, Tag("GSUB"), error) && g.Empty());
    // Every cut of the table reads or is refused, never outside.
    for (usize n = 0; n < good.Size(); n += 2)
    {
        Array<u8> cut;
        cut.Append(good.Data(), n);
        if (!Load(f, cut, SampleGpos()))
            continue;
        Layout l;
        String e;
        l.Read(f.font, Tag("GSUB"), e);
    }
}
