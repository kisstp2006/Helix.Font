#pragma once

#include <Support/FontWriter.h>

// Fonts the tests, fuzz targets and benchmarks share.
namespace helix::font::testing
{
    // Glyphs: 0 .notdef (empty), 1 space, 2 'A' (a square with a square hole), 3 'O' (a circle of off-curve points),
    // 4 'D' (lines and a quadratic), 5 acute (a small square), 6 'Aacute' (A and the acute, composite), 7 a scaled and
    // rotated copy of D (composite with a 2x2 transform).
    inline FontSpec TrueTypeSpec()
    {
        FontSpec s;
        s.glyphs.Resize(8);
        s.glyphs[1].advance = 250;
        s.glyphs[1].name = "space";
        s.glyphs[2].name = "A";
        s.glyphs[2].advance = 600;
        s.glyphs[2].contours.Push(Square(50, 0, 550, 700));
        // The hole runs the other way.
        s.glyphs[2].contours.Push({{150, 100}, {450, 100}, {450, 600}, {150, 600}});
        s.glyphs[3].name = "O";
        s.glyphs[3].contours.Push({{300, 0, false}, {0, 350, false}, {300, 700, false}, {600, 350, false}});
        s.glyphs[3].advance = 650;
        s.glyphs[4].name = "D";
        s.glyphs[4].contours.Push({{50, 0}, {50, 700}, {300, 700}, {550, 700, false}, {550, 350}, {550, 0, false}, {300, 0}});
        s.glyphs[5].name = "acute";
        s.glyphs[5].contours.Push(Square(0, 0, 100, 100));
        s.glyphs[6].name = "Aacute";
        s.glyphs[6].advance = 600;
        s.glyphs[6].components.Push({2});
        Component accent;
        accent.glyph = 5;
        accent.dx = 250;
        accent.dy = 750;
        s.glyphs[6].components.Push(accent);
        Component turned;
        turned.glyph = 4;
        turned.transform = 4;
        turned.a = 0.5f, turned.b = 0.5f, turned.c = -0.5f, turned.d = 0.5f;
        turned.dx = 400;
        s.glyphs[7].components.Push(turned);
        s.glyphs[7].name = "Dturned";
        s.cmap = {{32, 1}, {65, 2}, {79, 3}, {68, 4}, {0xB4, 5}, {0xC1, 6}, {0x1F600, 7}};
        s.names = {{String("1"), String("Helix Test")}, {String("2"), String("Regular")}, {String("4"), String("Helix Test Regular")},
                   {String("257"), String("Mac Family")}};
        s.kerning = {{2, 3, -40}, {4, 2, 25}};
        return s;
    }

    // A CFF font: 0 .notdef, 1 a square drawn with rlineto, 2 a curve with every curve operator, 3 a glyph made of a
    // local and a global subroutine.
    inline FontSpec CffSpec()
    {
        FontSpec s;
        s.cff = true;
        s.glyphs.Resize(4);
        s.glyphs[0].charstring = Charstring().Op(14).bytes;
        // Width 600 first, then a 100 x 100 square at (50, 0).
        s.glyphs[1].charstring = Charstring().Num(600).Num(50).Num(0).Op(21).Num(100).Num(0).Num(0).Num(100).Num(-100).Num(0).Op(5).Op(14).bytes;
        s.glyphs[2].charstring = Charstring()
                                     .Num(0)
                                     .Num(0)
                                     .Op(21)
                                     .Num(10)
                                     .Num(20)
                                     .Num(30)
                                     .Num(40)
                                     .Num(50)
                                     .Num(60)
                                     .Op(8) // rrcurveto
                                     .Num(10)
                                     .Num(20)
                                     .Num(30)
                                     .Num(40)
                                     .Op(31) // hvcurveto
                                     .Num(10)
                                     .Num(20)
                                     .Num(30)
                                     .Num(40)
                                     .Num(5)
                                     .Op(30) // vhcurveto with the last extra
                                     .Num(7)
                                     .Num(10)
                                     .Num(20)
                                     .Num(30)
                                     .Num(40)
                                     .Op(27) // hhcurveto with dy1
                                     .Num(10)
                                     .Num(20)
                                     .Num(30)
                                     .Num(40)
                                     .Op(26) // vvcurveto
                                     .Op(14)
                                     .bytes;
        // Local subr 0 (bias 107: called with -107) draws a line, global subr 0 another.
        s.localSubrs.Push(Charstring().Num(100).Num(0).Op(5).Op(11).bytes);
        s.globalSubrs.Push(Charstring().Num(0).Num(100).Op(5).Op(11).bytes);
        s.glyphs[3].charstring = Charstring().Num(0).Num(0).Op(21).Num(-107).Op(10).Num(-107).Op(29).Op(14).bytes;
        s.cmap = {{65, 1}, {66, 2}, {67, 3}};
        return s;
    }
}
