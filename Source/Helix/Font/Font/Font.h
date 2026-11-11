#pragma once

#include <Helix/Core/Containers/Array.h>
#include <Helix/Core/Strings/String.h>
#include <Helix/Font/Font/Bytes.h>

namespace helix::font
{
    struct TableRecord
    {
        u32 tag = 0;
        u32 checksum = 0;
        u32 offset = 0;
        u32 length = 0;
    };

    struct HeadTable
    {
        u32 fontRevision = 0;
        u16 flags = 0;
        u16 unitsPerEm = 1000;
        i16 xMin = 0, yMin = 0, xMax = 0, yMax = 0;
        u16 macStyle = 0;
        u16 lowestRecPPEM = 0;
        // 0: loca has u16 offsets / 2, 1: u32 offsets.
        i16 indexToLocFormat = 0;
    };

    struct HheaTable
    {
        i16 ascender = 0, descender = 0, lineGap = 0;
        u16 advanceWidthMax = 0;
        i16 minLeftSideBearing = 0, minRightSideBearing = 0, xMaxExtent = 0;
        i16 caretSlopeRise = 1, caretSlopeRun = 0, caretOffset = 0;
        u16 numberOfHMetrics = 0;
    };

    struct Os2Table
    {
        bool present = false;
        u16 version = 0;
        i16 xAvgCharWidth = 0;
        u16 weightClass = 400, widthClass = 5, fsType = 0;
        i16 subscriptXSize = 0, subscriptYSize = 0, subscriptXOffset = 0, subscriptYOffset = 0;
        i16 superscriptXSize = 0, superscriptYSize = 0, superscriptXOffset = 0, superscriptYOffset = 0;
        i16 strikeoutSize = 0, strikeoutPosition = 0;
        i16 familyClass = 0;
        u8 panose[10] = {};
        u32 unicodeRange[4] = {};
        u32 vendorID = 0;
        u16 fsSelection = 0, firstCharIndex = 0, lastCharIndex = 0;
        i16 typoAscender = 0, typoDescender = 0, typoLineGap = 0;
        u16 winAscent = 0, winDescent = 0;
        // Version 1 and later.
        u32 codePageRange[2] = {};
        // Version 2 and later.
        i16 xHeight = 0, capHeight = 0;
        u16 defaultChar = 0, breakChar = 0, maxContext = 0;
    };

    struct PostTable
    {
        u32 version = 0;
        float italicAngle = 0;
        i16 underlinePosition = 0, underlineThickness = 0;
        bool fixedPitch = false;
    };

    struct NameRecord
    {
        u16 platform = 0, encoding = 0, language = 0, id = 0;
        // As UTF-8.
        String text;
    };

    struct GlyphMetrics
    {
        u16 advance = 0;
        i16 leftSideBearing = 0;
    };

    // Ids of the name table.
    enum NameId : u16
    {
        kNameCopyright = 0,
        kNameFamily = 1,
        kNameSubfamily = 2,
        kNameUniqueId = 3,
        kNameFullName = 4,
        kNameVersion = 5,
        kNamePostScript = 6,
        kNameTypographicFamily = 16,
        kNameTypographicSubfamily = 17,
    };

    // A font of a TrueType, OpenType or collection file held in memory (the bytes must outlive the font). Open reads the
    // table directory, the metrics tables, the names, the glyph names and the character map; outlines, kerning, layout
    // tables and color glyphs are read by their own functions from Table().
    class Font
    {
    public:
        // The number of fonts in a file: the count of a collection, 1 for a single font, 0 when it is not a font.
        static u32 CountFonts(Span<const u8> file);

        bool Open(Span<const u8> file, u32 index, String& error);

        // The bytes of a table, empty when the font has none.
        Bytes Table(u32 tag) const;
        bool HasTable(u32 tag) const { return !Table(tag).Empty(); }
        const Array<TableRecord>& Tables() const { return tables_; }
        // The whole file (offsets of collection tables are from its start).
        Bytes File() const { return file_; }
        // TrueType outlines (glyf) or CFF.
        bool IsCff() const { return HasTable(Tag("CFF ")); }

        const HeadTable& Head() const { return head_; }
        const HheaTable& Hhea() const { return hhea_; }
        const Os2Table& Os2() const { return os2_; }
        const PostTable& Post() const { return post_; }
        u32 GlyphCount() const { return glyphCount_; }
        GlyphMetrics HMetrics(u32 glyph) const;

        const Array<NameRecord>& Names() const { return names_; }
        // The best record for an id: Windows English, then any Windows or Unicode one, then Mac Roman.
        StringView Name(u16 id) const;
        // From post format 1 or 2; empty when the font has none.
        StringView GlyphName(u32 glyph) const;

        // The glyph of a code point, 0 (.notdef) when the font has none.
        u32 GlyphIndex(u32 codepoint) const;
        // The glyph of a variation sequence (cmap format 14): its own glyph, the default glyph of the code point when the
        // sequence uses it, or 0 when the font does not list the sequence.
        u32 GlyphIndex(u32 codepoint, u32 selector) const;
        // Every mapped code point in order, with its glyph.
        void ForEachCodepoint(void (*visit)(void* context, u32 codepoint, u32 glyph), void* context) const;

    private:
        struct CmapRange
        {
            u32 first, last;
            // kDelta: codepoint + delta, kDelta16: the same mod 65536 (format 4), kList: glyphs_[delta + codepoint - first],
            // kConstant: delta for every code point (format 13).
            i64 delta;
            u8 mode;
        };
        static constexpr u8 kDelta = 0, kDelta16 = 1, kList = 2, kConstant = 3;
        struct VariationSelector
        {
            u32 selector;
            u32 defaultBegin, defaultCount;
            u32 mappingBegin, mappingCount;
        };
        struct UvsRange
        {
            u32 first, last;
        };
        struct UvsMapping
        {
            u32 codepoint;
            u32 glyph;
        };

        bool ReadCmap(String& error);
        bool ReadCmapSubtable(Bytes t, String& error);
        void ReadCmap14(Bytes t);
        void ReadNames();
        void ReadPost();
        u32 Map(u32 codepoint) const;

        Bytes file_;
        Array<TableRecord> tables_;
        HeadTable head_;
        HheaTable hhea_;
        Os2Table os2_;
        PostTable post_;
        u32 glyphCount_ = 0;
        Bytes hmtx_;
        Array<NameRecord> names_;
        // post format 2: per glyph, an offset into the font (above 0x10000) or an index of the standard names.
        Array<u32> glyphNames_;
        Array<CmapRange> cmap_;
        Array<u16> glyphs_;
        Array<VariationSelector> selectors_;
        Array<UvsRange> uvsDefault_;
        Array<UvsMapping> uvsMappings_;
    };

    // The 258 standard Macintosh glyph names of post format 1.
    StringView StandardGlyphName(u32 index);
}
