#pragma once

#include <Support/LayoutWriter.h>
#include <Support/SampleFonts.h>

#include <Helix/Image/Codecs/Png.h>

// Layout and color tables the tests and fuzz targets share.
namespace helix::font::testing
{
    // GSUB: one lookup of every type (0 single delta, 1 single list, 2 multiple, 3 alternate, 4 ligature, 5 chained by
    // coverage, 6 two extensions, 7 reverse, 8 context by class with a mark filtering set, 9 chained by glyph).
    inline Array<u8> SampleGsub()
    {
        Array<LookupSpec> l;
        l.Resize(10);
        Blob s;
        // 0: single, delta.
        s = Blob();
        s.U16(1).Offset16(Coverage1({10, 11, 12})).I16(5);
        l[0] = {1, 0, 0, {s.Flatten()}};
        // 1: single, substitutes.
        s = Blob();
        s.U16(2).Offset16(Coverage1({20, 22})).U16(2).U16(30).U16(31);
        l[1] = {1, 0, 0, {s.Flatten()}};
        // 2: multiple.
        Blob seq;
        seq.U16(3).U16(41).U16(42).U16(43);
        s = Blob();
        s.U16(1).Offset16(Coverage1({40})).U16(1).Offset16(seq);
        l[2] = {2, 0, 0, {s.Flatten()}};
        // 3: alternates for a range of two glyphs.
        Blob alt0, alt1;
        alt0.U16(2).U16(60).U16(61);
        alt1.U16(1).U16(62);
        s = Blob();
        s.U16(1).Offset16(Coverage2({{50, 51}})).U16(2).Offset16(alt0).Offset16(alt1);
        l[3] = {3, 0, 0, {s.Flatten()}};
        // 4: ligatures 70 71 72 -> 80 and 70 71 -> 81.
        Blob lig0, lig1, set;
        lig0.U16(80).U16(3).U16(71).U16(72);
        lig1.U16(81).U16(2).U16(71);
        set.U16(2).Offset16(lig0).Offset16(lig1);
        s = Blob();
        s.U16(1).Offset16(Coverage1({70})).U16(1).Offset16(set);
        l[4] = {4, 0, 0, {s.Flatten()}};
        // 5: chained context by coverages: backtrack 1, input 2 3, lookahead 4.
        s = Blob();
        s.U16(3).U16(1).Offset16(Coverage1({1})).U16(2).Offset16(Coverage1({2})).Offset16(Coverage1({3})).U16(1).Offset16(Coverage1({4}));
        s.U16(2).U16(0).U16(0).U16(1).U16(1);
        l[5] = {6, 0, 0, {s.Flatten()}};
        // 6: two extensions holding single substitutions.
        Blob inner, inner2;
        inner.U16(1).Offset16(Coverage1({90})).I16(-1);
        inner2.U16(1).Offset16(Coverage1({91})).I16(-2);
        l[6] = {7, 0, 0, {Extension(1, inner.Flatten()), Extension(1, inner2.Flatten())}};
        // 7: reverse chaining.
        s = Blob();
        s.U16(1).Offset16(Coverage1({100, 101})).U16(1).Offset16(Coverage1({1})).U16(0).U16(2).U16(110).U16(111);
        l[7] = {8, 0, 0, {s.Flatten()}};
        // 8: context by classes (5 is class 1, 6 class 2), with a mark filtering set.
        Blob rule, classSet;
        rule.U16(2).U16(1).U16(2).U16(0).U16(0);
        classSet.U16(1).Offset16(rule);
        s = Blob();
        s.U16(2).Offset16(Coverage1({5, 6})).Offset16(ClassDef2({{5, 5, 1}, {6, 6, 2}})).U16(3).U16(0).Offset16(classSet).U16(0);
        l[8] = {5, 0x10, 3, {s.Flatten()}};
        // 9: chained context by glyphs: backtrack 1, input 7 8, lookahead 9.
        Blob chainRule, chainSet;
        chainRule.U16(1).U16(1).U16(2).U16(8).U16(1).U16(9).U16(1).U16(1).U16(4);
        chainSet.U16(1).Offset16(chainRule);
        s = Blob();
        s.U16(1).Offset16(Coverage1({7})).U16(1).Offset16(chainSet);
        l[9] = {6, 0, 0, {s.Flatten()}};
        return LayoutTable(l);
    }

    // GPOS: 0 and 1 single, 2 pairs by glyph, 3 pairs by class, 4 cursive, 5 mark to base, 6 mark to ligature, 7 mark to
    // mark, 8 an extension, 9 context by coverage.
    inline Array<u8> SampleGpos()
    {
        Array<LookupSpec> l;
        l.Resize(10);
        Blob s;
        // 0: single, one value (x placement and x advance).
        s.U16(1).Offset16(Coverage1({10})).U16(0x5).I16(-10).I16(20);
        l[0] = {1, 0, 0, {s.Flatten()}};
        // 1: single, a value per glyph.
        s = Blob();
        s.U16(2).Offset16(Coverage1({11, 12})).U16(0x4).U16(2).I16(5).I16(6);
        l[1] = {1, 0, 0, {s.Flatten()}};
        // 2: pairs by glyph.
        Blob pairSet;
        pairSet.U16(2).U16(21).I16(-30).U16(22).I16(-40);
        s = Blob();
        s.U16(1).Offset16(Coverage1({20})).U16(0x4).U16(0).U16(1).Offset16(pairSet);
        l[2] = {2, 0, 0, {s.Flatten()}};
        // 3: pairs by class: classes 1 x 1 hold (x advance -50, then x placement 7).
        s = Blob();
        s.U16(2).Offset16(Coverage1({30, 31})).U16(0x4).U16(0x1).Offset16(ClassDef2({{30, 31, 1}})).Offset16(ClassDef2({{40, 40, 1}})).U16(2).U16(2);
        s.I16(0).I16(0).I16(0).I16(0).I16(0).I16(0).I16(-50).I16(7);
        l[3] = {2, 0, 0, {s.Flatten()}};
        // 4: cursive: an entry, no exit.
        s = Blob();
        s.U16(1).Offset16(Coverage1({50})).U16(1).Offset16(Anchor1(10, 20)).U16(0);
        l[4] = {3, 0, 0, {s.Flatten()}};
        // 5: marks on bases: two mark classes, the second has no anchor on the base.
        Blob anchor2;
        anchor2.U16(2).I16(3).I16(4).U16(5);
        Blob marks, bases;
        marks.U16(2).U16(0).Offset16(Anchor1(1, 2)).U16(1).Offset16(anchor2);
        bases.U16(1).Offset16(Anchor1(100, 200)).U16(0);
        s = Blob();
        s.U16(1).Offset16(Coverage1({60, 61})).Offset16(Coverage1({70})).U16(2).Offset16(marks).Offset16(bases);
        l[5] = {4, 0, 0, {s.Flatten()}};
        // 6: marks on a ligature of two components.
        Blob ligMarks, attach, ligs;
        ligMarks.U16(1).U16(0).Offset16(Anchor1(0, 0));
        attach.U16(2).Offset16(Anchor1(10, 10)).Offset16(Anchor1(20, 20));
        ligs.U16(1).Offset16(attach);
        s = Blob();
        s.U16(1).Offset16(Coverage1({60})).Offset16(Coverage1({80})).U16(1).Offset16(ligMarks).Offset16(ligs);
        l[6] = {5, 0, 0, {s.Flatten()}};
        // 7: marks on marks.
        Blob m1, m2;
        m1.U16(1).U16(0).Offset16(Anchor1(1, 1));
        m2.U16(1).Offset16(Anchor1(2, 2));
        s = Blob();
        s.U16(1).Offset16(Coverage1({60})).Offset16(Coverage1({61})).U16(1).Offset16(m1).Offset16(m2);
        l[7] = {6, 0, 0, {s.Flatten()}};
        // 8: an extension holding pairs by glyph.
        l[8] = {9, 0, 0, {Extension(2, l[2].subtables[0])}};
        // 9: context by coverage: input 20, apply lookup 2 at 0.
        s = Blob();
        s.U16(3).U16(1).U16(1).Offset16(Coverage1({20})).U16(0).U16(2);
        l[9] = {7, 0, 0, {s.Flatten()}};
        return LayoutTable(l);
    }

    // GDEF 1.2: glyph classes, mark attachment classes, one mark glyph set.
    inline Array<u8> SampleGdef()
    {
        Blob sets;
        sets.U16(1).U16(1).Offset32(Coverage1({60}));
        Blob gdef;
        gdef.U16(1).U16(2).Offset16(ClassDef2({{60, 61, 3}, {70, 70, 1}})).U16(0).U16(0).Offset16(ClassDef2({{60, 60, 1}})).Offset16(sets);
        return gdef.Flatten();
    }

    inline image::Image Picture(u32 w, u32 h, u8 seed)
    {
        image::Image img;
        img.Allocate(w, h, image::ImageFormat::RGBA8);
        for (usize i = 0; i < img.pixels.Size(); i++)
            img.pixels[i] = u8(i * 7 + seed);
        return img;
    }

    // Two strikes: 20 ppem (glyphs 2 and 3, index format 1, image format 17) and 40 ppem (glyph 2, index format 4,
    // image format 18).
    inline void AddBitmaps(FontSpec& s, const Array<u8>& png20, const Array<u8>& png40)
    {
        Blob cbdt;
        cbdt.U16(3).U16(0);
        usize a = cbdt.Size();
        cbdt.U8(3).U8(4).U8(1).U8(3).U8(5).U32(u32(png20.Size())).Bytes(png20);
        usize b = cbdt.Size();
        cbdt.U8(3).U8(4).U8(1).U8(3).U8(6).U32(u32(png20.Size())).Bytes(png20);
        usize c = cbdt.Size();
        cbdt.U8(6).U8(8).U8(2).U8(6).U8(10).U8(0).U8(0).U8(0).U32(u32(png40.Size())).Bytes(png40);
        usize d = cbdt.Size();

        auto size = [](Blob& bl, u32 arrayOffset, u16 first, u16 last, u8 ppem) {
            bl.U32(arrayOffset).U32(0).U32(1).U32(0);
            for (int i = 0; i < 24; i++)
                bl.U8(0);
            bl.U16(first).U16(last).U8(ppem).U8(ppem).U8(32).U8(1);
        };
        Blob cblc;
        cblc.U16(3).U16(0).U32(2);
        u32 array20 = 8 + 2 * 48, array40 = array20 + 8 + 20;
        size(cblc, array20, 2, 3, 20);
        size(cblc, array40, 2, 2, 40);
        // Strike 20: the array, then a format 1 subtable with three offsets.
        cblc.U16(2).U16(3).U32(8);
        cblc.U16(1).U16(17).U32(u32(a)).U32(0).U32(u32(b - a)).U32(u32(c - a));
        // Strike 40: the array, then a format 4 subtable.
        cblc.U16(2).U16(2).U32(8);
        cblc.U16(4).U16(18).U32(u32(c)).U32(1).U16(2).U16(0).U16(0).U16(u32(d - c));
        s.AddTable("CBDT", cbdt.bytes);
        s.AddTable("CBLC", cblc.bytes);
    }

    // COLR / CPAL: glyph 6 drawn as glyph 2 in palette color 0 and glyph 5 in the text color; palettes red and blue.
    inline void AddColorLayers(FontSpec& s)
    {
        Blob colr;
        colr.U16(0).U16(1).U32(14).U32(20).U16(2);
        colr.U16(6).U16(0).U16(2);
        colr.U16(2).U16(0).U16(5).U16(0xFFFF);
        Blob cpal;
        cpal.U16(0).U16(1).U16(2).U16(2).U32(16).U16(0).U16(1);
        cpal.U8(0).U8(0).U8(255).U8(255).U8(255).U8(0).U8(0).U8(255);
        s.AddTable("COLR", colr.bytes);
        s.AddTable("CPAL", cpal.bytes);
    }

    // The TrueType sample with every table: layout, GDEF, color layers and bitmaps.
    inline FontSpec FullSpec()
    {
        FontSpec s = TrueTypeSpec();
        s.AddTable("GSUB", SampleGsub());
        s.AddTable("GPOS", SampleGpos());
        s.AddTable("GDEF", SampleGdef());
        AddColorLayers(s);
        Array<u8> a, b;
        image::EncodePng(Picture(4, 3, 1), a);
        image::EncodePng(Picture(8, 6, 2), b);
        AddBitmaps(s, a, b);
        return s;
    }
}
