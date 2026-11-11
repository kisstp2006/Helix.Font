#include <Helix/Font/Raster/Raster.h>

#include <Helix/Core/Strings/Format.h>

#include <algorithm>
#include <cmath>

// Signed and multi channel signed distance fields, written from Viktor Chlumsky's thesis "Shape Decomposition for
// Multi-channel Distance Fields" (2015): exact distances to the segments, pseudo-distances, edge coloring at corners and
// the correction of clashing texels.
namespace helix::font
{
    namespace
    {
        constexpr usize kMaxSegments = 1u << 16;

        struct V2
        {
            double x = 0, y = 0;
        };
        V2 operator+(V2 a, V2 b) { return {a.x + b.x, a.y + b.y}; }
        V2 operator-(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
        V2 operator*(V2 a, double s) { return {a.x * s, a.y * s}; }
        double Dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
        double Cross(V2 a, V2 b) { return a.x * b.y - a.y * b.x; }
        double Length(V2 a) { return std::sqrt(Dot(a, a)); }
        V2 Normalize(V2 a)
        {
            double l = Length(a);
            return l > 0 ? a * (1 / l) : V2{0, 1};
        }

        enum Color : u8
        {
            kRed = 1,
            kGreen = 2,
            kBlue = 4,
            kYellow = kRed | kGreen,
            kMagenta = kRed | kBlue,
            kCyan = kGreen | kBlue,
            kWhite = 7,
        };

        struct Segment
        {
            // 1 line, 2 quadratic, 3 cubic: the degree, and degree + 1 points.
            u8 degree = 1;
            u8 color = kWhite;
            V2 p[4];
            double minX = 0, minY = 0, maxX = 0, maxY = 0;

            V2 At(double t) const
            {
                double s = 1 - t;
                switch (degree)
                {
                case 1: return p[0] * s + p[1] * t;
                case 2: return p[0] * (s * s) + p[1] * (2 * s * t) + p[2] * (t * t);
                default: return p[0] * (s * s * s) + p[1] * (3 * s * s * t) + p[2] * (3 * s * t * t) + p[3] * (t * t * t);
                }
            }
            V2 Tangent(double t) const
            {
                double s = 1 - t;
                V2 d;
                switch (degree)
                {
                case 1: d = p[1] - p[0]; break;
                case 2: d = (p[1] - p[0]) * (2 * s) + (p[2] - p[1]) * (2 * t); break;
                default: d = (p[1] - p[0]) * (3 * s * s) + (p[2] - p[1]) * (6 * s * t) + (p[3] - p[2]) * (3 * t * t); break;
                }
                // A degenerate control point: the direction to the next distinct point.
                if (Dot(d, d) < 1e-18)
                    d = p[degree] - p[0];
                return d;
            }
            V2 Second(double t) const
            {
                if (degree == 2)
                    return (p[2] - p[1] * 2 + p[0]) * 2;
                if (degree == 3)
                    return (p[2] - p[1] * 2 + p[0]) * (6 * (1 - t)) + (p[3] - p[2] * 2 + p[1]) * (6 * t);
                return {};
            }
            void Bounds()
            {
                minX = maxX = p[0].x;
                minY = maxY = p[0].y;
                for (u8 i = 1; i <= degree; i++)
                {
                    minX = std::min(minX, p[i].x), maxX = std::max(maxX, p[i].x);
                    minY = std::min(minY, p[i].y), maxY = std::max(maxY, p[i].y);
                }
            }
            // The two halves at t (de Casteljau).
            void Split(double t, Segment& a, Segment& b) const
            {
                a = *this, b = *this;
                V2 q[4] = {p[0], p[1], p[2], p[3]};
                V2 left[4], right[4];
                u8 n = degree;
                for (u8 level = 0; level <= n; level++)
                {
                    left[level] = q[0];
                    right[n - level] = q[n - level];
                    for (u8 i = 0; i + level < n; i++)
                        q[i] = q[i] * (1 - t) + q[i + 1] * t;
                }
                for (u8 i = 0; i <= n; i++)
                    a.p[i] = left[i], b.p[i] = right[i];
                a.Bounds(), b.Bounds();
            }
        };

        // How far v lies outside [lo, hi].
        inline double BoxGap(double lo, double hi, double v) { return v < lo ? lo - v : v > hi ? v - hi : 0.0; }

        // The closest point of a segment: its distance and parameter.
        struct Closest
        {
            double distance = 1e300;
            double t = 0;
        };

        // Real roots of a t^3 + b t^2 + c t + d in [0, 1].
        int SolveCubic(double a, double b, double c, double d, double out[3])
        {
            int n = 0;
            auto keep = [&](double t) {
                if (t >= 0 && t <= 1)
                    out[n++] = t;
            };
            if (std::fabs(a) < 1e-12)
            {
                if (std::fabs(b) < 1e-12)
                {
                    if (std::fabs(c) > 1e-12)
                        keep(-d / c);
                    return n;
                }
                double disc = c * c - 4 * b * d;
                if (disc < 0)
                    return n;
                double s = std::sqrt(disc);
                keep((-c + s) / (2 * b));
                keep((-c - s) / (2 * b));
                return n;
            }
            b /= a, c /= a, d /= a;
            double q = (b * b - 3 * c) / 9, r = (2 * b * b * b - 9 * b * c + 27 * d) / 54;
            double q3 = q * q * q;
            if (r * r < q3)
            {
                double theta = std::acos(std::clamp(r / std::sqrt(q3), -1.0, 1.0));
                double m = -2 * std::sqrt(q);
                keep(m * std::cos(theta / 3) - b / 3);
                keep(m * std::cos((theta + 2 * 3.14159265358979323846) / 3) - b / 3);
                keep(m * std::cos((theta - 2 * 3.14159265358979323846) / 3) - b / 3);
                return n;
            }
            double A = -std::cbrt(std::fabs(r) + std::sqrt(r * r - q3));
            if (r < 0)
                A = -A;
            double B = A == 0 ? 0 : q / A;
            keep(A + B - b / 3);
            return n;
        }

        // How orthogonal the direction from the closest point to p is to the segment there: of two segments at the same
        // distance, the more orthogonal one is the true edge (the other meets it at a corner).
        double Ortho(const Segment& s, double t, V2 p)
        {
            V2 dir = Normalize(p - s.At(t)), tan = Normalize(s.Tangent(t));
            return std::fabs(Cross(tan, dir));
        }

        Closest Nearest(const Segment& s, V2 p)
        {
            if (s.degree == 1)
            {
                V2 ab = s.p[1] - s.p[0];
                double l = Dot(ab, ab);
                double t = l > 0 ? std::clamp(Dot(p - s.p[0], ab) / l, 0.0, 1.0) : 0;
                return {Length(p - (s.p[0] + ab * t)), t};
            }
            double bestSquared = 1e300, bestT = 0;
            auto consider = [&](double t) {
                V2 q = s.At(t) - p;
                double d = Dot(q, q);
                if (d < bestSquared)
                    bestSquared = d, bestT = t;
            };
            consider(0);
            consider(1);
            if (s.degree == 2)
            {
                V2 a = s.p[1] - s.p[0], b = s.p[2] - s.p[1] * 2 + s.p[0], w = s.p[0] - p;
                double roots[3];
                int n = SolveCubic(Dot(b, b), 3 * Dot(a, b), 2 * Dot(a, a) + Dot(w, b), Dot(w, a), roots);
                for (int i = 0; i < n; i++)
                    consider(roots[i]);
            }
            else
            {
                // Starts along the curve refined by Newton's method on (B(t) - p) . B'(t).
                for (int i = 0; i <= 8; i++)
                {
                    double t = i / 8.0;
                    for (int k = 0; k < 6; k++)
                    {
                        V2 q = s.At(t) - p, d1 = s.Tangent(t), d2 = s.Second(t);
                        double f = Dot(q, d1), df = Dot(d1, d1) + Dot(q, d2);
                        if (std::fabs(df) < 1e-18)
                            break;
                        t = std::clamp(t - f / df, 0.0, 1.0);
                    }
                    consider(t);
                }
            }
            return {std::sqrt(bestSquared), bestT};
        }

        // The signed pseudo-distance at the closest point: beyond an end, the distance to the end's tangent line. The
        // sign follows the segment's direction (left positive).
        double PseudoDistance(const Segment& s, const Closest& c, V2 p)
        {
            V2 q = s.At(c.t), tan = Normalize(s.Tangent(c.t));
            double side = Cross(tan, p - q);
            double sign = side >= 0 ? 1 : -1;
            if (c.t <= 0 || c.t >= 1)
            {
                double along = Dot(p - q, tan);
                if ((c.t <= 0 && along < 0) || (c.t >= 1 && along > 0))
                    return side;
            }
            return sign * c.distance;
        }

        struct Shape
        {
            Array<Segment> segments;
            // Contours as ranges of segments.
            Array<u32> contourStarts;
            // Flattened edges for the fill.
            Array<V2> polyline;
            Array<u32> polyStarts;
        };

        bool BuildShape(const Outline& o, double scale, double ox, double oy, Shape& shape, String& error)
        {
            auto map = [&](Point p) { return V2{double(p.x) * scale - ox, -double(p.y) * scale - oy}; };
            usize p = 0;
            V2 start, last;
            bool open = false;
            auto add = [&](u8 degree, const V2* pts) {
                Segment s;
                s.degree = degree;
                s.p[0] = last;
                for (u8 i = 1; i <= degree; i++)
                    s.p[i] = pts[i - 1];
                bool degenerate = true;
                for (u8 i = 1; i <= degree; i++)
                    degenerate = degenerate && Length(s.p[i] - s.p[0]) < 1e-9;
                last = s.p[degree];
                if (degenerate)
                    return;
                s.Bounds();
                shape.segments.Push(s);
            };
            auto close = [&] {
                if (open && Length(last - start) > 1e-9)
                    add(1, &start);
                open = false;
            };
            for (Verb v : o.verbs)
            {
                if (shape.segments.Size() > kMaxSegments)
                    return error = "more segments than a glyph has", false;
                V2 pts[3];
                switch (v)
                {
                case Verb::Move:
                    close();
                    start = last = map(o.points[p++]);
                    shape.contourStarts.Push(u32(shape.segments.Size()));
                    open = true;
                    break;
                case Verb::Line:
                    pts[0] = map(o.points[p++]);
                    add(1, pts);
                    break;
                case Verb::Quad:
                    pts[0] = map(o.points[p]), pts[1] = map(o.points[p + 1]);
                    p += 2;
                    add(2, pts);
                    break;
                case Verb::Cubic:
                    pts[0] = map(o.points[p]), pts[1] = map(o.points[p + 1]), pts[2] = map(o.points[p + 2]);
                    p += 3;
                    add(3, pts);
                    break;
                }
            }
            close();
            // Contours left empty by degenerate segments are dropped.
            Array<u32> starts;
            for (usize c = 0; c < shape.contourStarts.Size(); c++)
            {
                u32 end = c + 1 < shape.contourStarts.Size() ? shape.contourStarts[c + 1] : u32(shape.segments.Size());
                if (end > shape.contourStarts[c] && (starts.Empty() || starts.Back() != shape.contourStarts[c]))
                    starts.Push(shape.contourStarts[c]);
            }
            shape.contourStarts = starts;
            // The fill: each contour flattened.
            for (usize c = 0; c < shape.contourStarts.Size(); c++)
            {
                u32 begin = shape.contourStarts[c];
                u32 end = c + 1 < shape.contourStarts.Size() ? shape.contourStarts[c + 1] : u32(shape.segments.Size());
                shape.polyStarts.Push(u32(shape.polyline.Size()));
                for (u32 i = begin; i < end; i++)
                {
                    const Segment& s = shape.segments[i];
                    double size = (s.maxX - s.minX) + (s.maxY - s.minY);
                    int steps = s.degree == 1 ? 1 : std::clamp(int(size), 4, 64);
                    for (int k = 0; k < steps; k++)
                        shape.polyline.Push(s.At(double(k) / steps));
                }
            }
            return true;
        }

        // Whether each pixel center of a row is inside by the nonzero rule.
        void FillRow(const Shape& shape, double y, u32 width, Array<u8>& inside)
        {
            struct Crossing
            {
                double x;
                int dir;
            };
            Array<Crossing> crossings;
            for (usize c = 0; c < shape.polyStarts.Size(); c++)
            {
                u32 begin = shape.polyStarts[c];
                u32 end = c + 1 < shape.polyStarts.Size() ? shape.polyStarts[c + 1] : u32(shape.polyline.Size());
                for (u32 i = begin; i < end; i++)
                {
                    V2 a = shape.polyline[i], b = shape.polyline[i + 1 < end ? i + 1 : begin];
                    if ((a.y <= y) == (b.y <= y))
                        continue;
                    double x = a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x);
                    crossings.Push({x, b.y > a.y ? 1 : -1});
                }
            }
            std::sort(crossings.begin(), crossings.end(), [](const Crossing& a, const Crossing& b) { return a.x < b.x; });
            inside.Resize(width);
            int winding = 0;
            usize k = 0;
            for (u32 x = 0; x < width; x++)
            {
                double px = x + 0.5;
                while (k < crossings.Size() && crossings[k].x <= px)
                    winding += crossings[k++].dir;
                inside[x] = winding != 0;
            }
        }

        bool Prepare(const Outline& outline, const DistanceOptions& o, u32 channels, GlyphBitmap& out, Shape& shape, String& error)
        {
            out = GlyphBitmap();
            out.channels = channels;
            if (!(o.scale > 0) || !std::isfinite(o.scale) || !(o.range > 0))
                return error = "a scale or range that is not positive", false;
            if (outline.Empty())
                return true;
            if (!outline.Finite())
                return error = "an outline with points that are not finite", false;
            double x0 = std::floor(double(outline.xMin) * o.scale + o.shiftX) - o.padding;
            double x1 = std::ceil(double(outline.xMax) * o.scale + o.shiftX) + o.padding;
            double y0 = std::floor(-double(outline.yMax) * o.scale + o.shiftY) - o.padding;
            double y1 = std::ceil(-double(outline.yMin) * o.scale + o.shiftY) + o.padding;
            if (!(x1 - x0 <= kMaxGlyphPixels && y1 - y0 <= kMaxGlyphPixels) || std::fabs(x0) > 1e7 || std::fabs(y0) > 1e7)
                return error = Format("a field of {} x {} pixels", x1 - x0, y1 - y0), false;
            out.width = u32(x1 - x0);
            out.height = u32(y1 - y0);
            out.left = i32(x0);
            out.top = i32(y0);
            out.pixels.Resize(usize(out.width) * out.height * channels, u8(0));
            return BuildShape(outline, o.scale, x0 - o.shiftX, y0 - o.shiftY, shape, error);
        }

        u8 Encode(double distance, double range) { return u8(std::clamp(std::lround(128 + distance / range * 127), 0l, 255l)); }

        // The smallest true distance to any segment, or `cap` when all are farther; a segment whose box is farther cannot be
        // closer.
        double TrueDistance(const Shape& shape, V2 p, double cap)
        {
            double best = cap;
            for (const Segment& s : shape.segments)
            {
                double dx = BoxGap(s.minX, s.maxX, p.x), dy = BoxGap(s.minY, s.maxY, p.y);
                if (dx * dx + dy * dy >= best * best)
                    continue;
                best = std::min(best, Nearest(s, p).distance);
            }
            return best;
        }

        // Colors the edges of each contour so that the two edges at every corner share one channel.
        void ColorEdges(Shape& shape)
        {
            Array<Segment> colored;
            Array<u32> starts;
            for (usize c = 0; c < shape.contourStarts.Size(); c++)
            {
                u32 begin = shape.contourStarts[c];
                u32 end = c + 1 < shape.contourStarts.Size() ? shape.contourStarts[c + 1] : u32(shape.segments.Size());
                Array<Segment> edges;
                edges.Append(shape.segments.Data() + begin, end - begin);
                // Corners: where the direction turns by more than about 8 degrees, or backwards.
                auto corner = [&](u32 i) {
                    V2 a = Normalize(edges[(i + u32(edges.Size()) - 1) % edges.Size()].Tangent(1)), b = Normalize(edges[i].Tangent(0));
                    return Dot(a, b) <= 0 || std::fabs(Cross(a, b)) > 0.14112;
                };
                Array<u32> corners;
                for (u32 i = 0; i < edges.Size(); i++)
                    if (corner(i))
                        corners.Push(i);
                if (corners.Empty())
                {
                    for (Segment& e : edges)
                        e.color = kWhite;
                }
                else if (corners.Size() == 1)
                {
                    // A teardrop: three spans from the corner, so the corner sees two colors.
                    u32 rotate = corners[0];
                    Array<Segment> r;
                    for (u32 i = 0; i < edges.Size(); i++)
                        r.Push(edges[(rotate + i) % edges.Size()]);
                    while (r.Size() < 3)
                    {
                        usize longest = 0;
                        for (usize i = 1; i < r.Size(); i++)
                            if ((r[i].maxX - r[i].minX) + (r[i].maxY - r[i].minY) > (r[longest].maxX - r[longest].minX) + (r[longest].maxY - r[longest].minY))
                                longest = i;
                        Segment a, b;
                        r[longest].Split(0.5, a, b);
                        r[longest] = a;
                        r.Insert(longest + 1, b);
                    }
                    static constexpr u8 kSpans[3] = {kMagenta, kWhite, kYellow};
                    for (usize i = 0; i < r.Size(); i++)
                        r[i].color = kSpans[std::min<usize>(2, i * 3 / r.Size())];
                    edges = r;
                }
                else
                {
                    static constexpr u8 kCycle[3] = {kCyan, kMagenta, kYellow};
                    u32 spans = u32(corners.Size());
                    u32 span = 0;
                    for (u32 k = 0; k < edges.Size(); k++)
                    {
                        u32 i = (corners[0] + k) % u32(edges.Size());
                        if (k > 0 && span + 1 < spans && i == corners[span + 1])
                            span++;
                        u8 color = kCycle[span % 3];
                        // The last span must differ from the first one too.
                        if (span == spans - 1 && spans % 3 == 1)
                            color = kCycle[1] == kCycle[(span - 1) % 3] ? kCycle[2] : kCycle[1];
                        edges[i].color = color;
                    }
                }
                starts.Push(u32(colored.Size()));
                colored.Append(edges.Data(), edges.Size());
            }
            shape.segments = colored;
            shape.contourStarts = starts;
        }
    }

    bool SignedDistanceField(const Outline& outline, const DistanceOptions& o, GlyphBitmap& out, String& error)
    {
        Shape shape;
        if (!Prepare(outline, o, 1, out, shape, error))
            return false;
        Array<u8> inside;
        for (u32 y = 0; y < out.height; y++)
        {
            FillRow(shape, y + 0.5, out.width, inside);
            u8* row = out.Row(y);
            for (u32 x = 0; x < out.width; x++)
            {
                // Beyond the range every distance encodes the same.
                double d = TrueDistance(shape, {x + 0.5, y + 0.5}, o.range * 1.01 + 1);
                row[x] = Encode(inside[x] ? d : -d, o.range);
            }
        }
        return true;
    }

    bool MultiChannelDistanceField(const Outline& outline, const DistanceOptions& o, GlyphBitmap& out, String& error)
    {
        Shape shape;
        if (!Prepare(outline, o, 4, out, shape, error))
            return false;
        if (shape.segments.Empty())
            return true;
        ColorEdges(shape);
        u32 w = out.width, h = out.height;
        Array<float> field;
        field.Resize(usize(w) * h * 4, 0.0f);
        Array<u8> inside;
        for (u32 y = 0; y < h; y++)
        {
            FillRow(shape, y + 0.5, w, inside);
            for (u32 x = 0; x < w; x++)
            {
                V2 p{x + 0.5, y + 0.5};
                Closest best[3];
                const Segment* owner[3] = {};
                double trueBest = 1e300;
                for (const Segment& s : shape.segments)
                {
                    // A segment whose box is farther than every distance it could improve is skipped.
                    double dx = BoxGap(s.minX, s.maxX, p.x), dy = BoxGap(s.minY, s.maxY, p.y);
                    double near = std::sqrt(dx * dx + dy * dy);
                    bool useful = near < trueBest;
                    for (int ch = 0; ch < 3 && !useful; ch++)
                        useful = (s.color & (1 << ch)) && near <= best[ch].distance;
                    if (!useful)
                        continue;
                    Closest c = Nearest(s, p);
                    trueBest = std::min(trueBest, c.distance);
                    for (int ch = 0; ch < 3; ch++)
                    {
                        if (!(s.color & (1 << ch)))
                            continue;
                        bool closer = c.distance < best[ch].distance - 1e-9;
                        if (!closer && std::fabs(c.distance - best[ch].distance) <= 1e-9 && owner[ch])
                            closer = Ortho(s, c.t, p) > Ortho(*owner[ch], best[ch].t, p);
                        if (closer)
                            best[ch] = c, owner[ch] = &s;
                    }
                }
                float* px = &field[(usize(y) * w + x) * 4];
                for (int ch = 0; ch < 3; ch++)
                    px[ch] = owner[ch] ? float(PseudoDistance(*owner[ch], best[ch], p)) : float(-trueBest);
                // The edge directions give the sign; the fill decides it where they disagree (opposite orientation).
                bool in = inside[x] != 0;
                if ((Median(px[0], px[1], px[2]) > 0) != in)
                    for (int ch = 0; ch < 3; ch++)
                        px[ch] = -px[ch];
                px[3] = float(in ? trueBest : -trueBest);
            }
        }

        // Clashes: neighbors where two channels jump across the edge while the median does not, so interpolating between
        // them would draw a false edge. The texel farther from its own median is made a plain distance texel.
        Array<u8> clash;
        clash.Resize(usize(w) * h, u8(0));
        auto check = [&](usize a, usize b) {
            const float* pa = &field[a * 4];
            const float* pb = &field[b * 4];
            int jumps = 0;
            bool crosses = false;
            for (int ch = 0; ch < 3; ch++)
                if (std::fabs(pa[ch] - pb[ch]) >= 1.001f)
                {
                    jumps++;
                    crosses = crosses || (pa[ch] > 0) != (pb[ch] > 0);
                }
            float ma = Median(pa[0], pa[1], pa[2]), mb = Median(pb[0], pb[1], pb[2]);
            if (jumps < 2 || !crosses || std::fabs(ma - mb) >= 1.001f)
                return;
            auto spread = [](const float* q, float m) { return std::max(std::max(std::fabs(q[0] - m), std::fabs(q[1] - m)), std::fabs(q[2] - m)); };
            clash[spread(pa, ma) >= spread(pb, mb) ? a : b] = 1;
        };
        for (u32 y = 0; y < h; y++)
            for (u32 x = 0; x < w; x++)
            {
                usize i = usize(y) * w + x;
                if (x + 1 < w)
                    check(i, i + 1);
                if (y + 1 < h)
                    check(i, i + w);
            }
        for (usize i = 0; i < clash.Size(); i++)
            if (clash[i])
            {
                float* px = &field[i * 4];
                float m = Median(px[0], px[1], px[2]);
                px[0] = px[1] = px[2] = m;
            }
        for (usize i = 0; i < usize(w) * h; i++)
            for (int ch = 0; ch < 4; ch++)
                out.pixels[i * 4 + ch] = Encode(field[i * 4 + ch], o.range);
        return true;
    }
}
