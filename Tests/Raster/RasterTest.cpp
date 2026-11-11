#include <Support/SampleFonts.h>

#include <Helix/Font/Raster/Raster.h>
#include <Helix/Testing/Test.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace helix;
using namespace helix::font;
using namespace helix::font::testing;

namespace
{
    Outline Box(float x0, float y0, float x1, float y1)
    {
        Outline o;
        o.MoveTo({x0, y0});
        o.LineTo({x0, y1});
        o.LineTo({x1, y1});
        o.LineTo({x1, y0});
        o.ComputeBounds();
        return o;
    }

    // Signed distance to the square, positive inside.
    double SquareDistance(double x, double y, double x0, double y0, double x1, double y1)
    {
        double dx = std::max({x0 - x, 0.0, x - x1}), dy = std::max({y0 - y, 0.0, y - y1});
        if (dx > 0 || dy > 0)
            return -std::sqrt(dx * dx + dy * dy);
        return std::min({x - x0, x1 - x, y - y0, y1 - y});
    }

    double Decode(u8 v, double range) { return (double(v) - 128) / 127 * range; }

    // The field value at a pixel center, as font units (scale 1).
    Point Center(const GlyphBitmap& b, u32 x, u32 y) { return {float(double(b.left) + x + 0.5), float(-(double(b.top) + y + 0.5))}; }

    // The distance to a curve by dense sampling.
    double SampledDistance(const Outline& o, Point p)
    {
        double best = 1e300;
        usize at = 0;
        Point last;
        for (Verb v : o.verbs)
        {
            auto sample = [&](auto curve) {
                for (int i = 0; i <= 4000; i++)
                {
                    Point q = curve(i / 4000.0);
                    best = std::min(best, std::hypot(double(q.x - p.x), double(q.y - p.y)));
                }
            };
            if (v == Verb::Move)
                last = o.points[at++];
            else if (v == Verb::Line)
            {
                Point a = last, b = o.points[at++];
                sample([&](double t) { return Point{float(a.x + (b.x - a.x) * t), float(a.y + (b.y - a.y) * t)}; });
                last = b;
            }
            else if (v == Verb::Quad)
            {
                Point a = last, c = o.points[at], b = o.points[at + 1];
                at += 2;
                sample([&](double t) {
                    double s = 1 - t;
                    return Point{float(s * s * a.x + 2 * s * t * c.x + t * t * b.x), float(s * s * a.y + 2 * s * t * c.y + t * t * b.y)};
                });
                last = b;
            }
            else
            {
                Point a = last, c = o.points[at], d = o.points[at + 1], b = o.points[at + 2];
                at += 3;
                sample([&](double t) {
                    double s = 1 - t;
                    return Point{float(s * s * s * a.x + 3 * s * s * t * c.x + 3 * s * t * t * d.x + t * t * t * b.x),
                                 float(s * s * s * a.y + 3 * s * s * t * c.y + 3 * s * t * t * d.y + t * t * t * b.y)};
                });
                last = b;
            }
        }
        return best;
    }
}

HX_TEST(Raster, CoverageOfSquaresAndHoles)
{
    GlyphBitmap b;
    String error;
    RasterOptions o;
    HX_REQUIRE(Rasterize(Box(0, 0, 10, 10), o, b, error));
    HX_CHECK(b.width == 10u && b.height == 10u && b.left == 0 && b.top == -10 && b.channels == 1u);
    bool full = true;
    for (u8 v : b.pixels)
        full = full && v == 255;
    HX_CHECK(full);

    // Half a pixel to the right: the first and last columns are half covered.
    o.shiftX = 0.5f;
    HX_REQUIRE(Rasterize(Box(0, 0, 10, 10), o, b, error));
    HX_CHECK(b.width == 11u && std::abs(int(b.Row(5)[0]) - 128) <= 1 && std::abs(int(b.Row(5)[10]) - 128) <= 1 && b.Row(5)[5] == 255);

    // Oversampled: two times wider plus the filter's extra column.
    o = RasterOptions();
    o.oversampleX = 2;
    HX_REQUIRE(Rasterize(Box(0, 0, 10, 10), o, b, error));
    HX_CHECK(b.width == 21u && b.height == 10u);

    // A triangle covers its area.
    Outline t;
    t.MoveTo({0, 0});
    t.LineTo({0, 20});
    t.LineTo({20, 0});
    t.ComputeBounds();
    HX_REQUIRE(Rasterize(t, RasterOptions(), b, error));
    double sum = 0;
    for (u8 v : b.pixels)
        sum += v / 255.0;
    HX_CHECK(std::fabs(sum - 200) < 1.0);

    // The hole of 'A' stays empty; scale 1/32: outer 1.56..17.19 x 0..21.88, hole 4.69..14.06 x 3.13..18.75.
    Array<u8> bytes = BuildFont(TrueTypeSpec());
    Font f;
    Outlines outlines;
    Outline a;
    HX_REQUIRE(f.Open(Span<const u8>(bytes), 0, error) && outlines.Open(f, error) && outlines.Get(2, a, error));
    o = RasterOptions();
    o.scale = 0.03125f;
    HX_REQUIRE(Rasterize(a, o, b, error));
    HX_CHECK(b.width == 17u && b.height == 22u && b.left == 1 && b.top == -22);
    HX_CHECK(b.Row(11)[5] == 0 && b.Row(21)[1] == 255 && b.Row(1)[1] == 255 && std::abs(int(b.Row(0)[5]) - 223) <= 1);

    HX_CHECK(!Rasterize(Box(0, 0, 10, 10), RasterOptions{1, 0, 0, 9, 1}, b, error) && error.View().StartsWith("oversampling 9"));
    HX_CHECK(!Rasterize(Box(0, 0, 10000, 10), RasterOptions(), b, error) && error.View().StartsWith("a glyph of"));
    HX_CHECK(Rasterize(Outline(), RasterOptions(), b, error) && b.width == 0u);
}

HX_TEST(Raster, SignedDistanceOfASquare)
{
    GlyphBitmap b;
    String error;
    DistanceOptions o;
    HX_REQUIRE(SignedDistanceField(Box(0, 0, 20, 20), o, b, error));
    HX_CHECK(b.width == 28u && b.height == 28u && b.left == -4 && b.top == -24);
    double worst = 0;
    for (u32 y = 0; y < b.height; y++)
        for (u32 x = 0; x < b.width; x++)
        {
            Point p = Center(b, x, y);
            // Inside the range the field is the exact distance, rounded to 8 bits.
            double exact = SquareDistance(p.x, p.y, 0, 0, 20, 20);
            if (std::fabs(exact) < 3.9)
                worst = std::max(worst, std::fabs(Decode(b.Row(y)[x], 4) - exact));
        }
    HX_CHECK(worst < 4.0 / 127 * 0.51 + 1e-6);
    if (worst >= 4.0 / 127 * 0.51)
        std::printf("    worst %g\n", worst);
}

HX_TEST(Raster, DistanceToCurvesIsExact)
{
    // A parabola closed by a line, and a cubic closed by a line.
    Outline q;
    q.MoveTo({0, 0});
    q.QuadTo({10, 30}, {20, 0});
    q.LineTo({0, 0});
    q.ComputeBounds();
    Outline c;
    c.MoveTo({0, 0});
    c.CubicTo({0, 25}, {25, 25}, {25, 0});
    c.LineTo({0, 0});
    c.ComputeBounds();
    for (const Outline* o : {&q, &c})
    {
        GlyphBitmap b;
        String error;
        DistanceOptions d;
        d.range = 6;
        d.padding = 3;
        HX_REQUIRE(SignedDistanceField(*o, d, b, error));
        double worst = 0;
        for (u32 y = 0; y < b.height; y += 2)
            for (u32 x = 0; x < b.width; x += 2)
            {
                double exact = SampledDistance(*o, Center(b, x, y));
                if (exact < 5.9)
                    worst = std::max(worst, std::fabs(std::fabs(Decode(b.Row(y)[x], 6)) - exact));
            }
        // Quantization (6 / 127 / 2) and the sampling of the reference.
        HX_CHECK(worst < 0.03);
        if (worst >= 0.03)
            std::printf("    worst %g\n", worst);
    }
}

HX_TEST(Raster, MultiChannelFieldKeepsCornersSharp)
{
    // A small square: magnified, a plain distance field rounds its corners, the median of the three channels does not.
    Outline sq = Box(0, 0, 8, 8);
    GlyphBitmap msdf, sdf;
    String error;
    DistanceOptions o;
    o.range = 2;
    o.padding = 2;
    HX_REQUIRE(MultiChannelDistanceField(sq, o, msdf, error));
    HX_REQUIRE(SignedDistanceField(sq, o, sdf, error));
    HX_CHECK(msdf.channels == 4u && msdf.width == sdf.width && msdf.height == sdf.height);
    // The sign of the median is the fill at every texel, and alpha is the true field.
    bool signs = true, alpha = true;
    for (u32 y = 0; y < msdf.height; y++)
        for (u32 x = 0; x < msdf.width; x++)
        {
            const u8* p = msdf.Row(y) + 4 * x;
            Point c = Center(msdf, x, y);
            bool inside = c.x > 0 && c.x < 8 && c.y > 0 && c.y < 8;
            signs = signs && ((Median(p[0], p[1], p[2]) > 128) == inside);
            alpha = alpha && std::abs(int(p[3]) - int(sdf.Row(y)[x])) <= 1;
        }
    HX_CHECK(signs && alpha);

    // Sampled bilinearly 16 times finer, thresholded, compared with the square.
    auto sample = [](const GlyphBitmap& b, int channel, double fx, double fy) {
        double x = fx - 0.5, y = fy - 0.5;
        int x0 = int(std::floor(x)), y0 = int(std::floor(y));
        double tx = x - x0, ty = y - y0;
        auto at = [&](int px, int py) {
            px = std::clamp(px, 0, int(b.width) - 1), py = std::clamp(py, 0, int(b.height) - 1);
            return double(b.Row(u32(py))[u32(px) * b.channels + u32(channel)]);
        };
        return (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) + (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
    };
    int msdfWrong = 0, sdfWrong = 0;
    for (int sy = 0; sy < int(msdf.height) * 16; sy++)
        for (int sx = 0; sx < int(msdf.width) * 16; sx++)
        {
            double fx = (sx + 0.5) / 16, fy = (sy + 0.5) / 16;
            double px = msdf.left + fx, py = -(msdf.top + fy);
            bool inside = px > 0 && px < 8 && py > 0 && py < 8;
            if (std::fabs(px) < 0.07 || std::fabs(px - 8) < 0.07 || std::fabs(py) < 0.07 || std::fabs(py - 8) < 0.07)
                continue;
            double m = Median(float(sample(msdf, 0, fx, fy)), float(sample(msdf, 1, fx, fy)), float(sample(msdf, 2, fx, fy)));
            msdfWrong += (m > 128) != inside;
            sdfWrong += (sample(sdf, 0, fx, fy) > 128) != inside;
        }
    HX_CHECK(sdfWrong > 20 && msdfWrong * 10 < sdfWrong);
    if (!(sdfWrong > 20 && msdfWrong * 10 < sdfWrong))
        std::printf("    wrong samples: MSDF %d, SDF %d\n", msdfWrong, sdfWrong);
}
