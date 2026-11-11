#include <Helix/Font/Outline/Outline.h>

#include <Helix/Core/Strings/Format.h>

#include <cmath>
#include <cstdlib>

namespace helix::font
{
    namespace
    {
        // The Standard Encoding of CFF as string ids, for the accents of seac.
        constexpr u8 kStandardEncoding[256] = {
            0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
            0,   0,   0,   0,   0,   0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,
            21,  22,  23,  24,  25,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,
            47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,  72,
            73,  74,  75,  76,  77,  78,  79,  80,  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,  91,  92,  93,  94,  95,  0,   0,   0,
            0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
            0,   0,   0,   0,   0,   96,  97,  98,  99,  100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 0,   111, 112, 113, 114, 0,
            115, 116, 117, 118, 119, 120, 121, 122, 0,   123, 0,   124, 125, 126, 127, 128, 129, 130, 131, 0,   132, 133, 0,   134, 135, 136,
            137, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   138, 0,   139, 0,   0,   0,   0,   140, 141,
            142, 143, 0,   0,   0,   0,   0,   144, 0,   0,   0,   145, 0,   0,   146, 147, 148, 149, 0,   0,   0,   0,
        };
        static_assert(sizeof(kStandardEncoding) == 256);

        constexpr u32 kMaxStack = 48;
        constexpr u32 kMaxSubrDepth = 10;
        // Operators one glyph may run, subroutines included: more is a loop through damaged data.
        constexpr u32 kMaxOperations = 1u << 16;

        u32 SubrBias(u32 count) { return count < 1240 ? 107 : count < 33900 ? 1131 : 32768; }

        // One DICT entry: its operator (12 x as 1200 + x) and operands.
        struct DictEntry
        {
            u32 op;
            double args[48];
            u32 count;
        };

        // Calls `visit` for every entry; false when the DICT is damaged.
        template <class F>
        bool ParseDict(Bytes d, F&& visit)
        {
            DictEntry e{};
            usize at = 0;
            while (at < d.size)
            {
                u8 b = d.U8(at);
                if (b <= 21)
                {
                    e.op = b;
                    at++;
                    if (b == 12)
                        e.op = 1200 + d.U8(at++);
                    visit(e);
                    e.count = 0;
                    continue;
                }
                double v;
                if (b == 28)
                    v = d.I16(at + 1), at += 3;
                else if (b == 29)
                    v = d.I32(at + 1), at += 5;
                else if (b == 30)
                {
                    // A real number as nibbles.
                    char text[64];
                    usize n = 0;
                    at++;
                    bool done = false;
                    while (!done && at < d.size)
                    {
                        u8 byte = d.U8(at++);
                        for (int half = 0; half < 2 && !done; half++)
                        {
                            u8 nib = half == 0 ? byte >> 4 : byte & 15;
                            const char* s = nib <= 9 ? nullptr : nib == 0xA ? "." : nib == 0xB ? "E" : nib == 0xC ? "E-" : nib == 0xE ? "-" : "";
                            if (nib == 0xF)
                                done = true;
                            else if (nib <= 9 && n < 62)
                                text[n++] = char('0' + nib);
                            else if (s)
                                for (; *s && n < 62; s++)
                                    text[n++] = *s;
                        }
                    }
                    text[n] = 0;
                    v = std::strtod(text, nullptr);
                }
                else if (b >= 32 && b <= 246)
                    v = i32(b) - 139, at++;
                else if (b >= 247 && b <= 250)
                    v = (i32(b) - 247) * 256 + d.U8(at + 1) + 108, at += 2;
                else if (b >= 251 && b <= 254)
                    v = -(i32(b) - 251) * 256 - d.U8(at + 1) - 108, at += 2;
                else
                    return false;
                if (e.count < 48)
                    e.args[e.count++] = v;
            }
            return true;
        }
    }

    bool Outlines::ReadIndex(usize at, CffIndex& out, usize& end) const
    {
        const Bytes& t = cffTable_;
        out = CffIndex();
        if (!t.Has(at, 2))
            return false;
        out.count = t.U16(at);
        if (out.count == 0)
        {
            end = at + 2;
            return true;
        }
        out.offSize = t.U8(at + 2);
        if (out.offSize < 1 || out.offSize > 4)
            return false;
        out.offsets = at + 3;
        if (!t.Has(out.offsets, (usize(out.count) + 1) * out.offSize))
            return false;
        out.data = out.offsets + (usize(out.count) + 1) * out.offSize - 1;
        usize last = 0;
        for (u8 i = 0; i < out.offSize; i++)
            last = last << 8 | t.U8(out.offsets + usize(out.count) * out.offSize + i);
        if (!t.Has(out.data, last))
            return false;
        end = out.data + last;
        return true;
    }

    Bytes Outlines::IndexItem(const CffIndex& index, u32 i) const
    {
        if (i >= index.count)
            return Bytes();
        usize a = 0, b = 0;
        for (u8 k = 0; k < index.offSize; k++)
        {
            a = a << 8 | cffTable_.U8(index.offsets + usize(i) * index.offSize + k);
            b = b << 8 | cffTable_.U8(index.offsets + usize(i + 1) * index.offSize + k);
        }
        if (a < 1 || b < a)
            return Bytes();
        return cffTable_.Sub(index.data + a, b - a);
    }

    bool Outlines::ReadPrivate(Bytes dict, CffPrivate& out) const
    {
        usize start = usize(dict.data - cffTable_.data);
        double subrs = 0;
        bool ok = ParseDict(dict, [&](const DictEntry& e) {
            if (e.count == 0)
                return;
            if (e.op == 19)
                subrs = e.args[0];
            else if (e.op == 21)
                out.nominalWidth = float(e.args[0]);
        });
        if (!ok)
            return false;
        if (subrs > 0)
        {
            usize end;
            if (!ReadIndex(start + usize(subrs), out.subrs, end))
                return false;
        }
        return true;
    }

    bool Outlines::ReadCff(String& error)
    {
        cffTable_ = font_->Table(Tag("CFF "));
        const Bytes& t = cffTable_;
        if (t.U8(0) != 1)
            return error = Format("CFF version {}", t.U8(0)), false;
        usize at = t.U8(2);
        CffIndex names, tops, strings;
        if (!ReadIndex(at, names, at) || !ReadIndex(at, tops, at) || !ReadIndex(at, strings, at) || !ReadIndex(at, globalSubrs_, at))
            return error = "CFF: an INDEX outside the table", false;
        double charStrings = 0, privateSize = 0, privateOffset = 0, fdArray = 0, fdSelect = 0, charset = 0, type = 2;
        bool ok = ParseDict(IndexItem(tops, 0), [&](const DictEntry& e) {
            if (e.count == 0)
                return;
            switch (e.op)
            {
            case 17: charStrings = e.args[0]; break;
            case 18:
                if (e.count >= 2)
                    privateSize = e.args[0], privateOffset = e.args[1];
                break;
            case 15: charset = e.args[0]; break;
            case 1206: type = e.args[0]; break;
            case 1230: cid_ = true; break;
            case 1236: fdArray = e.args[0]; break;
            case 1237: fdSelect = e.args[0]; break;
            default: break;
            }
        });
        if (!ok)
            return error = "CFF: a damaged Top DICT", false;
        if (type != 2)
            return error = Format("CFF charstring type {}", type), false;
        charset_ = usize(charset);
        usize end;
        if (charStrings <= 0 || !ReadIndex(usize(charStrings), charStrings_, end))
            return error = "CFF: no CharStrings INDEX", false;
        if (charStrings_.count < font_->GlyphCount())
            return error = Format("CFF: {} charstrings for {} glyphs", charStrings_.count, font_->GlyphCount()), false;

        if (!cid_)
        {
            CffPrivate p;
            if (privateSize > 0 && !ReadPrivate(t.Sub(usize(privateOffset), usize(privateSize)), p))
                return error = "CFF: a damaged Private DICT", false;
            privates_.Push(p);
            return true;
        }
        CffIndex fds;
        if (fdArray <= 0 || !ReadIndex(usize(fdArray), fds, end))
            return error = "CFF: a CID font without its FDArray", false;
        for (u32 i = 0; i < fds.count && i < 256; i++)
        {
            double size = 0, offset = 0;
            if (!ParseDict(IndexItem(fds, i), [&](const DictEntry& e) {
                    if (e.op == 18 && e.count >= 2)
                        size = e.args[0], offset = e.args[1];
                }))
                return error = Format("CFF: font DICT {} is damaged", i), false;
            CffPrivate p;
            if (size > 0 && !ReadPrivate(t.Sub(usize(offset), usize(size)), p))
                return error = Format("CFF: the Private DICT of font DICT {} is damaged", i), false;
            privates_.Push(p);
        }
        if (privates_.Empty())
            return error = "CFF: an empty FDArray", false;
        u32 glyphs = font_->GlyphCount();
        fdSelect_.Resize(glyphs, u8(0));
        Bytes s = t.From(usize(fdSelect));
        u8 format = s.U8(0);
        if (format == 0)
        {
            if (!s.Has(1, glyphs))
                return error = "CFF: FDSelect outside the table", false;
            for (u32 g = 0; g < glyphs; g++)
                fdSelect_[g] = s.U8(1 + g);
        }
        else if (format == 3)
        {
            u16 ranges = s.U16(1);
            if (!s.Has(3, usize(ranges) * 3 + 2))
                return error = "CFF: FDSelect outside the table", false;
            for (u32 r = 0; r < ranges; r++)
            {
                u32 first = s.U16(3 + 3 * usize(r)), next = s.U16(6 + 3 * usize(r));
                u8 fd = s.U8(5 + 3 * usize(r));
                for (u32 g = first; g < next && g < glyphs; g++)
                    fdSelect_[g] = fd;
            }
        }
        else
            return error = Format("CFF: FDSelect format {}", format), false;
        for (u8& fd : fdSelect_)
            if (fd >= privates_.Size())
                fd = 0;
        return true;
    }

    u32 Outlines::CffGlyphOfSid(u32 sid) const
    {
        u32 glyphs = font_->GlyphCount();
        if (charset_ == 0)
            return sid < glyphs && sid <= 228 ? sid : 0;
        if (charset_ < 3)
            return 0;
        Bytes c = cffTable_.From(charset_);
        u8 format = c.U8(0);
        if (format == 0)
        {
            for (u32 g = 1; g < glyphs; g++)
                if (c.U16(1 + 2 * usize(g - 1)) == sid)
                    return g;
            return 0;
        }
        usize at = 1;
        u32 g = 1;
        while (g < glyphs && c.Has(at, format == 1 ? 3 : 4))
        {
            u32 first = c.U16(at);
            u32 left = format == 1 ? c.U8(at + 2) : c.U16(at + 2);
            if (sid >= first && sid <= first + left)
                return g + (sid - first) < glyphs ? g + (sid - first) : 0;
            g += left + 1;
            at += format == 1 ? 3 : 4;
        }
        return 0;
    }

    // Runs Type 2 charstrings into an outline.
    struct CharstringRunner
    {
        const Outlines& o;
        Outline& out;
        String& error;
        const Outlines::CffIndex* local = nullptr;
        float stack[kMaxStack];
        u32 sp = 0;
        float x = 0, y = 0;
        u32 stems = 0;
        bool widthDone = false;
        bool open = false;
        bool done = false;
        u32 operations = 0;
        float transient[32] = {};
        // seac: the accent's string ids and offset, run after the base.
        bool seac = false;
        float seacX = 0, seacY = 0;
        u32 seacBase = 0, seacAccent = 0;

        bool Fail(StringView what)
        {
            error = String(what);
            return false;
        }

        // The first stack clearing operator may carry the advance width first.
        void Width(bool extra)
        {
            if (!widthDone && extra && sp > 0)
            {
                for (u32 i = 1; i < sp; i++)
                    stack[i - 1] = stack[i];
                sp--;
            }
            widthDone = true;
        }

        void MoveTo(float dx, float dy)
        {
            x += dx, y += dy;
            out.MoveTo({x, y});
            open = true;
        }
        void LineTo(float dx, float dy)
        {
            if (!open)
                out.MoveTo({x, y}), open = true;
            x += dx, y += dy;
            out.LineTo({x, y});
        }
        void CurveTo(float dx1, float dy1, float dx2, float dy2, float dx3, float dy3)
        {
            if (!open)
                out.MoveTo({x, y}), open = true;
            Point c1{x + dx1, y + dy1};
            Point c2{c1.x + dx2, c1.y + dy2};
            Point p{c2.x + dx3, c2.y + dy3};
            out.CubicTo(c1, c2, p);
            x = p.x, y = p.y;
        }

        bool Run(Bytes cs, u32 depth)
        {
            if (depth > kMaxSubrDepth)
                return Fail("subroutines nested too deep");
            usize at = 0;
            while (at < cs.size && !done)
            {
                if (++operations > kMaxOperations)
                    return Fail("a charstring that does not end");
                u8 b = cs.U8(at);
                if (b >= 32 || b == 28)
                {
                    float v;
                    if (b == 28)
                        v = cs.I16(at + 1), at += 3;
                    else if (b <= 246)
                        v = float(i32(b) - 139), at++;
                    else if (b <= 250)
                        v = float((i32(b) - 247) * 256 + cs.U8(at + 1) + 108), at += 2;
                    else if (b <= 254)
                        v = float(-(i32(b) - 251) * 256 - cs.U8(at + 1) - 108), at += 2;
                    else
                        v = float(cs.I32(at + 1)) / 65536.0f, at += 5;
                    if (sp >= kMaxStack)
                        return Fail("a charstring stack overflow");
                    stack[sp++] = v;
                    continue;
                }
                at++;
                switch (b)
                {
                case 1:
                case 3:
                case 18:
                case 23:
                    Width(sp % 2 == 1);
                    stems += sp / 2;
                    sp = 0;
                    break;
                case 19:
                case 20:
                    Width(sp % 2 == 1);
                    stems += sp / 2;
                    sp = 0;
                    at += (stems + 7) / 8;
                    break;
                case 21:
                    Width(sp > 2);
                    if (sp < 2)
                        return Fail("rmoveto without its arguments");
                    MoveTo(stack[sp - 2], stack[sp - 1]);
                    sp = 0;
                    break;
                case 22:
                    Width(sp > 1);
                    if (sp < 1)
                        return Fail("hmoveto without its argument");
                    MoveTo(stack[sp - 1], 0);
                    sp = 0;
                    break;
                case 4:
                    Width(sp > 1);
                    if (sp < 1)
                        return Fail("vmoveto without its argument");
                    MoveTo(0, stack[sp - 1]);
                    sp = 0;
                    break;
                case 5:
                    for (u32 i = 0; i + 1 < sp; i += 2)
                        LineTo(stack[i], stack[i + 1]);
                    sp = 0;
                    break;
                case 6:
                case 7:
                {
                    bool horizontal = b == 6;
                    for (u32 i = 0; i < sp; i++, horizontal = !horizontal)
                        horizontal ? LineTo(stack[i], 0) : LineTo(0, stack[i]);
                    sp = 0;
                    break;
                }
                case 8:
                    for (u32 i = 0; i + 5 < sp; i += 6)
                        CurveTo(stack[i], stack[i + 1], stack[i + 2], stack[i + 3], stack[i + 4], stack[i + 5]);
                    sp = 0;
                    break;
                case 24:
                {
                    u32 i = 0;
                    for (; i + 7 < sp; i += 6)
                        CurveTo(stack[i], stack[i + 1], stack[i + 2], stack[i + 3], stack[i + 4], stack[i + 5]);
                    if (i + 1 < sp)
                        LineTo(stack[i], stack[i + 1]);
                    sp = 0;
                    break;
                }
                case 25:
                {
                    u32 i = 0;
                    for (; i + 7 < sp; i += 2)
                        LineTo(stack[i], stack[i + 1]);
                    if (i + 5 < sp)
                        CurveTo(stack[i], stack[i + 1], stack[i + 2], stack[i + 3], stack[i + 4], stack[i + 5]);
                    sp = 0;
                    break;
                }
                case 26:
                case 27:
                {
                    // vvcurveto, hhcurveto: an odd count starts with the cross axis delta of the first curve.
                    u32 i = 0;
                    float cross = 0;
                    if (sp % 2 == 1)
                        cross = stack[i++];
                    for (; i + 3 < sp; i += 4)
                    {
                        if (b == 27)
                            CurveTo(stack[i], cross, stack[i + 1], stack[i + 2], stack[i + 3], 0);
                        else
                            CurveTo(cross, stack[i], stack[i + 1], stack[i + 2], 0, stack[i + 3]);
                        cross = 0;
                    }
                    sp = 0;
                    break;
                }
                case 30:
                case 31:
                {
                    bool horizontal = b == 31;
                    for (u32 i = 0; i + 3 < sp; i += 4, horizontal = !horizontal)
                    {
                        float last = sp - i == 5 ? stack[i + 4] : 0;
                        if (horizontal)
                            CurveTo(stack[i], 0, stack[i + 1], stack[i + 2], last, stack[i + 3]);
                        else
                            CurveTo(0, stack[i], stack[i + 1], stack[i + 2], stack[i + 3], last);
                    }
                    sp = 0;
                    break;
                }
                case 10:
                case 29:
                {
                    if (sp < 1)
                        return Fail("a subroutine call without its number");
                    const Outlines::CffIndex& index = b == 10 ? *local : o.globalSubrs_;
                    i64 n = i64(stack[--sp]) + SubrBias(index.count);
                    if (n < 0 || n >= i64(index.count))
                        return Fail("a call of a missing subroutine");
                    if (!Run(o.IndexItem(index, u32(n)), depth + 1))
                        return false;
                    break;
                }
                case 11: return true;
                case 14:
                    Width(sp == 1 || sp == 5);
                    if (sp == 4)
                    {
                        seac = true;
                        seacX = stack[0], seacY = stack[1];
                        seacBase = u32(stack[2]) & 255, seacAccent = u32(stack[3]) & 255;
                    }
                    sp = 0;
                    done = true;
                    break;
                case 12:
                {
                    u8 e = cs.U8(at++);
                    if (!Escape(e))
                        return false;
                    break;
                }
                default: return Fail(Format("charstring operator {}", b));
                }
            }
            return true;
        }

        bool Need(u32 n)
        {
            if (sp < n)
                return Fail("an operator without its arguments");
            return true;
        }

        bool Escape(u8 e)
        {
            switch (e)
            {
            case 35:
                if (!Need(13))
                    return false;
                CurveTo(stack[0], stack[1], stack[2], stack[3], stack[4], stack[5]);
                CurveTo(stack[6], stack[7], stack[8], stack[9], stack[10], stack[11]);
                sp = 0;
                return true;
            case 34:
                if (!Need(7))
                    return false;
                CurveTo(stack[0], 0, stack[1], stack[2], stack[3], 0);
                CurveTo(stack[4], 0, stack[5], -stack[2], stack[6], 0);
                sp = 0;
                return true;
            case 36:
                if (!Need(9))
                    return false;
                CurveTo(stack[0], stack[1], stack[2], stack[3], stack[4], 0);
                CurveTo(stack[5], 0, stack[6], stack[7], stack[8], -(stack[1] + stack[3] + stack[7]));
                sp = 0;
                return true;
            case 37:
            {
                if (!Need(11))
                    return false;
                float dx = 0, dy = 0;
                for (u32 i = 0; i < 10; i += 2)
                    dx += stack[i], dy += stack[i + 1];
                CurveTo(stack[0], stack[1], stack[2], stack[3], stack[4], stack[5]);
                if (std::fabs(dx) > std::fabs(dy))
                    CurveTo(stack[6], stack[7], stack[8], stack[9], stack[10], -dy);
                else
                    CurveTo(stack[6], stack[7], stack[8], stack[9], -dx, stack[10]);
                sp = 0;
                return true;
            }
            // Arithmetic and storage.
            case 3:
            case 4:
            case 10:
            case 11:
            case 12:
            case 15:
            case 24:
            {
                if (!Need(2))
                    return false;
                float a = stack[sp - 2], c = stack[sp - 1], r;
                switch (e)
                {
                case 3: r = (a != 0 && c != 0) ? 1.0f : 0.0f; break;
                case 4: r = (a != 0 || c != 0) ? 1.0f : 0.0f; break;
                case 10: r = a + c; break;
                case 11: r = a - c; break;
                case 12: r = c != 0 ? a / c : 0; break;
                case 15: r = a == c ? 1.0f : 0.0f; break;
                default: r = a * c; break;
                }
                sp -= 2;
                stack[sp++] = r;
                return true;
            }
            case 5:
            case 9:
            case 14:
            case 26:
            {
                if (!Need(1))
                    return false;
                float& a = stack[sp - 1];
                a = e == 5 ? (a == 0 ? 1.0f : 0.0f) : e == 9 ? std::fabs(a) : e == 14 ? -a : std::sqrt(std::fabs(a));
                return true;
            }
            case 18:
                if (!Need(1))
                    return false;
                sp--;
                return true;
            case 20:
            {
                if (!Need(2))
                    return false;
                i32 i = i32(stack[sp - 1]);
                if (i >= 0 && i < 32)
                    transient[i] = stack[sp - 2];
                sp -= 2;
                return true;
            }
            case 21:
            {
                if (!Need(1))
                    return false;
                i32 i = i32(stack[sp - 1]);
                stack[sp - 1] = i >= 0 && i < 32 ? transient[i] : 0;
                return true;
            }
            case 22:
            {
                if (!Need(4))
                    return false;
                float r = stack[sp - 2] <= stack[sp - 1] ? stack[sp - 4] : stack[sp - 3];
                sp -= 4;
                stack[sp++] = r;
                return true;
            }
            case 23:
                if (sp >= kMaxStack)
                    return Fail("a charstring stack overflow");
                stack[sp++] = 0.5f;
                return true;
            case 27:
                if (!Need(1) || sp >= kMaxStack)
                    return Fail("dup without room");
                stack[sp] = stack[sp - 1];
                sp++;
                return true;
            case 28:
            {
                if (!Need(2))
                    return false;
                float t = stack[sp - 1];
                stack[sp - 1] = stack[sp - 2];
                stack[sp - 2] = t;
                return true;
            }
            case 29:
            {
                if (!Need(1))
                    return false;
                i32 i = i32(stack[sp - 1]);
                if (i < 0)
                    i = 0;
                stack[sp - 1] = u32(i) + 1 < sp ? stack[sp - 2 - u32(i)] : 0;
                return true;
            }
            case 30:
            {
                if (!Need(2))
                    return false;
                i32 n = i32(stack[sp - 2]), j = i32(stack[sp - 1]);
                sp -= 2;
                if (n <= 0 || u32(n) > sp)
                    return true;
                j = ((j % n) + n) % n;
                float temp[kMaxStack];
                float* base = stack + sp - n;
                for (i32 i = 0; i < n; i++)
                    temp[(i + j) % n] = base[i];
                for (i32 i = 0; i < n; i++)
                    base[i] = temp[i];
                return true;
            }
            default: return Fail(Format("charstring operator 12 {}", e));
            }
        }
    };

    bool Outlines::GetCff(u32 glyph, Outline& out, String& error) const
    {
        const CffPrivate& p = privates_[cid_ ? fdSelect_[glyph] : 0];
        CharstringRunner r{*this, out, error};
        r.local = &p.subrs;
        if (!r.Run(IndexItem(charStrings_, glyph), 0))
            return error = Format("glyph {}: {}", glyph, error), false;
        if (!r.seac)
            return true;
        // An accented glyph made of two Standard Encoding glyphs.
        u32 base = CffGlyphOfSid(kStandardEncoding[r.seacBase]), accent = CffGlyphOfSid(kStandardEncoding[r.seacAccent]);
        if (base == 0 || accent == 0)
            return error = Format("glyph {}: an accent of glyphs the font does not have", glyph), false;
        out.Clear();
        CharstringRunner b{*this, out, error};
        b.local = &privates_[cid_ ? fdSelect_[base] : 0].subrs;
        if (!b.Run(IndexItem(charStrings_, base), 0) || b.seac)
            return error = Format("glyph {}: a damaged accent base", glyph), false;
        Outline mark;
        CharstringRunner a{*this, mark, error};
        a.local = &privates_[cid_ ? fdSelect_[accent] : 0].subrs;
        if (!a.Run(IndexItem(charStrings_, accent), 0) || a.seac)
            return error = Format("glyph {}: a damaged accent", glyph), false;
        out.Append(mark, 1, 0, 0, 1, r.seacX, r.seacY);
        return true;
    }
}
