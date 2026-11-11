#pragma once

#include <Support/FontWriter.h>

// GSUB, GPOS and GDEF tables for the tests, built from subtables given as bytes.
namespace helix::font::testing
{
    inline Array<u8> Coverage1(const Array<u16>& glyphs)
    {
        Blob b;
        b.U16(1).U16(u32(glyphs.Size()));
        for (u16 g : glyphs)
            b.U16(g);
        return b.bytes;
    }

    // Ranges of first, last.
    inline Array<u8> Coverage2(const Array<Array<u16>>& ranges)
    {
        Blob b;
        b.U16(2).U16(u32(ranges.Size()));
        u32 index = 0;
        for (const Array<u16>& r : ranges)
        {
            b.U16(r[0]).U16(r[1]).U16(index);
            index += u32(r[1] - r[0]) + 1;
        }
        return b.bytes;
    }

    // Ranges of first, last, class.
    inline Array<u8> ClassDef2(const Array<Array<u16>>& ranges)
    {
        Blob b;
        b.U16(2).U16(u32(ranges.Size()));
        for (const Array<u16>& r : ranges)
            b.U16(r[0]).U16(r[1]).U16(r[2]);
        return b.bytes;
    }

    inline Array<u8> Anchor1(i32 x, i32 y)
    {
        Blob b;
        b.U16(1).I16(x).I16(y);
        return b.bytes;
    }

    struct LookupSpec
    {
        u16 type = 1;
        u16 flags = 0;
        u16 markSet = 0;
        Array<Array<u8>> subtables;
    };

    // One script 'latn' with a default language (features 0 and 1) and 'TRK ' (required feature 1, feature 0), feature
    // 0 'liga' with every lookup and feature 1 'kern' with lookup 0.
    inline Array<u8> LayoutTable(const Array<LookupSpec>& lookups)
    {
        Blob dflt, trk;
        dflt.U16(0).U16(0xFFFF).U16(2).U16(0).U16(1);
        trk.U16(0).U16(1).U16(1).U16(0);
        Blob script;
        script.Offset16(dflt).U16(1).Tag("TRK ").Offset16(trk);
        Blob scripts;
        scripts.U16(1).Tag("latn").Offset16(script);

        Blob liga, kern;
        liga.U16(0).U16(u32(lookups.Size()));
        for (usize i = 0; i < lookups.Size(); i++)
            liga.U16(u32(i));
        kern.U16(0).U16(1).U16(0);
        Blob features;
        features.U16(2).Tag("liga").Offset16(liga).Tag("kern").Offset16(kern);

        Blob list;
        list.U16(u32(lookups.Size()));
        for (const LookupSpec& l : lookups)
        {
            Blob lookup;
            lookup.U16(l.type).U16(l.flags).U16(u32(l.subtables.Size()));
            for (const Array<u8>& s : l.subtables)
                lookup.Offset16(s);
            if (l.flags & 0x10)
                lookup.U16(l.markSet);
            list.Offset16(lookup);
        }
        Blob table;
        table.U16(1).U16(0).Offset16(scripts).Offset16(features).Offset16(list);
        return table.Flatten();
    }

    // An extension subtable holding a subtable of another type.
    inline Array<u8> Extension(u16 type, const Array<u8>& subtable)
    {
        Blob b;
        b.U16(1).U16(type).Offset32(subtable);
        return b.Flatten();
    }
}
