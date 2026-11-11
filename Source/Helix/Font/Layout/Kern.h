#pragma once

#include <Helix/Font/Font/Font.h>

namespace helix::font
{
    // The horizontal pairs of the kern table (format 0 subtables of the Microsoft and the Apple header), merged and sorted.
    class KernTable
    {
    public:
        struct Pair
        {
            // left << 16 | right.
            u32 key;
            i32 value;
        };

        // False with an error when the table is damaged; a font without one reads as empty.
        bool Read(const Font& font, String& error);
        // The adjustment in font units, 0 when the pair is not listed.
        i32 Get(u32 left, u32 right) const;
        const Array<Pair>& Pairs() const { return pairs_; }

    private:
        Array<Pair> pairs_;
    };
}
