#include <Helix/Font/Color/Color.h>
#include <Helix/Font/Layout/Kern.h>
#include <Helix/Font/Layout/Layout.h>
#include <Helix/Font/Outline/Outline.h>
#include <Helix/Testing/Test.h>

#include <cstdio>

using namespace helix;
using namespace helix::font;

// Fonts of the machine, read when present: every glyph outline, the layout tables, kerning and color glyphs. Nothing of
// them is part of the repository; a machine without them skips the font.
namespace
{
    bool ReadFile(const char* path, Array<u8>& out)
    {
        std::FILE* f = std::fopen(path, "rb");
        if (!f)
            return false;
        std::fseek(f, 0, SEEK_END);
        long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        out.Resize(usize(size));
        bool ok = std::fread(out.Data(), 1, out.Size(), f) == out.Size();
        std::fclose(f);
        return ok;
    }

    constexpr const char* kFonts[] = {
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/ariali.ttf",
        "C:/Windows/Fonts/msgothic.ttc",
        "C:/Windows/Fonts/DavidCLM-Medium.otf",
        "C:/Windows/Fonts/FrankRuehlCLM-Bold.otf",
        "C:/Windows/Fonts/seguiemj.ttf",
        "C:/Windows/Fonts/Nirmala.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/opentype/urw-base35/NimbusSans-Regular.otf",
    };
}

HX_TEST(SystemFonts, EveryGlyphAndTableReads)
{
    u32 files = 0;
    for (const char* path : kFonts)
    {
        Array<u8> bytes;
        if (!ReadFile(path, bytes))
            continue;
        files++;
        u32 fonts = Font::CountFonts(bytes);
        HX_CHECK(fonts >= 1u);
        for (u32 i = 0; i < fonts; i++)
        {
            Font f;
            Outlines o;
            String error;
            if (!f.Open(Span<const u8>(bytes), i, error) || !o.Open(f, error))
            {
                std::printf("    %s %u: %s\n", path, i, error.CStr());
                HX_CHECK(false);
                continue;
            }
            u32 failed = 0, drawn = 0;
            Outline outline;
            for (u32 g = 0; g < f.GlyphCount(); g++)
            {
                if (!o.Get(g, outline, error))
                {
                    if (failed++ < 3)
                        std::printf("    %s %u: %s\n", path, i, error.CStr());
                }
                else
                    drawn += !outline.Empty();
            }
            HX_CHECK(failed == 0u && drawn > 0u);
            Layout gsub, gpos;
            GlyphDefinitions gdef;
            KernTable kern;
            ColorLayers colr;
            bool tables = gsub.Read(f, Tag("GSUB"), error) && gpos.Read(f, Tag("GPOS"), error) && gdef.Read(f, error) && kern.Read(f, error) && colr.Read(f, error);
            if (!tables)
                std::printf("    %s %u: %s\n", path, i, error.CStr());
            HX_CHECK(tables);
            u32 a = f.GlyphIndex('A');
            std::printf("    %s %u: %u glyphs, %s, 'A' is %u, %u GSUB and %u GPOS lookups, %u kern pairs, %u color palettes, family '%s'\n", path, i,
                        f.GlyphCount(), f.IsCff() ? "CFF" : "glyf", a, u32(gsub.lookups.Size()), u32(gpos.lookups.Size()), u32(kern.Pairs().Size()),
                        colr.Empty() ? 0u : u32(colr.PaletteCount()), String(f.Name(kNameFamily)).CStr());
            if (!colr.Empty())
            {
                u32 smile = f.GlyphIndex(0x1F600);
                GlyphBitmap b;
                RasterOptions r;
                r.scale = 64.0f / f.Head().unitsPerEm;
                HX_CHECK(smile != 0 && !colr.Layers(smile).Empty() && RenderColorGlyph(colr, o, smile, 0, 0xFF000000u, r, b, error) && b.width > 32);
            }
        }
    }
    std::printf("    %u font files on this machine\n", files);
}
