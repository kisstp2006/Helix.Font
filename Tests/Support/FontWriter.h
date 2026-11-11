#pragma once

#include <Helix/Core/Containers/Array.h>
#include <Helix/Core/Strings/String.h>
#include <Helix/Font/Font/Bytes.h>

#include <algorithm>
#include <cmath>

// Builds fonts for the tests, fuzz targets and benchmarks: TrueType (glyf) or CFF outlines, the metrics tables, names,
// glyph names, character maps of every format, kerning, and any further table given as bytes. Written from the
// OpenType specification; nothing here is a copy of a font.
namespace helix::font::testing
{
    // Big endian bytes, with 16 and 32 bit offsets to child blobs patched when flattened (children follow their parent,
    // offsets count from the parent's start).
    struct Blob
    {
        Array<u8> bytes;

        struct Pending
        {
            usize at;
            bool wide;
            Array<u8> data;
        };
        Array<Pending> pending;

        Blob& U8(u32 v)
        {
            bytes.Push(u8(v));
            return *this;
        }
        Blob& U16(u32 v)
        {
            bytes.Push(u8(v >> 8));
            bytes.Push(u8(v));
            return *this;
        }
        Blob& I16(i32 v) { return U16(u32(v) & 0xFFFF); }
        Blob& U24(u32 v)
        {
            U8(v >> 16);
            return U16(v & 0xFFFF);
        }
        Blob& U32(u32 v)
        {
            U16(v >> 16);
            return U16(v & 0xFFFF);
        }
        Blob& Tag(const char* t) { return U8(u8(t[0])).U8(u8(t[1])).U8(u8(t[2])).U8(u8(t[3])); }
        Blob& Bytes(const Array<u8>& b)
        {
            bytes.Append(b.Data(), b.Size());
            return *this;
        }
        Blob& Text(StringView s)
        {
            bytes.Append(reinterpret_cast<const u8*>(s.Data()), s.Size());
            return *this;
        }
        // An offset to `child`, placed after this blob; a child of no bytes gives offset 0.
        Blob& Offset16(const Array<u8>& child)
        {
            pending.Push({bytes.Size(), false, child});
            return U16(0);
        }
        Blob& Offset16(const Blob& child) { return Offset16(child.Flatten()); }
        Blob& Offset32(const Array<u8>& child)
        {
            pending.Push({bytes.Size(), true, child});
            return U32(0);
        }
        Blob& Offset32(const Blob& child) { return Offset32(child.Flatten()); }
        usize Size() const { return bytes.Size(); }

        Array<u8> Flatten() const
        {
            Array<u8> out = bytes;
            for (const Pending& p : pending)
            {
                if (p.data.Empty())
                    continue;
                usize at = out.Size();
                out.Append(p.data.Data(), p.data.Size());
                if (p.wide)
                {
                    out[p.at] = u8(at >> 24), out[p.at + 1] = u8(at >> 16), out[p.at + 2] = u8(at >> 8), out[p.at + 3] = u8(at);
                }
                else
                {
                    out[p.at] = u8(at >> 8), out[p.at + 1] = u8(at);
                }
            }
            return out;
        }
    };

    struct TtPoint
    {
        i32 x, y;
        bool on = true;
    };

    struct Component
    {
        u16 glyph = 0;
        i32 dx = 0, dy = 0;
        // 1 scale, 2 x and y scale, 4 two by two; values in 2.14.
        int transform = 0;
        float a = 1, b = 0, c = 0, d = 1;
        // Point matching: the component's point `childPoint` on the composite's point `parentPoint`.
        bool matchPoints = false;
        u16 parentPoint = 0, childPoint = 0;
        bool scaledOffset = false;
    };

    struct GlyphSpec
    {
        Array<Array<TtPoint>> contours;
        Array<Component> components;
        // Type 2 charstring bytes for CFF fonts.
        Array<u8> charstring;
        u16 advance = 500;
        String name;
    };

    // Type 2 charstring bytes.
    struct Charstring
    {
        Array<u8> bytes;

        Charstring& Num(i32 v)
        {
            if (v >= -107 && v <= 107)
                bytes.Push(u8(v + 139));
            else if (v >= 108 && v <= 1131)
            {
                v -= 108;
                bytes.Push(u8((v >> 8) + 247));
                bytes.Push(u8(v));
            }
            else if (v >= -1131 && v <= -108)
            {
                v = -v - 108;
                bytes.Push(u8((v >> 8) + 251));
                bytes.Push(u8(v));
            }
            else
            {
                bytes.Push(28);
                bytes.Push(u8(v >> 8));
                bytes.Push(u8(v));
            }
            return *this;
        }
        // A 16.16 fixed number.
        Charstring& Fixed(float v)
        {
            i32 f = i32(std::lround(v * 65536));
            bytes.Push(255);
            bytes.Push(u8(f >> 24));
            bytes.Push(u8(f >> 16));
            bytes.Push(u8(f >> 8));
            bytes.Push(u8(f));
            return *this;
        }
        Charstring& Op(u8 op)
        {
            bytes.Push(op);
            return *this;
        }
        Charstring& Op12(u8 op)
        {
            bytes.Push(12);
            bytes.Push(op);
            return *this;
        }
    };

    struct FontSpec
    {
        u16 unitsPerEm = 1000;
        i16 ascender = 800, descender = -200, lineGap = 90;
        Array<GlyphSpec> glyphs;
        // Code point, glyph.
        Array<Array<u32>> cmap;
        // Variation selector, code point, glyph (0: the default glyph).
        Array<Array<u32>> variations;
        // Name id and text (Windows English, UTF-16; ids above 255 are written as Mac Roman instead).
        Array<Array<String>> names;
        // Left, right, value.
        Array<Array<i32>> kerning;
        bool longLoca = false;
        bool cff = false;
        // CID keyed CFF: the font DICT of each glyph (two font DICTs, FDSelect format 3).
        Array<u8> cidFds;
        Array<Array<u8>> globalSubrs, localSubrs;
        // Fewer long metrics than glyphs: the rest share the last advance.
        u16 longMetrics = 0;
        // cmap format 4 segments read their glyphs from the glyph array instead of a delta.
        bool cmapGlyphArray = false;
        // A Mac format 6 subtable too.
        bool cmapFormat6 = false;
        bool glyphNames = true;
        // Tables added as they are.
        struct Extra
        {
            char tag[5];
            Array<u8> bytes;
        };
        Array<Extra> extra;

        void AddTable(const char* tag, const Array<u8>& bytes)
        {
            Extra& e = extra.Emplace();
            for (int i = 0; i < 5; i++)
                e.tag[i] = tag[i];
            e.bytes = bytes;
        }
    };

    namespace detail
    {
        inline Array<u8> Index(const Array<Array<u8>>& items)
        {
            Blob b;
            b.U16(u32(items.Size()));
            if (items.Empty())
                return b.bytes;
            b.U8(4);
            u32 offset = 1;
            b.U32(offset);
            for (const Array<u8>& i : items)
                b.U32(offset += u32(i.Size()));
            for (const Array<u8>& i : items)
                b.Bytes(i);
            return b.bytes;
        }

        // A DICT integer in the fixed 5 byte form, so offsets can be patched without moving anything.
        inline void DictInt(Array<u8>& d, i32 v)
        {
            d.Push(29);
            d.Push(u8(v >> 24));
            d.Push(u8(v >> 16));
            d.Push(u8(v >> 8));
            d.Push(u8(v));
        }

        inline Array<u8> Glyf(const GlyphSpec& g, i32& xMin, i32& yMin, i32& xMax, i32& yMax)
        {
            Blob b;
            xMin = yMin = 1 << 30;
            xMax = yMax = -(1 << 30);
            if (!g.components.Empty())
            {
                // Composite boxes are not used by the reader; a plausible box.
                xMin = 0, yMin = 0, xMax = g.advance, yMax = 700;
                b.I16(-1).I16(xMin).I16(yMin).I16(xMax).I16(yMax);
                for (usize i = 0; i < g.components.Size(); i++)
                {
                    const Component& c = g.components[i];
                    i32 a1 = c.matchPoints ? c.parentPoint : c.dx, a2 = c.matchPoints ? c.childPoint : c.dy;
                    bool words = c.matchPoints ? (a1 > 255 || a2 > 255) : (a1 < -128 || a1 > 127 || a2 < -128 || a2 > 127);
                    u16 flags = (words ? 1 : 0) | (c.matchPoints ? 0 : 2) | (i + 1 < g.components.Size() ? 32 : 0) | (c.scaledOffset ? 2048 : 0);
                    flags |= c.transform == 1 ? 8 : c.transform == 2 ? 64 : c.transform == 4 ? 128 : 0;
                    b.U16(flags).U16(c.glyph);
                    if (words)
                        b.U16(u32(a1) & 0xFFFF).U16(u32(a2) & 0xFFFF);
                    else
                        b.U8(u32(a1) & 0xFF).U8(u32(a2) & 0xFF);
                    auto f2 = [&](float v) { b.I16(i32(std::lround(v * 16384))); };
                    if (c.transform == 1)
                        f2(c.a);
                    else if (c.transform == 2)
                        f2(c.a), f2(c.d);
                    else if (c.transform == 4)
                        f2(c.a), f2(c.b), f2(c.c), f2(c.d);
                }
                while (b.Size() % 4)
                    b.U8(0);
                return b.bytes;
            }
            if (g.contours.Empty())
                return Array<u8>();
            Array<TtPoint> all;
            Array<u16> ends;
            for (const Array<TtPoint>& c : g.contours)
            {
                all.Append(c.Data(), c.Size());
                ends.Push(u16(all.Size() - 1));
            }
            for (const TtPoint& p : all)
                xMin = std::min(xMin, p.x), yMin = std::min(yMin, p.y), xMax = std::max(xMax, p.x), yMax = std::max(yMax, p.y);
            b.I16(i32(g.contours.Size())).I16(xMin).I16(yMin).I16(xMax).I16(yMax);
            for (u16 e : ends)
                b.U16(e);
            b.U16(0);
            // Flags with short vectors, same-as-previous and repeats, as real fonts write them.
            Array<u8> flags;
            Blob xs, ys;
            i32 px = 0, py = 0;
            for (const TtPoint& p : all)
            {
                u8 f = p.on ? 1 : 0;
                i32 dx = p.x - px, dy = p.y - py;
                if (dx == 0)
                    f |= 16;
                else if (dx >= -255 && dx <= 255)
                    f |= 2 | (dx > 0 ? 16 : 0), xs.U8(u32(dx < 0 ? -dx : dx));
                else
                    xs.I16(dx);
                if (dy == 0)
                    f |= 32;
                else if (dy >= -255 && dy <= 255)
                    f |= 4 | (dy > 0 ? 32 : 0), ys.U8(u32(dy < 0 ? -dy : dy));
                else
                    ys.I16(dy);
                flags.Push(f);
                px = p.x, py = p.y;
            }
            for (usize i = 0; i < flags.Size();)
            {
                usize run = 1;
                while (i + run < flags.Size() && flags[i + run] == flags[i] && run < 256)
                    run++;
                if (run > 2)
                {
                    b.U8(flags[i] | 8).U8(u32(run - 1));
                    i += run;
                }
                else
                    b.U8(flags[i++]);
            }
            b.Bytes(xs.bytes).Bytes(ys.bytes);
            while (b.Size() % 4)
                b.U8(0);
            return b.bytes;
        }

        inline Array<u8> Cff(const FontSpec& s)
        {
            Array<Array<u8>> charstrings;
            for (const GlyphSpec& g : s.glyphs)
                charstrings.Push(g.charstring);
            bool cid = !s.cidFds.Empty();
            Array<u8> header = {1, 0, 4, 4};
            Array<u8> names = Index({Array<u8>{'T', 'e', 's', 't'}});
            Array<u8> strings = Index(Array<Array<u8>>());
            Array<u8> globals = Index(s.globalSubrs);
            Array<u8> chars = Index(charstrings);
            Array<u8> locals = Index(s.localSubrs);
            // Private DICT: Subrs right after it.
            Array<u8> priv;
            if (!s.localSubrs.Empty())
            {
                DictInt(priv, 0);
                priv.Push(19);
            }
            usize privSize = priv.Size();
            if (!s.localSubrs.Empty())
            {
                // The offset counts from the Private DICT start.
                Array<u8> fixed;
                DictInt(fixed, i32(privSize));
                fixed.Push(19);
                priv = fixed;
            }
            // Top DICT with fixed size integers, so its size is known before the offsets are.
            auto top = [&](i32 charStringsAt, i32 privateAt, i32 fdArrayAt, i32 fdSelectAt) {
                Array<u8> d;
                if (cid)
                {
                    DictInt(d, 391), DictInt(d, 392), DictInt(d, 0);
                    d.Push(12), d.Push(30);
                }
                DictInt(d, charStringsAt);
                d.Push(17);
                if (cid)
                {
                    DictInt(d, fdArrayAt);
                    d.Push(12), d.Push(36);
                    DictInt(d, fdSelectAt);
                    d.Push(12), d.Push(37);
                }
                else
                {
                    DictInt(d, i32(priv.Size()));
                    DictInt(d, privateAt);
                    d.Push(18);
                }
                return d;
            };
            usize topIndexSize = Index({top(0, 0, 0, 0)}).Size();
            usize at = header.Size() + names.Size() + topIndexSize + strings.Size() + globals.Size();
            usize charsAt = at;
            at += chars.Size();
            usize privAt = at;
            at += priv.Size() + locals.Size();
            Array<u8> fdArray, fdSelect;
            usize fdArrayAt = 0, fdSelectAt = 0;
            if (cid)
            {
                // Two font DICTs sharing the one Private DICT.
                Array<u8> fd;
                DictInt(fd, i32(priv.Size()));
                DictInt(fd, i32(privAt));
                fd.Push(18);
                fdArray = Index({fd, fd});
                fdArrayAt = at;
                at += fdArray.Size();
                // Format 3 ranges from the per glyph list.
                Blob sel;
                sel.U8(3);
                Array<u32> starts;
                for (usize g = 0; g < s.cidFds.Size(); g++)
                    if (g == 0 || s.cidFds[g] != s.cidFds[g - 1])
                        starts.Push(u32(g));
                sel.U16(u32(starts.Size()));
                for (u32 st : starts)
                    sel.U16(st).U8(s.cidFds[st]);
                sel.U16(u32(s.cidFds.Size()));
                fdSelect = sel.bytes;
                fdSelectAt = at;
            }
            Blob out;
            out.Bytes(header).Bytes(names).Bytes(Index({top(i32(charsAt), i32(privAt), i32(fdArrayAt), i32(fdSelectAt))})).Bytes(strings).Bytes(globals);
            out.Bytes(chars).Bytes(priv).Bytes(locals).Bytes(fdArray).Bytes(fdSelect);
            return out.bytes;
        }

        inline void Utf16(Blob& b, StringView s)
        {
            // ASCII and Latin-1 two byte sequences are enough for the tests.
            for (usize i = 0; i < s.Size(); i++)
            {
                u8 c = u8(s[i]);
                if (c >= 0xC0 && i + 1 < s.Size())
                {
                    b.U16(u32(c & 0x1F) << 6 | (u8(s[i + 1]) & 0x3F));
                    i++;
                }
                else
                    b.U16(c);
            }
        }
    }

    inline Array<u8> BuildFont(const FontSpec& s)
    {
        struct Table
        {
            u32 tag;
            Array<u8> bytes;
        };
        Array<Table> tables;
        auto add = [&](const char* tag, const Array<u8>& bytes) {
            tables.Push({u32(u8(tag[0])) << 24 | u32(u8(tag[1])) << 16 | u32(u8(tag[2])) << 8 | u8(tag[3]), bytes});
        };
        u32 n = u32(s.glyphs.Size());

        i32 bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
        Array<i32> lsb;
        if (!s.cff)
        {
            Blob glyf, loca;
            bool first = true;
            for (const GlyphSpec& g : s.glyphs)
            {
                if (s.longLoca)
                    loca.U32(u32(glyf.Size()));
                else
                    loca.U16(u32(glyf.Size() / 2));
                i32 x0, y0, x1, y1;
                Array<u8> data = detail::Glyf(g, x0, y0, x1, y1);
                glyf.Bytes(data);
                lsb.Push(data.Empty() ? 0 : x0);
                if (!data.Empty())
                {
                    if (first)
                        bx0 = x0, by0 = y0, bx1 = x1, by1 = y1, first = false;
                    bx0 = std::min(bx0, x0), by0 = std::min(by0, y0), bx1 = std::max(bx1, x1), by1 = std::max(by1, y1);
                }
            }
            if (s.longLoca)
                loca.U32(u32(glyf.Size()));
            else
                loca.U16(u32(glyf.Size() / 2));
            add("glyf", glyf.bytes);
            add("loca", loca.bytes);
        }
        else
        {
            add("CFF ", detail::Cff(s));
            for (u32 g = 0; g < n; g++)
                lsb.Push(0);
            bx1 = 1000, by1 = 800;
        }

        Blob head;
        head.U32(0x00010000).U32(0x00010000).U32(0).U32(0x5F0F3CF5).U16(0).U16(s.unitsPerEm);
        head.U32(0).U32(0).U32(0).U32(0);
        head.I16(bx0).I16(by0).I16(bx1).I16(by1).U16(0).U16(8).I16(2).I16(s.longLoca ? 1 : 0).I16(0);
        add("head", head.bytes);

        u16 longMetrics = s.longMetrics ? s.longMetrics : u16(n);
        Blob hhea;
        hhea.U32(0x00010000).I16(s.ascender).I16(s.descender).I16(s.lineGap).U16(1000).I16(0).I16(0).I16(1000).I16(1).I16(0).I16(0);
        for (int i = 0; i < 5; i++)
            hhea.I16(0);
        hhea.U16(longMetrics);
        add("hhea", hhea.bytes);

        Blob maxp;
        maxp.U32(0x00005000).U16(n);
        add("maxp", maxp.bytes);

        Blob hmtx;
        for (u32 g = 0; g < n; g++)
        {
            if (g < longMetrics)
                hmtx.U16(s.glyphs[g].advance);
            hmtx.I16(lsb[g]);
        }
        add("hmtx", hmtx.bytes);

        // OS/2 version 4.
        Blob os2;
        os2.U16(4).I16(500).U16(700).U16(5).U16(0);
        for (int i = 0; i < 11; i++)
            os2.I16(10 + i);
        for (int i = 0; i < 10; i++)
            os2.U8(u32(i));
        os2.U32(1).U32(2).U32(4).U32(8).Tag("HLX ").U16(0x40).U16(32).U16(0xFFFF);
        os2.I16(s.ascender).I16(s.descender).I16(s.lineGap).U16(900).U16(250).U32(1).U32(0);
        os2.I16(480).I16(700).U16(0).U16(32).U16(3);
        add("OS/2", os2.bytes);

        // cmap: format 4 for the BMP, format 12 for everything, optionally 6 and 14.
        Array<Array<u32>> map = s.cmap;
        std::sort(map.begin(), map.end(), [](const Array<u32>& a, const Array<u32>& b) { return a[0] < b[0]; });
        Blob f4;
        {
            struct Seg
            {
                u32 start, end;
                Array<u32> glyphs;
            };
            Array<Seg> segs;
            for (const Array<u32>& m : map)
            {
                if (m[0] > 0xFFFE)
                    continue;
                if (!segs.Empty() && segs.Back().end + 1 == m[0] && (s.cmapGlyphArray || m[1] - segs.Back().glyphs.Back() == 1))
                {
                    segs.Back().end = m[0];
                    segs.Back().glyphs.Push(m[1]);
                }
                else
                {
                    Seg seg{m[0], m[0], Array<u32>()};
                    seg.glyphs.Push(m[1]);
                    segs.Push(seg);
                }
            }
            segs.Push({0xFFFF, 0xFFFF, Array<u32>{0}});
            u32 segCount = u32(segs.Size());
            Blob body;
            for (const Seg& sg : segs)
                body.U16(sg.end);
            body.U16(0);
            for (const Seg& sg : segs)
                body.U16(sg.start);
            for (const Seg& sg : segs)
                body.U16(sg.start == 0xFFFF || s.cmapGlyphArray ? (sg.start == 0xFFFF ? 1 : 0) : (sg.glyphs[0] - sg.start) & 0xFFFF);
            // Range offsets point from their own place into the glyph array that follows them.
            u32 arrayIndex = 0;
            for (u32 i = 0; i < segCount; i++)
            {
                const Seg& sg = segs[i];
                if (s.cmapGlyphArray && sg.start != 0xFFFF)
                {
                    body.U16(2 * (segCount - i) + 2 * arrayIndex);
                    arrayIndex += u32(sg.glyphs.Size());
                }
                else
                    body.U16(0);
            }
            if (s.cmapGlyphArray)
                for (const Seg& sg : segs)
                    if (sg.start != 0xFFFF)
                        for (u32 g : sg.glyphs)
                            body.U16(g);
            u32 entrySelector = 0;
            while ((2u << entrySelector) <= segCount)
                entrySelector++;
            u32 searchRange = 2u << entrySelector;
            f4.U16(4).U16(u32(14 + body.Size())).U16(0).U16(2 * segCount).U16(searchRange).U16(entrySelector).U16(2 * segCount - searchRange);
            f4.Bytes(body.bytes);
        }
        Blob f12;
        f12.U16(12).U16(0).U32(u32(16 + 12 * map.Size())).U32(0).U32(u32(map.Size()));
        for (const Array<u32>& m : map)
            f12.U32(m[0]).U32(m[0]).U32(m[1]);
        Blob cmap;
        u32 subtables = 2 + (s.cmapFormat6 ? 1 : 0) + (s.variations.Empty() ? 0 : 1);
        cmap.U16(0).U16(subtables);
        Blob f6;
        if (s.cmapFormat6)
        {
            // Mac Roman: the code points 32 to 127 the map has.
            f6.U16(6).U16(0).U16(0).U16(32).U16(96);
            for (u32 c = 32; c < 128; c++)
            {
                u32 g = 0;
                for (const Array<u32>& m : map)
                    if (m[0] == c)
                        g = m[1];
                f6.U16(g);
            }
        }
        Blob f14;
        if (!s.variations.Empty())
        {
            Array<u32> selectors;
            for (const Array<u32>& v : s.variations)
                if (std::find(selectors.begin(), selectors.end(), v[0]) == selectors.end())
                    selectors.Push(v[0]);
            std::sort(selectors.begin(), selectors.end());
            Blob records, data;
            usize headerSize = 10 + 11 * selectors.Size();
            for (u32 sel : selectors)
            {
                Blob def, nondef;
                u32 defs = 0, nondefs = 0;
                for (const Array<u32>& v : s.variations)
                    if (v[0] == sel)
                        (v[2] == 0 ? defs : nondefs)++;
                def.U32(defs);
                nondef.U32(nondefs);
                for (const Array<u32>& v : s.variations)
                    if (v[0] == sel)
                    {
                        if (v[2] == 0)
                            def.U24(v[1]).U8(0);
                        else
                            nondef.U24(v[1]).U16(v[2]);
                    }
                records.U24(sel);
                records.U32(defs ? u32(headerSize + data.Size()) : 0);
                if (defs)
                    data.Bytes(def.bytes);
                records.U32(nondefs ? u32(headerSize + data.Size()) : 0);
                if (nondefs)
                    data.Bytes(nondef.bytes);
            }
            f14.U16(14).U32(u32(headerSize + data.Size())).U32(u32(selectors.Size())).Bytes(records.bytes).Bytes(data.bytes);
        }
        if (s.cmapFormat6)
            cmap.U16(1).U16(0).Offset32(f6.bytes);
        if (!s.variations.Empty())
            cmap.U16(0).U16(5).Offset32(f14.bytes);
        cmap.U16(3).U16(1).Offset32(f4.bytes);
        cmap.U16(3).U16(10).Offset32(f12.bytes);
        add("cmap", cmap.Flatten());

        Blob name, strings;
        name.U16(0).U16(u32(s.names.Size())).U16(u32(6 + 12 * s.names.Size()));
        for (const Array<String>& r : s.names)
        {
            u32 id = 0;
            for (char ch : r[0].View())
                id = id * 10 + u32(ch - '0');
            usize at = strings.Size();
            if (id < 256)
            {
                detail::Utf16(strings, r[1]);
                name.U16(3).U16(1).U16(0x409).U16(id).U16(u32(strings.Size() - at)).U16(u32(at));
            }
            else
            {
                strings.Text(r[1]);
                name.U16(1).U16(0).U16(0).U16(id - 256).U16(u32(strings.Size() - at)).U16(u32(at));
            }
        }
        name.Bytes(strings.bytes);
        add("name", name.bytes);

        Blob post;
        post.U32(s.glyphNames ? 0x00020000 : 0x00030000).U32(u32(-12 * 65536)).I16(-100).I16(50).U32(0).U32(0).U32(0).U32(0).U32(0);
        if (s.glyphNames)
        {
            post.U16(n);
            Blob pascal;
            u32 custom = 0;
            for (const GlyphSpec& g : s.glyphs)
            {
                if (g.name.Empty())
                    post.U16(0);
                else if (g.name == StringView("A"))
                    post.U16(36);
                else
                {
                    post.U16(258 + custom++);
                    pascal.U8(u32(g.name.Size())).Text(g.name);
                }
            }
            post.Bytes(pascal.bytes);
        }
        add("post", post.bytes);

        if (!s.kerning.Empty())
        {
            Array<Array<i32>> pairs = s.kerning;
            std::sort(pairs.begin(), pairs.end(), [](const Array<i32>& a, const Array<i32>& b) { return a[0] != b[0] ? a[0] < b[0] : a[1] < b[1]; });
            Blob kern;
            kern.U16(0).U16(1).U16(0).U16(u32(14 + 6 * pairs.Size())).U16(0x0001).U16(u32(pairs.Size())).U16(0).U16(0).U16(0);
            for (const Array<i32>& p : pairs)
                kern.U16(u32(p[0])).U16(u32(p[1])).I16(p[2]);
            add("kern", kern.bytes);
        }
        for (const FontSpec::Extra& e : s.extra)
            add(e.tag, e.bytes);

        std::sort(tables.begin(), tables.end(), [](const Table& a, const Table& b) { return a.tag < b.tag; });
        Blob file;
        file.U32(s.cff ? Tag("OTTO") : 0x00010000).U16(u32(tables.Size())).U16(0).U16(0).U16(0);
        u32 offset = u32(12 + 16 * tables.Size());
        for (const Table& t : tables)
        {
            file.U32(t.tag).U32(0).U32(offset).U32(u32(t.bytes.Size()));
            offset += (u32(t.bytes.Size()) + 3) & ~3u;
        }
        for (const Table& t : tables)
        {
            file.Bytes(t.bytes);
            while (file.Size() % 4)
                file.U8(0);
        }
        return file.bytes;
    }

    // A collection of fonts: each font's tables moved behind the collection header.
    inline Array<u8> BuildCollection(const Array<Array<u8>>& fonts)
    {
        Blob out;
        out.Tag("ttcf").U32(0x00010000).U32(u32(fonts.Size()));
        usize headerSize = 12 + 4 * fonts.Size();
        usize at = headerSize;
        for (const Array<u8>& f : fonts)
        {
            out.U32(u32(at));
            at += f.Size();
        }
        for (const Array<u8>& f : fonts)
        {
            // Table offsets of each font count from the file start.
            Array<u8> copy = f;
            Bytes b(copy.Data(), copy.Size());
            u16 count = b.U16(4);
            usize base = out.Size();
            for (u32 i = 0; i < count; i++)
            {
                usize rec = 12 + 16 * usize(i) + 8;
                u32 off = b.U32(rec) + u32(base);
                copy[rec] = u8(off >> 24), copy[rec + 1] = u8(off >> 16), copy[rec + 2] = u8(off >> 8), copy[rec + 3] = u8(off);
            }
            out.Bytes(copy);
        }
        return out.bytes;
    }

    // A square contour from (x0, y0) to (x1, y1), clockwise as TrueType outer contours are.
    inline Array<TtPoint> Square(i32 x0, i32 y0, i32 x1, i32 y1) { return {{x0, y0}, {x0, y1}, {x1, y1}, {x1, y0}}; }
}
