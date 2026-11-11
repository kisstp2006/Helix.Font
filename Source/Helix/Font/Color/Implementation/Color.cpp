#include <Helix/Font/Color/Color.h>

#include <Helix/Core/Strings/Format.h>
#include <Helix/Image/Codecs/Png.h>

#include <algorithm>
#include <cmath>

namespace helix::font
{
    bool ColorBitmaps::Read(const Font& font, String& error)
    {
        *this = ColorBitmaps();
        cblc_ = font.Table(Tag("CBLC"));
        cbdt_ = font.Table(Tag("CBDT"));
        if (cblc_.Empty())
            return true;
        if (cbdt_.Empty())
            return error = "CBLC without CBDT", false;
        u32 n = cblc_.U32(4);
        if (!cblc_.Has(8, usize(n) * 48))
            return error = "CBLC: strikes outside the table", false;
        for (u32 i = 0; i < n; i++)
        {
            usize at = 8 + usize(i) * 48;
            BitmapStrike s;
            s.indexArray = cblc_.U32(at);
            s.indexCount = cblc_.U32(at + 8);
            s.firstGlyph = cblc_.U16(at + 40);
            s.lastGlyph = cblc_.U16(at + 42);
            s.ppemX = cblc_.U8(at + 44);
            s.ppemY = cblc_.U8(at + 45);
            s.bitDepth = cblc_.U8(at + 46);
            if (!cblc_.Has(s.indexArray, usize(s.indexCount) * 8))
                return error = Format("CBLC: the index of strike {} is outside the table", i), false;
            strikes_.Push(s);
        }
        return true;
    }

    bool ColorBitmaps::Locate(u32 glyph, const BitmapStrike& s, usize& data, usize& size, u16& imageFormat, usize& bigMetrics) const
    {
        bigMetrics = 0;
        for (u32 i = 0; i < s.indexCount; i++)
        {
            usize entry = s.indexArray + usize(i) * 8;
            u16 first = cblc_.U16(entry), last = cblc_.U16(entry + 2);
            if (glyph < first || glyph > last)
                continue;
            usize sub = s.indexArray + cblc_.U32(entry + 4);
            u16 indexFormat = cblc_.U16(sub);
            imageFormat = cblc_.U16(sub + 2);
            usize base = cblc_.U32(sub + 4);
            u32 k = glyph - first;
            switch (indexFormat)
            {
            case 1:
            {
                u32 a = cblc_.U32(sub + 8 + 4 * usize(k)), b = cblc_.U32(sub + 12 + 4 * usize(k));
                if (!cblc_.Has(sub + 8, (usize(k) + 2) * 4) || b <= a)
                    return false;
                data = base + a, size = b - a;
                return true;
            }
            case 3:
            {
                u16 a = cblc_.U16(sub + 8 + 2 * usize(k)), b = cblc_.U16(sub + 10 + 2 * usize(k));
                if (!cblc_.Has(sub + 8, (usize(k) + 2) * 2) || b <= a)
                    return false;
                data = base + a, size = usize(b) - a;
                return true;
            }
            case 2:
            {
                u32 imageSize = cblc_.U32(sub + 8);
                bigMetrics = sub + 12;
                data = base + usize(imageSize) * k, size = imageSize;
                return imageSize > 0;
            }
            case 4:
            {
                u32 count = cblc_.U32(sub + 8);
                if (!cblc_.Has(sub + 12, (usize(count) + 1) * 4))
                    return false;
                for (u32 g = 0; g < count; g++)
                    if (cblc_.U16(sub + 12 + 4 * usize(g)) == glyph)
                    {
                        u16 a = cblc_.U16(sub + 14 + 4 * usize(g)), b = cblc_.U16(sub + 18 + 4 * usize(g));
                        if (b <= a)
                            return false;
                        data = base + a, size = usize(b) - a;
                        return true;
                    }
                return false;
            }
            case 5:
            {
                u32 imageSize = cblc_.U32(sub + 8);
                bigMetrics = sub + 12;
                u32 count = cblc_.U32(sub + 20);
                if (!cblc_.Has(sub + 24, usize(count) * 2))
                    return false;
                for (u32 g = 0; g < count; g++)
                    if (cblc_.U16(sub + 24 + 2 * usize(g)) == glyph)
                    {
                        data = base + usize(imageSize) * g, size = imageSize;
                        return imageSize > 0;
                    }
                return false;
            }
            default: return false;
            }
        }
        return false;
    }

    bool ColorBitmaps::Get(u32 glyph, u32 ppem, GlyphBitmap& out, u32& strike, String& error) const
    {
        out = GlyphBitmap();
        // The smallest strike at least as large, else the largest, among those holding the glyph.
        i32 best = -1;
        for (u32 i = 0; i < strikes_.Size(); i++)
        {
            const BitmapStrike& s = strikes_[i];
            if (glyph < s.firstGlyph || glyph > s.lastGlyph)
                continue;
            if (best < 0)
            {
                best = i32(i);
                continue;
            }
            const BitmapStrike& b = strikes_[usize(best)];
            bool sBig = s.ppemY >= ppem, bBig = b.ppemY >= ppem;
            if ((sBig && (!bBig || s.ppemY < b.ppemY)) || (!sBig && !bBig && s.ppemY > b.ppemY))
                best = i32(i);
        }
        usize data, size, big;
        u16 format = 0;
        if (best < 0 || !Locate(glyph, strikes_[usize(best)], data, size, format, big))
            return error = Format("glyph {} has no color bitmap", glyph), false;
        strike = u32(best);
        Bytes d = cbdt_.Sub(data, size);
        if (d.Empty())
            return error = Format("glyph {}: bitmap data outside CBDT", glyph), false;
        i32 bearingX, bearingY;
        usize png;
        if (format == 17)
            bearingX = d.I8(2), bearingY = d.I8(3), png = 5;
        else if (format == 18)
            bearingX = d.I8(2), bearingY = d.I8(3), png = 8;
        else if (format == 19 && big != 0)
            bearingX = cblc_.I8(big + 2), bearingY = cblc_.I8(big + 3), png = 0;
        else
            return error = Format("glyph {}: bitmap image format {}", glyph, format), false;
        u32 length = d.U32(png);
        Bytes bytes = d.Sub(png + 4, length);
        if (bytes.Empty())
            return error = Format("glyph {}: PNG data outside the bitmap", glyph), false;
        image::Image img;
        if (!image::DecodePng(Span<const u8>(bytes.data, bytes.size), img))
            return error = Format("glyph {}: a damaged PNG", glyph), false;
        out.width = img.width;
        out.height = img.height;
        out.channels = 4;
        out.left = bearingX;
        out.top = -bearingY;
        out.pixels = Move(img.pixels);
        return true;
    }

    bool ColorBitmaps::Advance(u32 glyph, u32 strike, u32& advance) const
    {
        if (strike >= strikes_.Size())
            return false;
        usize data, size, big;
        u16 format = 0;
        if (!Locate(glyph, strikes_[strike], data, size, format, big))
            return false;
        Bytes d = cbdt_.Sub(data, size);
        if (format == 17 || format == 18)
            advance = d.U8(4);
        else if (big)
            advance = cblc_.U8(big + 4);
        else
            return false;
        return true;
    }

    bool ColorLayers::Read(const Font& font, String& error)
    {
        *this = ColorLayers();
        Bytes colr = font.Table(Tag("COLR"));
        if (colr.Empty())
            return true;
        u16 bases = colr.U16(2), layers = colr.U16(12);
        u32 baseAt = colr.U32(4), layerAt = colr.U32(8);
        if (!colr.Has(baseAt, usize(bases) * 6) || !colr.Has(layerAt, usize(layers) * 4))
            return error = "COLR: records outside the table", false;
        for (u32 i = 0; i < bases; i++)
        {
            usize at = baseAt + 6 * usize(i);
            Base b{colr.U16(at), colr.U16(at + 2), colr.U16(at + 4)};
            if (usize(b.first) + b.count > layers)
                return error = Format("COLR: glyph {} has layers outside the table", b.glyph), false;
            bases_.Push(b);
        }
        std::sort(bases_.begin(), bases_.end(), [](const Base& a, const Base& b) { return a.glyph < b.glyph; });
        for (u32 i = 0; i < layers; i++)
            layers_.Push({colr.U16(layerAt + 4 * usize(i)), colr.U16(layerAt + 4 * usize(i) + 2)});

        Bytes cpal = font.Table(Tag("CPAL"));
        if (cpal.Empty())
            return true;
        entries_ = cpal.U16(2);
        palettes_ = cpal.U16(4);
        u16 records = cpal.U16(6);
        u32 recordsAt = cpal.U32(8);
        if (!cpal.Has(12, usize(palettes_) * 2) || !cpal.Has(recordsAt, usize(records) * 4))
            return error = "CPAL: palettes outside the table", false;
        for (u32 p = 0; p < palettes_; p++)
        {
            u16 first = cpal.U16(12 + 2 * usize(p));
            if (usize(first) + entries_ > records)
                return error = Format("CPAL: palette {} outside the color records", p), false;
            paletteStarts_.Push(first);
        }
        for (u32 c = 0; c < records; c++)
        {
            usize at = recordsAt + 4 * usize(c);
            u32 b = cpal.U8(at), g = cpal.U8(at + 1), r = cpal.U8(at + 2), a = cpal.U8(at + 3);
            colors_.Push(r | g << 8 | b << 16 | a << 24);
        }
        return true;
    }

    Span<const ColorLayer> ColorLayers::Layers(u32 glyph) const
    {
        const Base* it = std::lower_bound(bases_.begin(), bases_.end(), glyph, [](const Base& b, u32 g) { return b.glyph < g; });
        if (it == bases_.end() || it->glyph != glyph)
            return {};
        return Span<const ColorLayer>(layers_.Data() + it->first, it->count);
    }

    u32 ColorLayers::Color(u32 palette, u32 entry) const
    {
        if (palette >= paletteStarts_.Size() || entry >= entries_)
            return 0;
        return colors_[usize(paletteStarts_[palette]) + entry];
    }

    bool RenderColorGlyph(const ColorLayers& colors, const Outlines& outlines, u32 glyph, u32 palette, u32 foreground, const RasterOptions& options,
                          GlyphBitmap& out, String& error)
    {
        out = GlyphBitmap();
        out.channels = 4;
        Span<const ColorLayer> layers = colors.Layers(glyph);
        if (layers.Empty())
            return error = Format("glyph {} has no color layers", glyph), false;
        Array<GlyphBitmap> masks;
        Array<u32> tints;
        i32 x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        for (const ColorLayer& layer : layers)
        {
            Outline o;
            if (!outlines.Get(layer.glyph, o, error))
                return false;
            GlyphBitmap m;
            if (!Rasterize(o, options, m, error))
                return false;
            if (m.width == 0)
                continue;
            if (masks.Empty())
                x0 = m.left, y0 = m.top, x1 = m.left + i32(m.width), y1 = m.top + i32(m.height);
            else
            {
                x0 = std::min(x0, m.left), y0 = std::min(y0, m.top);
                x1 = std::max(x1, m.left + i32(m.width)), y1 = std::max(y1, m.top + i32(m.height));
            }
            tints.Push(layer.paletteIndex == 0xFFFF ? foreground : colors.Color(palette, layer.paletteIndex));
            masks.Push(Move(m));
        }
        if (masks.Empty())
            return true;
        if (x1 - x0 > i32(kMaxGlyphPixels) || y1 - y0 > i32(kMaxGlyphPixels))
            return error = "a color glyph larger than a glyph bitmap can be", false;
        out.width = u32(x1 - x0), out.height = u32(y1 - y0), out.left = x0, out.top = y0;
        // Premultiplied while drawing, straight at the end.
        Array<float> rgba;
        rgba.Resize(usize(out.width) * out.height * 4, 0.0f);
        for (usize l = 0; l < masks.Size(); l++)
        {
            const GlyphBitmap& m = masks[l];
            u32 c = tints[l];
            float cr = float(c & 255) / 255, cg = float(c >> 8 & 255) / 255, cb = float(c >> 16 & 255) / 255, ca = float(c >> 24) / 255;
            for (u32 y = 0; y < m.height; y++)
                for (u32 x = 0; x < m.width; x++)
                {
                    float a = ca * float(m.Row(y)[x]) / 255;
                    float* d = &rgba[(usize(u32(m.top - y0) + y) * out.width + u32(m.left - x0) + x) * 4];
                    d[0] = cr * a + d[0] * (1 - a);
                    d[1] = cg * a + d[1] * (1 - a);
                    d[2] = cb * a + d[2] * (1 - a);
                    d[3] = a + d[3] * (1 - a);
                }
        }
        out.pixels.Resize(rgba.Size());
        for (usize i = 0; i < rgba.Size(); i += 4)
        {
            float a = rgba[i + 3];
            for (int ch = 0; ch < 3; ch++)
                out.pixels[i + ch] = u8(std::lround(std::clamp(a > 0 ? rgba[i + ch] / a : 0.0f, 0.0f, 1.0f) * 255));
            out.pixels[i + 3] = u8(std::lround(std::clamp(a, 0.0f, 1.0f) * 255));
        }
        return true;
    }
}
