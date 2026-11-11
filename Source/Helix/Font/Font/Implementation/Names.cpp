#include <Helix/Font/Font/Font.h>

namespace helix::font
{
    namespace
    {
        // Mac Roman 0x80 to 0xFF as Unicode.
        constexpr u16 kMacRoman[128] = {
            0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
            0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3, 0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
            0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
            0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211, 0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
            0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB, 0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
            0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
            0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1, 0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
            0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC, 0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,
        };

        // The standard Macintosh glyph order, separated by spaces.
        constexpr const char kStandardNames[] =
            ".notdef .null nonmarkingreturn space exclam quotedbl numbersign dollar percent ampersand quotesingle parenleft "
            "parenright asterisk plus comma hyphen period slash zero one two three four five six seven eight nine colon semicolon "
            "less equal greater question at A B C D E F G H I J K L M N O P Q R S T U V W X Y Z bracketleft backslash "
            "bracketright asciicircum underscore grave a b c d e f g h i j k l m n o p q r s t u v w x y z braceleft bar "
            "braceright asciitilde Adieresis Aring Ccedilla Eacute Ntilde Odieresis Udieresis aacute agrave acircumflex "
            "adieresis atilde aring ccedilla eacute egrave ecircumflex edieresis iacute igrave icircumflex idieresis ntilde "
            "oacute ograve ocircumflex odieresis otilde uacute ugrave ucircumflex udieresis dagger degree cent sterling section "
            "bullet paragraph germandbls registered copyright trademark acute dieresis notequal AE Oslash infinity plusminus "
            "lessequal greaterequal yen mu partialdiff summation product pi integral ordfeminine ordmasculine Omega ae oslash "
            "questiondown exclamdown logicalnot radical florin approxequal Delta guillemotleft guillemotright ellipsis "
            "nonbreakingspace Agrave Atilde Otilde OE oe endash emdash quotedblleft quotedblright quoteleft quoteright divide "
            "lozenge ydieresis Ydieresis fraction currency guilsinglleft guilsinglright fi fl daggerdbl periodcentered "
            "quotesinglbase quotedblbase perthousand Acircumflex Ecircumflex Aacute Edieresis Egrave Iacute Icircumflex "
            "Idieresis Igrave Oacute Ocircumflex apple Ograve Uacute Ucircumflex Ugrave dotlessi circumflex tilde macron breve "
            "dotaccent ring cedilla hungarumlaut ogonek caron Lslash lslash Scaron scaron Zcaron zcaron brokenbar Eth eth Yacute "
            "yacute Thorn thorn minus multiply onesuperior twosuperior threesuperior onehalf onequarter threequarters franc "
            "Gbreve gbreve Idotaccent Scedilla scedilla Cacute cacute Ccaron ccaron dcroat";

        struct StandardNames
        {
            StringView names[258];
            u32 count = 0;
            StandardNames()
            {
                StringView all(kStandardNames);
                usize at = 0;
                while (at < all.Size() && count < 258)
                {
                    usize space = all.Find(' ', at);
                    usize end = space == StringView::npos ? all.Size() : space;
                    names[count++] = all.Substr(at, end - at);
                    at = end + 1;
                }
            }
        };

        void AppendUtf8(String& out, u32 cp)
        {
            if (cp < 0x80)
                out.Append(char(cp));
            else if (cp < 0x800)
            {
                out.Append(char(0xC0 | (cp >> 6)));
                out.Append(char(0x80 | (cp & 63)));
            }
            else if (cp < 0x10000)
            {
                out.Append(char(0xE0 | (cp >> 12)));
                out.Append(char(0x80 | ((cp >> 6) & 63)));
                out.Append(char(0x80 | (cp & 63)));
            }
            else
            {
                out.Append(char(0xF0 | (cp >> 18)));
                out.Append(char(0x80 | ((cp >> 12) & 63)));
                out.Append(char(0x80 | ((cp >> 6) & 63)));
                out.Append(char(0x80 | (cp & 63)));
            }
        }

        // A glyph name of post format 2 that lives in the font: the high bit marks a file offset.
        constexpr u32 kInFont = 0x80000000u;
        constexpr u32 kMaxNames = 4096;
    }

    StringView StandardGlyphName(u32 index)
    {
        static const StandardNames names;
        return index < names.count ? names.names[index] : StringView();
    }

    void Font::ReadNames()
    {
        Bytes name = Table(Tag("name"));
        u16 count = name.U16(2);
        u16 strings = name.U16(4);
        if (!name.Has(6, usize(count) * 12))
            return;
        for (u32 i = 0; i < count && names_.Size() < kMaxNames; i++)
        {
            usize at = 6 + usize(i) * 12;
            NameRecord r;
            r.platform = name.U16(at);
            r.encoding = name.U16(at + 2);
            r.language = name.U16(at + 4);
            r.id = name.U16(at + 6);
            Bytes text = name.Sub(usize(strings) + name.U16(at + 10), name.U16(at + 8));
            if (text.Empty() && name.U16(at + 8) != 0)
                continue;
            if (r.platform == 0 || r.platform == 3)
            {
                for (usize c = 0; c + 1 < text.size; c += 2)
                {
                    u32 unit = text.U16(c);
                    if (unit >= 0xD800 && unit < 0xDC00 && c + 3 < text.size)
                    {
                        u32 low = text.U16(c + 2);
                        if (low >= 0xDC00 && low < 0xE000)
                        {
                            unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                            c += 2;
                        }
                        else
                            unit = 0xFFFD;
                    }
                    else if (unit >= 0xD800 && unit < 0xE000)
                        unit = 0xFFFD;
                    AppendUtf8(r.text, unit);
                }
            }
            else if (r.platform == 1 && r.encoding == 0)
            {
                for (usize c = 0; c < text.size; c++)
                {
                    u8 b = text.U8(c);
                    AppendUtf8(r.text, b < 0x80 ? b : kMacRoman[b - 0x80]);
                }
            }
            else
                continue;
            names_.Push(Move(r));
        }
    }

    StringView Font::Name(u16 id) const
    {
        const NameRecord* best = nullptr;
        int bestScore = 0;
        for (const NameRecord& r : names_)
        {
            if (r.id != id)
                continue;
            int score = r.platform == 3 && r.language == 0x409 ? 4 : r.platform == 3 || r.platform == 0 ? 3 : r.platform == 1 && r.language == 0 ? 2 : 1;
            if (score > bestScore)
                best = &r, bestScore = score;
        }
        return best ? best->text.View() : StringView();
    }

    void Font::ReadPost()
    {
        Bytes post = Table(Tag("post"));
        if (post.size < 32)
            return;
        post_.version = post.U32(0);
        post_.italicAngle = post.Fixed(4);
        post_.underlinePosition = post.I16(8);
        post_.underlineThickness = post.I16(10);
        post_.fixedPitch = post.U32(12) != 0;
        if (post_.version == 0x00010000)
        {
            for (u32 g = 0; g < 258 && g < glyphCount_; g++)
                glyphNames_.Push(g);
            return;
        }
        if (post_.version != 0x00020000)
            return;
        u16 n = post.U16(32);
        if (!post.Has(34, usize(n) * 2))
            return;
        // The Pascal strings after the index array, in order.
        Array<u32> strings;
        usize at = 34 + usize(n) * 2;
        while (at < post.size)
        {
            strings.Push(u32(at));
            at += 1 + usize(post.U8(at));
        }
        usize base = usize(post.data - file_.data);
        for (u32 g = 0; g < n; g++)
        {
            u16 index = post.U16(34 + 2 * usize(g));
            if (index < 258)
                glyphNames_.Push(index);
            else if (u32(index - 258) < strings.Size())
                glyphNames_.Push(kInFont | u32(base + strings[index - 258]));
            else
                glyphNames_.Push(0);
        }
    }

    StringView Font::GlyphName(u32 glyph) const
    {
        if (glyph >= glyphNames_.Size())
            return StringView();
        u32 v = glyphNames_[glyph];
        if (!(v & kInFont))
            return StandardGlyphName(v);
        usize at = v & ~kInFont;
        u8 length = file_.U8(at);
        Bytes text = file_.Sub(at + 1, length);
        return StringView(reinterpret_cast<const char*>(text.data), text.size);
    }
}
