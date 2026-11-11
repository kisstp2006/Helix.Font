#include <Helix/Font/Font/Font.h>

#include <Helix/Core/Strings/Format.h>

#include <algorithm>

namespace helix::font
{
    namespace
    {
        // More list entries than any font has: a damaged table cannot make the map grow without end.
        constexpr usize kMaxListGlyphs = usize(1) << 21;

        // How good a subtable is for the map: full Unicode first, then the BMP, symbol and Mac Roman.
        int Score(u16 platform, u16 encoding, u16 format)
        {
            bool full = format == 12 || format == 13;
            if (platform == 3 && encoding == 10 && full)
                return 6;
            if (platform == 0 && encoding != 5 && full)
                return 6;
            if (platform == 3 && encoding == 1 && format == 4)
                return 4;
            if (platform == 0 && encoding != 5 && (format == 4 || format == 6 || format == 0))
                return 4;
            if (platform == 3 && encoding == 0 && format == 4)
                return 2;
            if (platform == 1 && encoding == 0 && (format == 0 || format == 6 || format == 4))
                return 1;
            return 0;
        }
    }

    bool Font::ReadCmap(String& error)
    {
        Bytes cmap = Table(Tag("cmap"));
        if (cmap.Empty())
            return true;
        u16 count = cmap.U16(2);
        if (!cmap.Has(4, usize(count) * 8))
            return error = "cmap: encoding records outside the table", false;
        int best = 0;
        Bytes chosen;
        for (u32 i = 0; i < count; i++)
        {
            u16 platform = cmap.U16(4 + 8 * i), encoding = cmap.U16(6 + 8 * i);
            u32 offset = cmap.U32(8 + 8 * i);
            Bytes sub = cmap.From(offset);
            u16 format = sub.U16(0);
            if (platform == 0 && encoding == 5 && format == 14)
            {
                ReadCmap14(sub);
                continue;
            }
            int s = Score(platform, encoding, format);
            if (s > best)
                best = s, chosen = sub;
        }
        if (best == 0)
            return true;
        if (!ReadCmapSubtable(chosen, error))
            return false;
        std::sort(cmap_.begin(), cmap_.end(), [](const CmapRange& a, const CmapRange& b) { return a.first < b.first; });
        return true;
    }

    bool Font::ReadCmapSubtable(Bytes t, String& error)
    {
        u16 format = t.U16(0);
        switch (format)
        {
        case 0:
        {
            if (!t.Has(6, 256))
                return error = "cmap format 0: shorter than its glyph array", false;
            u32 list = u32(glyphs_.Size());
            for (u32 i = 0; i < 256; i++)
                glyphs_.Push(t.U8(6 + i));
            cmap_.Push({0, 255, list, kList});
            return true;
        }
        case 6:
        {
            u16 first = t.U16(6), n = t.U16(8);
            if (!t.Has(10, usize(n) * 2))
                return error = "cmap format 6: shorter than its glyph array", false;
            if (n == 0)
                return true;
            u32 list = u32(glyphs_.Size());
            for (u32 i = 0; i < n; i++)
                glyphs_.Push(t.U16(10 + 2 * usize(i)));
            cmap_.Push({first, u32(first) + n - 1, list, kList});
            return true;
        }
        case 4:
        {
            u16 segX2 = t.U16(6);
            usize segs = segX2 / 2;
            if (segX2 % 2 != 0 || !t.Has(14, usize(segX2) * 4 + 2))
                return error = "cmap format 4: segments outside the subtable", false;
            usize ends = 14, starts = 16 + usize(segX2), deltas = starts + segX2, rangeOffsets = deltas + segX2;
            for (usize s = 0; s < segs; s++)
            {
                u16 end = t.U16(ends + 2 * s), start = t.U16(starts + 2 * s);
                i16 delta = t.I16(deltas + 2 * s);
                u16 rangeOffset = t.U16(rangeOffsets + 2 * s);
                if (start > end || (start == 0xFFFF && end == 0xFFFF))
                    continue;
                if (rangeOffset == 0)
                {
                    cmap_.Push({start, end, delta, kDelta16});
                    continue;
                }
                // The glyphs come from the glyph array, addressed from the range offset's own place.
                usize at = rangeOffsets + 2 * s + rangeOffset;
                usize n = usize(end) - start + 1;
                if (!t.Has(at, 2))
                    continue;
                if (!t.Has(at, n * 2))
                    n = (t.size - at) / 2;
                if (glyphs_.Size() + n > kMaxListGlyphs)
                    return error = "cmap format 4: more glyphs than any font maps", false;
                u32 list = u32(glyphs_.Size());
                for (usize i = 0; i < n; i++)
                {
                    u16 g = t.U16(at + 2 * i);
                    glyphs_.Push(g == 0 ? u16(0) : u16(g + delta));
                }
                cmap_.Push({start, u32(start + n - 1), list, kList});
            }
            return true;
        }
        case 12:
        case 13:
        {
            u32 n = t.U32(12);
            if (!t.Has(16, usize(n) * 12))
                return error = Format("cmap format {}: groups outside the subtable", format), false;
            cmap_.Reserve(n);
            for (u32 i = 0; i < n; i++)
            {
                usize at = 16 + usize(i) * 12;
                u32 first = t.U32(at), last = t.U32(at + 4), glyph = t.U32(at + 8);
                if (first > last || last > 0x10FFFF)
                    continue;
                if (format == 12)
                    cmap_.Push({first, last, i64(glyph) - i64(first), kDelta});
                else
                    cmap_.Push({first, last, i64(glyph), kConstant});
            }
            return true;
        }
        default: return error = Format("cmap format {}", format), false;
        }
    }

    void Font::ReadCmap14(Bytes t)
    {
        u32 n = t.U32(6);
        if (!t.Has(10, usize(n) * 11))
            return;
        for (u32 i = 0; i < n; i++)
        {
            usize at = 10 + usize(i) * 11;
            VariationSelector v{t.U24(at), u32(uvsDefault_.Size()), 0, u32(uvsMappings_.Size()), 0};
            u32 defaults = t.U32(at + 3), mappings = t.U32(at + 7);
            if (defaults != 0)
            {
                Bytes d = t.From(defaults);
                u32 count = d.U32(0);
                if (d.Has(4, usize(count) * 4))
                    for (u32 r = 0; r < count; r++)
                    {
                        u32 first = d.U24(4 + 4 * usize(r));
                        uvsDefault_.Push({first, first + d.U8(7 + 4 * usize(r))});
                    }
            }
            if (mappings != 0)
            {
                Bytes m = t.From(mappings);
                u32 count = m.U32(0);
                if (m.Has(4, usize(count) * 5))
                    for (u32 r = 0; r < count; r++)
                        uvsMappings_.Push({m.U24(4 + 5 * usize(r)), m.U16(7 + 5 * usize(r))});
            }
            v.defaultCount = u32(uvsDefault_.Size()) - v.defaultBegin;
            v.mappingCount = u32(uvsMappings_.Size()) - v.mappingBegin;
            selectors_.Push(v);
        }
    }

    u32 Font::Map(u32 codepoint) const
    {
        // The last range that starts at or before the code point.
        usize lo = 0, hi = cmap_.Size();
        while (lo < hi)
        {
            usize mid = (lo + hi) / 2;
            if (cmap_[mid].first <= codepoint)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo == 0)
            return 0;
        const CmapRange& r = cmap_[lo - 1];
        if (codepoint > r.last)
            return 0;
        i64 glyph;
        switch (r.mode)
        {
        case kDelta: glyph = i64(codepoint) + r.delta; break;
        case kDelta16: glyph = (i64(codepoint) + r.delta) & 0xFFFF; break;
        case kList: glyph = glyphs_[usize(r.delta) + codepoint - r.first]; break;
        default: glyph = r.delta; break;
        }
        return glyph > 0 && glyph < i64(glyphCount_) ? u32(glyph) : 0;
    }

    u32 Font::GlyphIndex(u32 codepoint) const { return Map(codepoint); }

    u32 Font::GlyphIndex(u32 codepoint, u32 selector) const
    {
        for (const VariationSelector& v : selectors_)
        {
            if (v.selector != selector)
                continue;
            for (u32 i = 0; i < v.mappingCount; i++)
                if (uvsMappings_[v.mappingBegin + i].codepoint == codepoint)
                {
                    u32 g = uvsMappings_[v.mappingBegin + i].glyph;
                    return g < glyphCount_ ? g : 0;
                }
            for (u32 i = 0; i < v.defaultCount; i++)
                if (codepoint >= uvsDefault_[v.defaultBegin + i].first && codepoint <= uvsDefault_[v.defaultBegin + i].last)
                    return Map(codepoint);
            return 0;
        }
        return 0;
    }

    void Font::ForEachCodepoint(void (*visit)(void* context, u32 codepoint, u32 glyph), void* context) const
    {
        u32 next = 0;
        for (const CmapRange& r : cmap_)
            for (u32 c = r.first > next ? r.first : next; c <= r.last; c++)
            {
                if (u32 g = Map(c))
                    visit(context, c, g);
                next = c + 1;
                if (c == 0xFFFFFFFFu)
                    return;
            }
    }
}
