#pragma once

#include <Helix/Core/Basics/Span.h>

namespace helix::font
{
    // A table tag as the four characters read big endian: Tag("glyf").
    constexpr u32 Tag(const char (&name)[5]) { return u32(u8(name[0])) << 24 | u32(u8(name[1])) << 16 | u32(u8(name[2])) << 8 | u32(u8(name[3])); }

    // A view of big endian bytes. Every read is checked: outside the view it gives 0, so damaged offsets read zeros and
    // never memory around the font. Counts read from the data are checked with Has before they drive a loop.
    struct Bytes
    {
        const u8* data = nullptr;
        usize size = 0;

        constexpr Bytes() = default;
        constexpr Bytes(const u8* d, usize n) : data(d), size(n) {}
        Bytes(Span<const u8> s) : data(s.Data()), size(s.Size()) {}

        bool Empty() const { return size == 0; }
        bool Has(usize at, usize count) const { return at <= size && count <= size - at; }

        u8 U8(usize at) const { return at < size ? data[at] : 0; }
        i8 I8(usize at) const { return i8(U8(at)); }
        u16 U16(usize at) const { return Has(at, 2) ? u16(data[at] << 8 | data[at + 1]) : 0; }
        i16 I16(usize at) const { return i16(U16(at)); }
        u32 U24(usize at) const { return Has(at, 3) ? u32(data[at]) << 16 | u32(data[at + 1]) << 8 | data[at + 2] : 0; }
        u32 U32(usize at) const { return Has(at, 4) ? u32(data[at]) << 24 | u32(data[at + 1]) << 16 | u32(data[at + 2]) << 8 | data[at + 3] : 0; }
        i32 I32(usize at) const { return i32(U32(at)); }
        // 16.16 fixed point.
        float Fixed(usize at) const { return float(I32(at)) / 65536.0f; }
        // 2.14 fixed point.
        float F2Dot14(usize at) const { return float(I16(at)) / 16384.0f; }

        // The bytes from `at` (to the end, or `count` of them), empty when outside.
        Bytes From(usize at) const { return at <= size ? Bytes(data + at, size - at) : Bytes(); }
        Bytes Sub(usize at, usize count) const { return Has(at, count) ? Bytes(data + at, count) : Bytes(); }
    };
}
