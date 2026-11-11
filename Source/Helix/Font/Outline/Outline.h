#pragma once

#include <Helix/Font/Font/Font.h>

namespace helix::font
{
    enum class Verb : u8
    {
        // Starts a contour; every contour is closed back to its first point.
        Move,
        Line,
        // A control point and the end point.
        Quad,
        // Two control points and the end point.
        Cubic,
    };

    struct Point
    {
        float x = 0, y = 0;

        bool operator==(const Point&) const = default;
    };

    // The contours of a glyph in font units, y up: one verb per segment and its points in one array (a Move or Line has
    // one point, a Quad two, a Cubic three).
    struct Outline
    {
        Array<Verb> verbs;
        Array<Point> points;
        // The box of all points, control points included; zero for an empty glyph.
        float xMin = 0, yMin = 0, xMax = 0, yMax = 0;

        void Clear();
        bool Empty() const { return verbs.Empty(); }
        void MoveTo(Point p);
        void LineTo(Point p);
        void QuadTo(Point c, Point p);
        void CubicTo(Point c1, Point c2, Point p);
        void ComputeBounds();
        u32 ContourCount() const;
        // Whether every point is a finite number (damaged CFF arithmetic can make infinities).
        bool Finite() const;
        // Appends another outline, scaled by (a, b, c, d) as x' = a x + c y + dx, y' = b x + d y + dy.
        void Append(const Outline& other, float a, float b, float c, float d, float dx, float dy);
    };

    // Reads glyph outlines: glyf / loca (simple and composite glyphs) or CFF (Type 2 charstrings). Open reads the
    // indexes once; Get then reads one glyph. Damaged glyph data gives an error for that glyph only.
    class Outlines
    {
    public:
        bool Open(const Font& font, String& error);
        bool Get(u32 glyph, Outline& out, String& error) const;
        // A glyph with no contours (space) reads as an empty outline and true.

    private:
        struct CffIndex
        {
            usize offsets = 0;
            usize data = 0;
            u32 count = 0;
            u8 offSize = 0;
        };
        struct CffPrivate
        {
            CffIndex subrs;
            float nominalWidth = 0;
        };
        struct TtPoint
        {
            float x, y;
            bool on;
        };
        struct TtGlyph
        {
            Array<TtPoint> points;
            Array<u32> ends;
        };

        bool ReadCff(String& error);
        bool ReadIndex(usize at, CffIndex& out, usize& end) const;
        Bytes IndexItem(const CffIndex& index, u32 i) const;
        bool ReadPrivate(Bytes dict, CffPrivate& out) const;
        bool GlyfPoints(u32 glyph, TtGlyph& out, u32 depth, String& error) const;
        bool GetGlyf(u32 glyph, Outline& out, String& error) const;
        bool GetCff(u32 glyph, Outline& out, String& error) const;
        u32 CffGlyphOfSid(u32 sid) const;

        const Font* font_ = nullptr;
        bool cff_ = false;
        // glyf.
        Bytes glyf_;
        Bytes loca_;
        bool longLoca_ = false;
        // CFF.
        Bytes cffTable_;
        CffIndex charStrings_;
        CffIndex globalSubrs_;
        Array<CffPrivate> privates_;
        // The Private DICT of each glyph (one entry for fonts that are not CID keyed).
        Array<u8> fdSelect_;
        usize charset_ = 0;
        bool cid_ = false;
        friend struct CharstringRunner;
    };
}
