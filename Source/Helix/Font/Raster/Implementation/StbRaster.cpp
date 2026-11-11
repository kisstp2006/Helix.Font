#include <Helix/Font/Raster/Raster.h>

#include <Helix/Core/Basics/Assert.h>
#include <Helix/Core/Memory/Allocator.h>
#include <Helix/Core/Strings/Format.h>

#include <cmath>
#include <cstring>

// Rasterization through the public domain stb_truetype (ThirdParty/stb_truetype, see THIRD-PARTY.txt). Only its
// rasterizer and prefilter are used, on our own outlines; its font parser is compiled but not called.
namespace helix::font::stb
{
    // stb frees without a size, so every block keeps its size in front of it.
    constexpr usize kHeader = 16;

    void* Allocate(usize size)
    {
        u8* block = static_cast<u8*>(HeapAllocator().Allocate(size + kHeader, kHeader));
        if (!block)
            return nullptr;
        std::memcpy(block, &size, sizeof(size));
        return block + kHeader;
    }

    void Free(void* memory)
    {
        if (!memory)
            return;
        u8* block = static_cast<u8*>(memory) - kHeader;
        usize size;
        std::memcpy(&size, block, sizeof(size));
        HeapAllocator().Free(block, size + kHeader, kHeader);
    }

    // Float coordinates, so composite transforms and CFF fractions reach the rasterizer unrounded.
    struct Vertex
    {
        float x, y, cx, cy, cx1, cy1;
        unsigned char type, padding;
    };
}

#define stbtt_vertex ::helix::font::stb::Vertex
#define stbtt_vertex_type float
#define STBTT_malloc(size, user) ((void)(user), ::helix::font::stb::Allocate(size))
#define STBTT_free(memory, user) ((void)(user), ::helix::font::stb::Free(memory))
#define STBTT_assert(expression) HX_ASSERT(expression)
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(push, 0)
#pragma warning(disable : 4244 4245 4456 4457 4701 4702 4996 4505)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wunknown-warning-option"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wcomma"
#endif
#endif
#include <stb_truetype/stb_truetype.h>
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif

namespace helix::font
{
    namespace
    {
        // Every glyph of a real font has far fewer segments.
        constexpr usize kMaxSegments = 1u << 18;

        void ToVertices(const Outline& o, Array<stb::Vertex>& out)
        {
            out.Clear();
            out.Reserve(o.verbs.Size());
            usize p = 0;
            for (Verb v : o.verbs)
            {
                stb::Vertex vx{};
                switch (v)
                {
                case Verb::Move:
                    vx.type = STBTT_vmove;
                    vx.x = o.points[p].x, vx.y = o.points[p].y;
                    p += 1;
                    break;
                case Verb::Line:
                    vx.type = STBTT_vline;
                    vx.x = o.points[p].x, vx.y = o.points[p].y;
                    p += 1;
                    break;
                case Verb::Quad:
                    vx.type = STBTT_vcurve;
                    vx.cx = o.points[p].x, vx.cy = o.points[p].y;
                    vx.x = o.points[p + 1].x, vx.y = o.points[p + 1].y;
                    p += 2;
                    break;
                case Verb::Cubic:
                    vx.type = STBTT_vcubic;
                    vx.cx = o.points[p].x, vx.cy = o.points[p].y;
                    vx.cx1 = o.points[p + 1].x, vx.cy1 = o.points[p + 1].y;
                    vx.x = o.points[p + 2].x, vx.y = o.points[p + 2].y;
                    p += 3;
                    break;
                }
                out.Push(vx);
            }
        }
    }

    bool Rasterize(const Outline& outline, const RasterOptions& o, GlyphBitmap& out, String& error)
    {
        out = GlyphBitmap();
        if (o.oversampleX < 1 || o.oversampleX > 8 || o.oversampleY < 1 || o.oversampleY > 8)
            return error = Format("oversampling {} x {} outside 1 to 8", o.oversampleX, o.oversampleY), false;
        if (!(o.scale > 0) || !std::isfinite(o.scale))
            return error = "a scale that is not positive", false;
        if (outline.Empty())
            return true;
        if (outline.verbs.Size() > kMaxSegments)
            return error = Format("{} segments", outline.verbs.Size()), false;
        if (!outline.Finite())
            return error = "an outline with points that are not finite", false;
        float sx = o.scale * float(o.oversampleX), sy = o.scale * float(o.oversampleY);
        float shiftX = o.shiftX * float(o.oversampleX), shiftY = o.shiftY * float(o.oversampleY);
        // The pixel box, y down.
        double x0 = std::floor(double(outline.xMin) * sx + shiftX), x1 = std::ceil(double(outline.xMax) * sx + shiftX);
        double y0 = std::floor(-double(outline.yMax) * sy + shiftY), y1 = std::ceil(-double(outline.yMin) * sy + shiftY);
        double w = x1 - x0 + (o.oversampleX - 1), h = y1 - y0 + (o.oversampleY - 1);
        if (!(w >= 0 && h >= 0 && w <= kMaxGlyphPixels && h <= kMaxGlyphPixels) || std::fabs(x0) > 1e7 || std::fabs(y0) > 1e7)
            return error = Format("a glyph of {} x {} pixels", w, h), false;
        if (w == 0 || h == 0)
            return true;
        out.width = u32(w);
        out.height = u32(h);
        out.left = i32(x0);
        out.top = i32(y0);
        out.pixels.Resize(usize(out.width) * out.height, u8(0));

        Array<stb::Vertex> vertices;
        ToVertices(outline, vertices);
        stbtt__bitmap bitmap;
        bitmap.w = int(out.width - (o.oversampleX - 1));
        bitmap.h = int(out.height - (o.oversampleY - 1));
        bitmap.stride = int(out.width);
        bitmap.pixels = out.pixels.Data();
        stbtt_Rasterize(&bitmap, 0.35f, vertices.Data(), int(vertices.Size()), sx, sy, shiftX, shiftY, out.left, out.top, 1, nullptr);
        if (o.oversampleX > 1)
            stbtt__h_prefilter(out.pixels.Data(), int(out.width), int(out.height), int(out.width), o.oversampleX);
        if (o.oversampleY > 1)
            stbtt__v_prefilter(out.pixels.Data(), int(out.width), int(out.height), int(out.width), o.oversampleY);
        return true;
    }
}
