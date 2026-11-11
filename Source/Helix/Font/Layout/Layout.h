#pragma once

#include <Helix/Core/Containers/HashMap.h>
#include <Helix/Font/Font/Font.h>

// GSUB, GPOS and GDEF read into flat arrays. Every list is a range (begin, count) into one of the pools of its Layout,
// so a lookup holds no pointers into the font and costs no allocation per subtable. Applying the lookups (shaping) is
// done by the text shaper; this is the data it walks.
namespace helix::font
{
    inline constexpr u32 kNoIndex = ~0u;

    struct LayoutList
    {
        u32 begin = 0;
        u32 count = 0;
    };

    // A run of glyphs of a coverage (value: the coverage index of `first`) or a class definition (value: the class).
    struct GlyphRange
    {
        u16 first = 0, last = 0;
        u16 value = 0;
    };

    struct LayoutScript
    {
        u32 tag = 0;
        // Into languages; kNoIndex when the script has no default language system.
        u32 defaultLanguage = kNoIndex;
        LayoutList languages;
    };

    struct LayoutLanguage
    {
        u32 tag = 0;
        // A feature index, 0xFFFF for none.
        u16 requiredFeature = 0xFFFF;
        // Feature indices in indices.
        LayoutList features;
    };

    struct LayoutFeature
    {
        u32 tag = 0;
        // Lookup indices in indices.
        LayoutList lookups;
    };

    struct LayoutLookup
    {
        // The subtable type (extensions resolved to the type they hold).
        u16 type = 0;
        u16 flags = 0;
        u16 markFilteringSet = 0xFFFF;
        // Into subtables.
        LayoutList subtables;
    };

    struct ValueRecord
    {
        i16 xPlacement = 0, yPlacement = 0, xAdvance = 0, yAdvance = 0;
    };

    struct Anchor
    {
        i16 x = 0, y = 0;
        // The contour point of format 2, 0xFFFF for none.
        u16 point = 0xFFFF;
        bool present = false;
    };

    struct LigatureEntry
    {
        u16 glyph = 0;
        // The components after the first, in u16s.
        LayoutList components;
    };

    // A rule of a (chained) context lookup: glyphs, classes or coverage indices in u16s, and its lookup records.
    struct SequenceRule
    {
        LayoutList backtrack, input, lookahead;
        LayoutList records;
    };

    struct LookupRecord
    {
        u16 sequenceIndex = 0;
        u16 lookupIndex = 0;
    };

    struct PairValue
    {
        u16 second = 0;
        // Into values; kNoIndex for an empty value format.
        u32 value1 = kNoIndex, value2 = kNoIndex;
    };

    struct MarkRecord
    {
        u16 markClass = 0;
        // Into anchors.
        u32 anchor = kNoIndex;
    };

    // One subtable. What `data` and `data2` hold depends on the table, type and format:
    //
    //   GSUB 1   format 1: delta; format 2: data = substitute glyphs (u16s), one per coverage index
    //   GSUB 2/3 data = lists (one per coverage index) of glyphs in u16s
    //   GSUB 4   data = lists (one per coverage index) of ligatures
    //   GSUB 5/6, GPOS 7/8
    //            format 1: data = lists (one per coverage index) of rules, glyphs in their sequences
    //            format 2: data = lists (one per input class) of rules, classes in their sequences; classDef1 = input,
    //                      classDef2 = backtrack, classDef3 = lookahead (chained)
    //            format 3: data = one rule, coverage indices (into coverages) in its sequences
    //   GSUB 8   data = one rule with backtrack and lookahead coverage indices; data2 = substitutes (u16s)
    //   GPOS 1   format 1: data = one value; format 2: data = values, one per coverage index
    //   GPOS 2   format 1: data = lists (one per coverage index) of pairs; format 2: classDef1, classDef2, count1 x count2
    //            classes, data = values, two per class pair (kNoIndex filled: see valueFormat)
    //   GPOS 3   data = anchors, entry and exit per coverage index
    //   GPOS 4/6 coverage = marks, coverage2 = bases (or marks), count1 = mark classes, data = marks (one per mark),
    //            data2 = anchors, count1 per base
    //   GPOS 5   coverage = marks, coverage2 = ligatures, count1 = mark classes, data = marks, data2 = lists (one per
    //            ligature) of anchors, count1 per component
    struct LayoutSubtable
    {
        u16 type = 0, format = 0;
        u32 coverage = kNoIndex, coverage2 = kNoIndex;
        u32 classDef1 = kNoIndex, classDef2 = kNoIndex, classDef3 = kNoIndex;
        i32 delta = 0;
        u32 count1 = 0, count2 = 0;
        u16 valueFormat1 = 0, valueFormat2 = 0;
        LayoutList data, data2;
    };

    // GSUB or GPOS.
    class Layout
    {
    public:
        // False with an error for a damaged table; a font without the table reads as empty.
        bool Read(const Font& font, u32 tag, String& error);
        bool Empty() const { return lookups.Empty(); }

        // The coverage index of a glyph, kNoIndex when the coverage does not hold it.
        u32 CoverageIndex(u32 coverage, u32 glyph) const;
        // The class of a glyph, 0 when the definition does not list it.
        u16 ClassOf(u32 classDef, u32 glyph) const;
        const LayoutScript* FindScript(u32 tag) const;
        const LayoutFeature* FindFeature(u32 tag) const;

        bool gpos = false;
        Array<LayoutScript> scripts;
        Array<LayoutLanguage> languages;
        Array<LayoutFeature> features;
        Array<LayoutLookup> lookups;
        Array<LayoutSubtable> subtables;

        Array<u16> u16s;
        // Ranges of glyphs into ranges, one per coverage and per class definition.
        Array<LayoutList> coverages;
        Array<LayoutList> classDefs;
        Array<GlyphRange> ranges;
        // Lists of lists: rule sets, ligature sets, pair sets, sequences, alternates, ligature anchors.
        Array<LayoutList> lists;
        Array<LigatureEntry> ligatures;
        Array<SequenceRule> rules;
        Array<LookupRecord> records;
        Array<ValueRecord> values;
        Array<PairValue> pairs;
        Array<Anchor> anchors;
        Array<MarkRecord> marks;
        // Feature and lookup indices.
        Array<u16> indices;

    private:
        friend struct LayoutReader;
    };

    // GDEF: glyph classes (1 base, 2 ligature, 3 mark, 4 component), mark attachment classes and mark glyph sets.
    class GlyphDefinitions
    {
    public:
        bool Read(const Font& font, String& error);

        u16 GlyphClass(u32 glyph) const { return Class(glyphClasses_, glyph); }
        u16 MarkAttachClass(u32 glyph) const { return Class(markClasses_, glyph); }
        u32 MarkSetCount() const { return u32(markSets_.Size()); }
        bool InMarkSet(u32 set, u32 glyph) const;

    private:
        static u16 Class(const Array<GlyphRange>& ranges, u32 glyph);

        Array<GlyphRange> glyphClasses_;
        Array<GlyphRange> markClasses_;
        Array<Array<GlyphRange>> markSets_;
    };

    // Reads a coverage table (format 1 or 2) into sorted ranges, or a class definition (format 1 or 2); false when damaged.
    bool ReadCoverage(Bytes table, Array<GlyphRange>& out);
    bool ReadClassDef(Bytes table, Array<GlyphRange>& out);
}
