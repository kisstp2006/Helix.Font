# Helix.Font

Font reading and glyph images of the Helix engine: TrueType, OpenType (CFF) and collection files, character maps,
outlines, kerning, the OpenType layout tables, color glyphs, coverage bitmaps, signed and multi channel distance fields,
and dynamic glyph atlases. The font reader, layout reader, distance fields and color glyphs are own code; rasterization
uses the public domain stb_truetype rasterizer and atlases the public domain stb_rect_pack (see `THIRD-PARTY.txt`).

## Contents

| Path | Content |
|---|---|
| `Source/Helix/Font/Font/Font.h` | `Font`: the table directory, head, hhea, maxp, hmtx, OS/2, name, post, cmap 0 / 4 / 6 / 12 / 13 / 14 |
| `Source/Helix/Font/Font/Bytes.h` | `Bytes`, the bounds checked big endian reader, and `Tag` |
| `Source/Helix/Font/Outline/Outline.h` | `Outline` (lines, quadratics, cubics in font units) and `Outlines` (glyf / loca, CFF) |
| `Source/Helix/Font/Layout/Kern.h` | `KernTable`: format 0 pairs of the Microsoft and Apple kern tables |
| `Source/Helix/Font/Layout/Layout.h` | `Layout` (GSUB, GPOS) and `GlyphDefinitions` (GDEF) in flat arrays |
| `Source/Helix/Font/Raster/Raster.h` | `Rasterize`, `SignedDistanceField`, `MultiChannelDistanceField`, `GlyphBitmap` |
| `Source/Helix/Font/Color/Color.h` | `ColorBitmaps` (CBLC / CBDT), `ColorLayers` (COLR v0, CPAL), `RenderColorGlyph` |
| `Source/Helix/Font/Atlas/FontAtlas.h` | `FontAtlas`: glyph bitmaps packed into pages as they are needed |
| `Tests/Support/FontWriter.h` | Builds TrueType, CFF and collection fonts with chosen tables, so tests need no font files |
| `Tests/`, `Benchmarks/`, `Fuzz/` | Every table and format, outlines point by point, rasters against shapes, the fonts of the machine when present |
| `ThirdParty/stb_truetype/`, `ThirdParty/stb_rect_pack/` | stb_truetype v1.26 and stb_rect_pack v1.01, unchanged |

## Reading a font

```cpp
using namespace helix::font;

Font font;
helix::String error;
if (!font.Open(fileBytes, 0, error))     // the bytes must outlive the font; index 1, 2... of a collection
    Log(error);
u32 glyph = font.GlyphIndex(U'A');
GlyphMetrics m = font.HMetrics(glyph);   // advance and left side bearing in font units

Outlines outlines;
outlines.Open(font, error);
Outline outline;
outlines.Get(glyph, outline, error);
```

- Collections: `Font::CountFonts` and the index of `Open`.
- Character maps: the best Unicode subtable is chosen (full Unicode, then the BMP, symbol, Mac Roman);
  `GlyphIndex(codepoint, selector)` reads variation sequences of format 14.
- Names come as UTF-8 from UTF-16 and Mac Roman records; `GlyphName` reads post formats 1 and 2.
- TrueType outlines: simple and composite glyphs (offsets, scales, 2x2 transforms, point matching, scaled offsets),
  short and long loca. CFF: every Type 2 operator (curves, flex, hints skipped, local and global subroutines,
  arithmetic and storage), seac accents and CID keyed fonts with FDSelect.
- Every read is checked against the table it reads; damaged data is an error naming the table or glyph, never a read
  outside the file.

## Layout tables

`Layout::Read(font, Tag("GSUB"), error)` (or GPOS) reads the script, language and feature lists and every lookup
subtable (GSUB 1 to 8, GPOS 1 to 9, extensions resolved) into pools of flat arrays: coverages and class definitions as
glyph ranges, value records, anchors, ligatures, rules and lookup records. `CoverageIndex` and `ClassOf` look glyphs up;
the comment at `LayoutSubtable` tells what each type keeps. Applying lookups is the text shaper's work.

## Glyph images

```cpp
RasterOptions r;
r.scale = 32.0f / font.Head().unitsPerEm;   // 32 pixels per em
r.shiftX = 0.25f;                          // a subpixel position
GlyphBitmap coverage;
Rasterize(outline, r, coverage, error);

DistanceOptions d;
d.scale = r.scale;
d.padding = 4;
d.range = 4;                               // 128 on the edge, 0 and 255 at 4 pixels out and in
GlyphBitmap msdf;
MultiChannelDistanceField(outline, d, msdf, error);
```

- Coverage: 8 bits, nonzero fill, subpixel offsets, oversampling 1 to 8 with box prefiltering for packed atlases.
- Signed distance fields: the exact distance to every line, quadratic and cubic, the sign from the fill.
- Multi channel distance fields (RGB, with the true field in alpha): edges colored at corners, pseudo-distances, the
  sign taken from the fill where edge directions disagree, clashing texels made plain distance texels. Written from
  Viktor Chlumsky's thesis "Shape Decomposition for Multi-channel Distance Fields".
- Color: CBDT PNG glyphs (index formats 1 to 5, image formats 17 to 19) through Helix.Image, the strike closest to the
  asked size; COLR v0 layers in their CPAL colors or the text color, drawn into RGBA.

`FontAtlas(width, height, channels, padding)` packs glyph bitmaps into pages on demand (`Add(key, bitmap, place)`), opens
a new page when one is full, and keeps the rectangle of each page written since the last upload.

## Speed

Release, one thread: a composite TrueType glyph outline about 1.2 us, a CFF glyph 0.3 us, the coverage of a 32 pixel
glyph 5 us, its signed distance field 65 us, a multi channel field of a curved glyph about 0.33 ms, 1000 glyphs into an
atlas 2.2 ms.

## Building

The repository builds with the presets of Helix.CMake (Windows, Web, Android); the submodules in `External/` are
Helix.CMake, Helix.Testing, Helix.Core, Helix.Platform, Helix.Profiler, Helix.Jobs and Helix.Image.

## License

GPL-3.0, see `LICENSE`. The copies in `ThirdParty/` are public domain, see `THIRD-PARTY.txt`.
