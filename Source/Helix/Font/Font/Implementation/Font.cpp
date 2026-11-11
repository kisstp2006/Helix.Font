#include <Helix/Font/Font/Font.h>

#include <Helix/Core/Strings/Format.h>

namespace helix::font
{
    namespace
    {
        String TagText(u32 tag)
        {
            char t[4] = {char(tag >> 24), char(tag >> 16), char(tag >> 8), char(tag)};
            for (char& c : t)
                if (u8(c) < 0x20 || u8(c) > 0x7E)
                    c = '?';
            return String(StringView(t, 4));
        }

        bool IsSfnt(u32 version) { return version == 0x00010000 || version == Tag("true") || version == Tag("OTTO"); }
    }

    u32 Font::CountFonts(Span<const u8> file)
    {
        Bytes b(file);
        if (b.U32(0) == Tag("ttcf"))
        {
            u32 n = b.U32(8);
            return b.Has(12, usize(n) * 4) ? n : 0;
        }
        return IsSfnt(b.U32(0)) ? 1 : 0;
    }

    bool Font::Open(Span<const u8> file, u32 index, String& error)
    {
        *this = Font();
        file_ = Bytes(file);
        Bytes& f = file_;
        u32 offset = 0;
        if (f.U32(0) == Tag("ttcf"))
        {
            u32 n = f.U32(8);
            if (!f.Has(12, usize(n) * 4))
                return error = "a collection header outside the file", false;
            if (index >= n)
                return error = Format("font {} of a collection of {}", index, n), false;
            offset = f.U32(12 + 4 * usize(index));
        }
        else if (index != 0)
            return error = Format("font {} of a file with one font", index), false;
        if (!IsSfnt(f.U32(offset)))
            return error = "not a TrueType or OpenType font", false;
        u16 count = f.U16(offset + 4);
        if (!f.Has(offset + 12, usize(count) * 16))
            return error = "a table directory outside the file", false;
        tables_.Reserve(count);
        for (u32 i = 0; i < count; i++)
        {
            usize at = offset + 12 + usize(i) * 16;
            TableRecord r{f.U32(at), f.U32(at + 4), f.U32(at + 8), f.U32(at + 12)};
            if (!f.Has(r.offset, r.length))
                return error = Format("table '{}' outside the file", TagText(r.tag)), false;
            tables_.Push(r);
        }

        Bytes head = Table(Tag("head"));
        if (head.size < 54)
            return error = "no head table", false;
        if (head.U32(12) != 0x5F0F3CF5)
            return error = "a head table without its magic number", false;
        head_.fontRevision = head.U32(4);
        head_.flags = head.U16(16);
        head_.unitsPerEm = head.U16(18);
        head_.xMin = head.I16(36);
        head_.yMin = head.I16(38);
        head_.xMax = head.I16(40);
        head_.yMax = head.I16(42);
        head_.macStyle = head.U16(44);
        head_.lowestRecPPEM = head.U16(46);
        head_.indexToLocFormat = head.I16(50);
        if (head_.unitsPerEm < 16 || head_.unitsPerEm > 16384)
            return error = Format("{} units per em", head_.unitsPerEm), false;

        Bytes maxp = Table(Tag("maxp"));
        if (maxp.size < 6)
            return error = "no maxp table", false;
        glyphCount_ = maxp.U16(4);

        Bytes hhea = Table(Tag("hhea"));
        if (!hhea.Empty())
        {
            if (hhea.size < 36)
                return error = "a hhea table shorter than 36 bytes", false;
            hhea_.ascender = hhea.I16(4);
            hhea_.descender = hhea.I16(6);
            hhea_.lineGap = hhea.I16(8);
            hhea_.advanceWidthMax = hhea.U16(10);
            hhea_.minLeftSideBearing = hhea.I16(12);
            hhea_.minRightSideBearing = hhea.I16(14);
            hhea_.xMaxExtent = hhea.I16(16);
            hhea_.caretSlopeRise = hhea.I16(18);
            hhea_.caretSlopeRun = hhea.I16(20);
            hhea_.caretOffset = hhea.I16(22);
            hhea_.numberOfHMetrics = hhea.U16(34);
        }
        hmtx_ = Table(Tag("hmtx"));

        Bytes os2 = Table(Tag("OS/2"));
        if (os2.size >= 78)
        {
            Os2Table& o = os2_;
            o.present = true;
            o.version = os2.U16(0);
            o.xAvgCharWidth = os2.I16(2);
            o.weightClass = os2.U16(4);
            o.widthClass = os2.U16(6);
            o.fsType = os2.U16(8);
            o.subscriptXSize = os2.I16(10);
            o.subscriptYSize = os2.I16(12);
            o.subscriptXOffset = os2.I16(14);
            o.subscriptYOffset = os2.I16(16);
            o.superscriptXSize = os2.I16(18);
            o.superscriptYSize = os2.I16(20);
            o.superscriptXOffset = os2.I16(22);
            o.superscriptYOffset = os2.I16(24);
            o.strikeoutSize = os2.I16(26);
            o.strikeoutPosition = os2.I16(28);
            o.familyClass = os2.I16(30);
            for (u32 i = 0; i < 10; i++)
                o.panose[i] = os2.U8(32 + i);
            for (u32 i = 0; i < 4; i++)
                o.unicodeRange[i] = os2.U32(42 + 4 * i);
            o.vendorID = os2.U32(58);
            o.fsSelection = os2.U16(62);
            o.firstCharIndex = os2.U16(64);
            o.lastCharIndex = os2.U16(66);
            o.typoAscender = os2.I16(68);
            o.typoDescender = os2.I16(70);
            o.typoLineGap = os2.I16(72);
            o.winAscent = os2.U16(74);
            o.winDescent = os2.U16(76);
            if (o.version >= 1 && os2.size >= 86)
            {
                o.codePageRange[0] = os2.U32(78);
                o.codePageRange[1] = os2.U32(82);
            }
            if (o.version >= 2 && os2.size >= 96)
            {
                o.xHeight = os2.I16(86);
                o.capHeight = os2.I16(88);
                o.defaultChar = os2.U16(90);
                o.breakChar = os2.U16(92);
                o.maxContext = os2.U16(94);
            }
        }

        ReadNames();
        ReadPost();
        return ReadCmap(error);
    }

    Bytes Font::Table(u32 tag) const
    {
        for (const TableRecord& r : tables_)
            if (r.tag == tag)
                return file_.Sub(r.offset, r.length);
        return Bytes();
    }

    GlyphMetrics Font::HMetrics(u32 glyph) const
    {
        u32 n = hhea_.numberOfHMetrics;
        if (n == 0 || glyph >= glyphCount_)
            return {};
        if (glyph < n)
            return {hmtx_.U16(4 * usize(glyph)), hmtx_.I16(4 * usize(glyph) + 2)};
        // Glyphs after the last long metric share its advance and have only a side bearing.
        return {hmtx_.U16(4 * usize(n - 1)), hmtx_.I16(4 * usize(n) + 2 * usize(glyph - n))};
    }
}
