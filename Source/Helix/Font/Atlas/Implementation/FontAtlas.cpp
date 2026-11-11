#include <Helix/Font/Atlas/FontAtlas.h>

#include <Helix/Core/Basics/Assert.h>

#include <cstring>

// Rectangle packing through the public domain stb_rect_pack (ThirdParty/stb_rect_pack, see THIRD-PARTY.txt).
#define STBRP_STATIC
#define STBRP_ASSERT(expression) HX_ASSERT(expression)
#define STB_RECT_PACK_IMPLEMENTATION
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(push, 0)
#pragma warning(disable : 4244 4456 4505 4701)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wconversion"
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wunknown-warning-option"
#pragma GCC diagnostic ignored "-Wcomma"
#endif
#endif
#include <stb_rect_pack/stb_rect_pack.h>
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif

namespace helix::font
{
    // Kept at one address: the packer points into itself.
    struct FontAtlas::Page
    {
        Array<u8> pixels;
        Array<stbrp_node> nodes;
        stbrp_context context;
        u32 dirtyX0 = ~0u, dirtyY0 = ~0u, dirtyX1 = 0, dirtyY1 = 0;
    };

    FontAtlas::FontAtlas(u32 pageWidth, u32 pageHeight, u32 channels, u32 padding)
        : width_(pageWidth), height_(pageHeight), channels_(channels), padding_(padding)
    {
    }

    FontAtlas::~FontAtlas()
    {
        for (Page* p : pages_)
            delete p;
    }

    const u8* FontAtlas::PagePixels(u32 page) const { return page < pages_.Size() ? pages_[page]->pixels.Data() : nullptr; }

    bool FontAtlas::Pack(Page& page, u32 w, u32 h, u32& x, u32& y)
    {
        stbrp_rect r{};
        r.w = int(w);
        r.h = int(h);
        if (!stbrp_pack_rects(&page.context, &r, 1) || !r.was_packed)
            return false;
        x = u32(r.x), y = u32(r.y);
        return true;
    }

    bool FontAtlas::Add(u64 key, const GlyphBitmap& bitmap, AtlasPlace& out)
    {
        if (const AtlasPlace* known = places_.Find(key))
        {
            out = *known;
            return true;
        }
        if (bitmap.channels != channels_ && bitmap.width > 0)
            return false;
        u32 w = bitmap.width + 2 * padding_, h = bitmap.height + 2 * padding_;
        if (w > width_ || h > height_)
            return false;
        u32 x = 0, y = 0;
        Page* target = nullptr;
        for (Page* p : pages_)
            if (Pack(*p, w, h, x, y))
            {
                target = p;
                break;
            }
        if (!target)
        {
            // Pages that could not take this one may still take smaller glyphs, so they stay open.
            Page* p = new Page();
            p->pixels.Resize(usize(width_) * height_ * channels_, u8(0));
            p->nodes.Resize(width_);
            stbrp_init_target(&p->context, int(width_), int(height_), p->nodes.Data(), int(width_));
            pages_.Push(p);
            if (!Pack(*p, w, h, x, y))
                return false;
            target = p;
        }
        u32 page = 0;
        while (pages_[page] != target)
            page++;
        AtlasPlace place;
        place.page = page;
        place.x = x + padding_;
        place.y = y + padding_;
        place.width = bitmap.width;
        place.height = bitmap.height;
        place.left = bitmap.left;
        place.top = bitmap.top;
        usize row = usize(bitmap.width) * channels_;
        for (u32 r = 0; r < bitmap.height; r++)
            std::memcpy(target->pixels.Data() + (usize(place.y + r) * width_ + place.x) * channels_, bitmap.Row(r), row);
        if (bitmap.width > 0)
        {
            target->dirtyX0 = place.x < target->dirtyX0 ? place.x : target->dirtyX0;
            target->dirtyY0 = place.y < target->dirtyY0 ? place.y : target->dirtyY0;
            target->dirtyX1 = place.x + place.width > target->dirtyX1 ? place.x + place.width : target->dirtyX1;
            target->dirtyY1 = place.y + place.height > target->dirtyY1 ? place.y + place.height : target->dirtyY1;
        }
        places_.Set(key, place);
        out = place;
        return true;
    }

    bool FontAtlas::Dirty(u32 page, u32& x0, u32& y0, u32& x1, u32& y1) const
    {
        if (page >= pages_.Size() || pages_[page]->dirtyX1 == 0)
            return false;
        const Page& p = *pages_[page];
        x0 = p.dirtyX0, y0 = p.dirtyY0, x1 = p.dirtyX1, y1 = p.dirtyY1;
        return true;
    }

    void FontAtlas::ClearDirty()
    {
        for (Page* p : pages_)
        {
            p->dirtyX0 = p->dirtyY0 = ~0u;
            p->dirtyX1 = p->dirtyY1 = 0;
        }
    }

    void FontAtlas::Clear()
    {
        places_.Clear();
        for (Page* p : pages_)
        {
            std::memset(p->pixels.Data(), 0, p->pixels.Size());
            stbrp_init_target(&p->context, int(width_), int(height_), p->nodes.Data(), int(width_));
            p->dirtyX0 = p->dirtyY0 = ~0u;
            p->dirtyX1 = p->dirtyY1 = 0;
        }
    }
}
