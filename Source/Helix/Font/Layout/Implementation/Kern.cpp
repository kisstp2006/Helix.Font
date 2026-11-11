#include <Helix/Font/Layout/Kern.h>

#include <Helix/Core/Strings/Format.h>

#include <algorithm>

namespace helix::font
{
    bool KernTable::Read(const Font& font, String& error)
    {
        pairs_.Clear();
        Bytes k = font.Table(Tag("kern"));
        if (k.Empty())
            return true;
        bool apple = k.U32(0) == 0x00010000;
        u32 tables = apple ? k.U32(4) : k.U16(2);
        usize at = apple ? 8 : 4;
        for (u32 i = 0; i < tables && at < k.size; i++)
        {
            usize length, header;
            u16 coverage;
            u8 format;
            bool horizontal;
            if (apple)
            {
                length = k.U32(at);
                coverage = k.U16(at + 4);
                header = 8;
                format = u8(coverage & 0xFF);
                horizontal = (coverage & 0xE000) == 0;
            }
            else
            {
                length = k.U16(at + 2);
                coverage = k.U16(at + 4);
                header = 6;
                format = u8(coverage >> 8);
                horizontal = (coverage & 1) != 0 && (coverage & 4) == 0;
            }
            if (length < header || !k.Has(at, header))
                return error = Format("kern: subtable {} has length {}", i, length), false;
            if (format == 0 && horizontal)
            {
                u16 n = k.U16(at + header);
                usize pairs = at + header + 8;
                if (!k.Has(pairs, usize(n) * 6))
                    return error = Format("kern: the pairs of subtable {} are outside the table", i), false;
                pairs_.Reserve(pairs_.Size() + n);
                for (u32 p = 0; p < n; p++)
                {
                    usize e = pairs + usize(p) * 6;
                    pairs_.Push({u32(k.U16(e)) << 16 | k.U16(e + 2), k.I16(e + 4)});
                }
            }
            // The pair count, not the length, sizes a format 0 subtable: a Microsoft length is 16 bits and overflows in
            // large ones, which then come last.
            at += length;
        }
        std::sort(pairs_.begin(), pairs_.end(), [](const Pair& a, const Pair& b) { return a.key < b.key; });
        // Pairs listed by several subtables add up.
        usize w = 0;
        for (usize r = 0; r < pairs_.Size(); r++)
        {
            if (w > 0 && pairs_[w - 1].key == pairs_[r].key)
                pairs_[w - 1].value += pairs_[r].value;
            else
                pairs_[w++] = pairs_[r];
        }
        pairs_.Resize(w);
        return true;
    }

    i32 KernTable::Get(u32 left, u32 right) const
    {
        u32 key = left << 16 | (right & 0xFFFF);
        const Pair* it = std::lower_bound(pairs_.begin(), pairs_.end(), key, [](const Pair& p, u32 k) { return p.key < k; });
        return it != pairs_.end() && it->key == key ? it->value : 0;
    }
}
