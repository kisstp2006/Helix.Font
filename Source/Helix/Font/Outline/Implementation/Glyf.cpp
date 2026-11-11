#include <Helix/Font/Outline/Outline.h>

#include <Helix/Core/Strings/Format.h>

#include <cmath>

namespace helix::font
{
    void Outline::Clear()
    {
        verbs.Clear();
        points.Clear();
        xMin = yMin = xMax = yMax = 0;
    }

    void Outline::MoveTo(Point p)
    {
        verbs.Push(Verb::Move);
        points.Push(p);
    }

    void Outline::LineTo(Point p)
    {
        verbs.Push(Verb::Line);
        points.Push(p);
    }

    void Outline::QuadTo(Point c, Point p)
    {
        verbs.Push(Verb::Quad);
        points.Push(c);
        points.Push(p);
    }

    void Outline::CubicTo(Point c1, Point c2, Point p)
    {
        verbs.Push(Verb::Cubic);
        points.Push(c1);
        points.Push(c2);
        points.Push(p);
    }

    void Outline::ComputeBounds()
    {
        if (points.Empty())
        {
            xMin = yMin = xMax = yMax = 0;
            return;
        }
        xMin = xMax = points[0].x;
        yMin = yMax = points[0].y;
        for (const Point& p : points)
        {
            xMin = p.x < xMin ? p.x : xMin;
            xMax = p.x > xMax ? p.x : xMax;
            yMin = p.y < yMin ? p.y : yMin;
            yMax = p.y > yMax ? p.y : yMax;
        }
    }

    bool Outline::Finite() const
    {
        for (const Point& p : points)
            if (!std::isfinite(p.x) || !std::isfinite(p.y))
                return false;
        return true;
    }

    u32 Outline::ContourCount() const
    {
        u32 n = 0;
        for (Verb v : verbs)
            n += v == Verb::Move;
        return n;
    }

    void Outline::Append(const Outline& other, float a, float b, float c, float d, float dx, float dy)
    {
        verbs.Append(other.verbs.Data(), other.verbs.Size());
        points.Reserve(points.Size() + other.points.Size());
        for (const Point& p : other.points)
            points.Push({a * p.x + c * p.y + dx, b * p.x + d * p.y + dy});
    }

    namespace
    {
        // Composite glyphs nest at most this deep, and no glyph has more points.
        constexpr u32 kMaxDepth = 16;
        constexpr usize kMaxPoints = 1u << 20;

        enum : u16
        {
            kArgsAreWords = 1,
            kArgsAreXY = 2,
            kHaveScale = 8,
            kMoreComponents = 32,
            kHaveXYScale = 64,
            kHaveTwoByTwo = 128,
            kScaledOffset = 2048,
        };
    }

    bool Outlines::Open(const Font& font, String& error)
    {
        *this = Outlines();
        font_ = &font;
        if (font.IsCff())
        {
            cff_ = true;
            return ReadCff(error);
        }
        glyf_ = font.Table(Tag("glyf"));
        loca_ = font.Table(Tag("loca"));
        longLoca_ = font.Head().indexToLocFormat != 0;
        if (!glyf_.Empty() && loca_.size < (usize(font.GlyphCount()) + 1) * (longLoca_ ? 4 : 2))
            return error = "a loca table shorter than the glyph count", false;
        return true;
    }

    bool Outlines::Get(u32 glyph, Outline& out, String& error) const
    {
        out.Clear();
        if (!font_ || glyph >= font_->GlyphCount())
            return error = Format("glyph {} of {}", glyph, font_ ? font_->GlyphCount() : 0), false;
        bool ok = cff_ ? GetCff(glyph, out, error) : GetGlyf(glyph, out, error);
        if (!ok)
            out.Clear();
        out.ComputeBounds();
        return ok;
    }

    bool Outlines::GlyfPoints(u32 glyph, TtGlyph& out, u32 depth, String& error) const
    {
        if (glyf_.Empty())
            return true;
        usize begin = longLoca_ ? loca_.U32(4 * usize(glyph)) : usize(loca_.U16(2 * usize(glyph))) * 2;
        usize end = longLoca_ ? loca_.U32(4 * usize(glyph) + 4) : usize(loca_.U16(2 * usize(glyph) + 2)) * 2;
        if (end <= begin)
            return true;
        Bytes g = glyf_.Sub(begin, end - begin);
        if (g.size < 10)
            return error = Format("glyph {}: data outside the glyf table", glyph), false;
        i16 contours = g.I16(0);
        if (contours >= 0)
        {
            if (!g.Has(10, usize(contours) * 2 + 2))
                return error = Format("glyph {}: contour ends outside its data", glyph), false;
            u32 firstPoint = u32(out.points.Size());
            u32 count = 0;
            for (i32 c = 0; c < contours; c++)
            {
                u32 e = u32(g.U16(10 + 2 * usize(c))) + 1;
                if (e < count)
                    return error = Format("glyph {}: contour ends out of order", glyph), false;
                count = e;
                out.ends.Push(firstPoint + e - 1);
            }
            if (out.points.Size() + count > kMaxPoints)
                return error = Format("glyph {}: too many points", glyph), false;
            usize at = 10 + usize(contours) * 2;
            at += 2 + g.U16(at);
            // Flags, with repeats.
            Array<u8> flags;
            flags.Reserve(count);
            while (flags.Size() < count)
            {
                if (!g.Has(at, 1))
                    return error = Format("glyph {}: flags outside its data", glyph), false;
                u8 f = g.U8(at++);
                flags.Push(f);
                if (f & 8)
                {
                    u8 repeat = g.U8(at++);
                    for (u32 r = 0; r < repeat && flags.Size() < count; r++)
                        flags.Push(f);
                }
            }
            // X then Y deltas.
            out.points.Resize(firstPoint + count);
            for (int axis = 0; axis < 2; axis++)
            {
                u8 shortBit = axis == 0 ? 2 : 4, sameBit = axis == 0 ? 16 : 32;
                i32 v = 0;
                for (u32 i = 0; i < count; i++)
                {
                    u8 f = flags[i];
                    if (f & shortBit)
                    {
                        if (!g.Has(at, 1))
                            return error = Format("glyph {}: coordinates outside its data", glyph), false;
                        i32 d = g.U8(at++);
                        v += (f & sameBit) ? d : -d;
                    }
                    else if (!(f & sameBit))
                    {
                        if (!g.Has(at, 2))
                            return error = Format("glyph {}: coordinates outside its data", glyph), false;
                        v += g.I16(at);
                        at += 2;
                    }
                    TtPoint& p = out.points[firstPoint + i];
                    (axis == 0 ? p.x : p.y) = float(v);
                    p.on = (f & 1) != 0;
                }
            }
            return true;
        }

        // A composite: components one after another.
        if (depth >= kMaxDepth)
            return error = Format("glyph {}: composite glyphs nested too deep", glyph), false;
        usize at = 10;
        u16 flags;
        do
        {
            if (!g.Has(at, 4))
                return error = Format("glyph {}: a component outside its data", glyph), false;
            flags = g.U16(at);
            u16 component = g.U16(at + 2);
            at += 4;
            i32 arg1, arg2;
            if (flags & kArgsAreWords)
            {
                arg1 = (flags & kArgsAreXY) ? g.I16(at) : g.U16(at);
                arg2 = (flags & kArgsAreXY) ? g.I16(at + 2) : g.U16(at + 2);
                at += 4;
            }
            else
            {
                arg1 = (flags & kArgsAreXY) ? g.I8(at) : g.U8(at);
                arg2 = (flags & kArgsAreXY) ? g.I8(at + 1) : g.U8(at + 1);
                at += 2;
            }
            float a = 1, b = 0, c = 0, d = 1;
            if (flags & kHaveScale)
                a = d = g.F2Dot14(at), at += 2;
            else if (flags & kHaveXYScale)
                a = g.F2Dot14(at), d = g.F2Dot14(at + 2), at += 4;
            else if (flags & kHaveTwoByTwo)
                a = g.F2Dot14(at), b = g.F2Dot14(at + 2), c = g.F2Dot14(at + 4), d = g.F2Dot14(at + 6), at += 8;
            if (!g.Has(0, at))
                return error = Format("glyph {}: a component outside its data", glyph), false;
            if (component >= font_->GlyphCount())
                return error = Format("glyph {}: component glyph {} of {}", glyph, component, font_->GlyphCount()), false;

            TtGlyph part;
            if (!GlyfPoints(component, part, depth + 1, error))
                return false;
            if (out.points.Size() + part.points.Size() > kMaxPoints)
                return error = Format("glyph {}: too many points", glyph), false;
            for (TtPoint& p : part.points)
            {
                float x = p.x, y = p.y;
                p.x = a * x + c * y;
                p.y = b * x + d * y;
            }
            float dx, dy;
            if (flags & kArgsAreXY)
            {
                dx = float(arg1), dy = float(arg2);
                if (flags & kScaledOffset)
                {
                    float x = dx, y = dy;
                    dx = a * x + c * y;
                    dy = b * x + d * y;
                }
            }
            else
            {
                // Point matching: the component's point arg2 lands on the composite's point arg1.
                if (u32(arg1) >= out.points.Size() || u32(arg2) >= part.points.Size())
                    return error = Format("glyph {}: matched points {} and {} outside the glyphs", glyph, arg1, arg2), false;
                dx = out.points[usize(arg1)].x - part.points[usize(arg2)].x;
                dy = out.points[usize(arg1)].y - part.points[usize(arg2)].y;
            }
            u32 base = u32(out.points.Size());
            for (const TtPoint& p : part.points)
                out.points.Push({p.x + dx, p.y + dy, p.on});
            for (u32 e : part.ends)
                out.ends.Push(base + e);
        } while (flags & kMoreComponents);
        return true;
    }

    bool Outlines::GetGlyf(u32 glyph, Outline& out, String& error) const
    {
        TtGlyph g;
        if (!GlyfPoints(glyph, g, 0, error))
            return false;
        u32 start = 0;
        for (u32 endIndex : g.ends)
        {
            u32 end = u32(endIndex) + 1;
            if (end <= start || end > g.points.Size())
            {
                start = end > start ? end : start;
                continue;
            }
            u32 n = end - start;
            const TtPoint* p = g.points.Data() + start;
            auto mid = [](Point a, Point b) { return Point{(a.x + b.x) / 2, (a.y + b.y) / 2}; };
            u32 first = 0;
            while (first < n && !p[first].on)
                first++;
            bool haveControl = false;
            Point control;
            if (first == n)
            {
                // Only off-curve points: start between the last and the first.
                Point origin = mid({p[n - 1].x, p[n - 1].y}, {p[0].x, p[0].y});
                out.MoveTo(origin);
                for (u32 k = 0; k < n; k++)
                {
                    Point q{p[k].x, p[k].y};
                    if (haveControl)
                        out.QuadTo(control, mid(control, q));
                    control = q;
                    haveControl = true;
                }
                out.QuadTo(control, origin);
            }
            else
            {
                out.MoveTo({p[first].x, p[first].y});
                // Around the contour back to the first on-curve point; two off-curve points imply one on the middle.
                for (u32 k = 1; k <= n; k++)
                {
                    const TtPoint& tp = p[(first + k) % n];
                    Point q{tp.x, tp.y};
                    if (tp.on)
                    {
                        if (haveControl)
                            out.QuadTo(control, q);
                        else
                            out.LineTo(q);
                        haveControl = false;
                    }
                    else
                    {
                        if (haveControl)
                            out.QuadTo(control, mid(control, q));
                        control = q;
                        haveControl = true;
                    }
                }
            }
            start = end;
        }
        return true;
    }
}
