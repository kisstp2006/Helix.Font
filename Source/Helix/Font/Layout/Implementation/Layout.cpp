#include <Helix/Font/Layout/Layout.h>

#include <Helix/Core/Strings/Format.h>

#include <algorithm>
#include <bit>

namespace helix::font
{
    namespace
    {
        void SortRanges(Array<GlyphRange>& r)
        {
            std::sort(r.begin(), r.end(), [](const GlyphRange& a, const GlyphRange& b) { return a.first < b.first; });
        }

        const GlyphRange* FindRange(const GlyphRange* begin, const GlyphRange* end, u32 glyph)
        {
            const GlyphRange* it = std::upper_bound(begin, end, glyph, [](u32 g, const GlyphRange& r) { return g < r.first; });
            if (it == begin)
                return nullptr;
            --it;
            return glyph <= it->last ? it : nullptr;
        }

        usize ValueSize(u16 format) { return 2 * usize(std::popcount(u32(format & 0xFF))); }
    }

    bool ReadCoverage(Bytes t, Array<GlyphRange>& out)
    {
        out.Clear();
        u16 format = t.U16(0), n = t.U16(2);
        if (format == 1)
        {
            if (!t.Has(4, usize(n) * 2))
                return false;
            for (u32 i = 0; i < n; i++)
            {
                u16 g = t.U16(4 + 2 * usize(i));
                if (!out.Empty() && u32(out.Back().last) + 1 == g && u32(out.Back().value) + (out.Back().last - out.Back().first) + 1 == i)
                    out.Back().last = g;
                else
                    out.Push({g, g, u16(i)});
            }
        }
        else if (format == 2)
        {
            if (!t.Has(4, usize(n) * 6))
                return false;
            for (u32 i = 0; i < n; i++)
            {
                usize at = 4 + 6 * usize(i);
                GlyphRange r{t.U16(at), t.U16(at + 2), t.U16(at + 4)};
                if (r.first > r.last)
                    return false;
                out.Push(r);
            }
        }
        else
            return false;
        SortRanges(out);
        return true;
    }

    bool ReadClassDef(Bytes t, Array<GlyphRange>& out)
    {
        out.Clear();
        u16 format = t.U16(0);
        if (format == 1)
        {
            u16 start = t.U16(2), n = t.U16(4);
            if (!t.Has(6, usize(n) * 2) || u32(start) + n > 0x10000)
                return false;
            for (u32 i = 0; i < n; i++)
            {
                u16 c = t.U16(6 + 2 * usize(i));
                u16 g = u16(start + i);
                if (c == 0)
                    continue;
                if (!out.Empty() && u32(out.Back().last) + 1 == g && out.Back().value == c)
                    out.Back().last = g;
                else
                    out.Push({g, g, c});
            }
        }
        else if (format == 2)
        {
            u16 n = t.U16(2);
            if (!t.Has(4, usize(n) * 6))
                return false;
            for (u32 i = 0; i < n; i++)
            {
                usize at = 4 + 6 * usize(i);
                GlyphRange r{t.U16(at), t.U16(at + 2), t.U16(at + 4)};
                if (r.first > r.last)
                    return false;
                out.Push(r);
            }
        }
        else
            return false;
        SortRanges(out);
        return true;
    }

    struct LayoutReader
    {
        LayoutReader(Layout& layout, Bytes table, String& e) : l(layout), t(table), error(e) {}

        Layout& l;
        Bytes t;
        String& error;
        bool failed = false;
        HashMap<u32, u32> coverageAt, classDefAt;
        // Entries the pools may still take: a table whose offsets point many times at the same large data cannot make
        // the reader work without end.
        usize budget = usize(1) << 22;
        Array<GlyphRange> scratch;

        bool Fail(StringView what)
        {
            if (!failed)
            {
                failed = true;
                error = String(what);
            }
            return false;
        }

        bool Spend(usize n)
        {
            if (n > budget)
                return Fail("a layout table that decodes to more than any font holds");
            budget -= n;
            return true;
        }

        u32 Coverage(usize at)
        {
            if (at == 0 || at >= t.size)
                return Fail(Format("a coverage at {} outside the table", at)), kNoIndex;
            if (const u32* known = coverageAt.Find(u32(at)))
                return *known;
            if (!ReadCoverage(t.From(at), scratch))
                return Fail(Format("a damaged coverage at {}", at)), kNoIndex;
            if (!Spend(scratch.Size() + 1) || l.coverages.Size() >= 0xFFFF)
                return Fail("more coverages than any font has"), kNoIndex;
            u32 index = u32(l.coverages.Size());
            l.coverages.Push({u32(l.ranges.Size()), u32(scratch.Size())});
            l.ranges.Append(scratch.Data(), scratch.Size());
            coverageAt.Set(u32(at), index);
            return index;
        }

        // A class definition; offset 0 means every glyph is class 0.
        u32 ClassDef(usize at)
        {
            if (at == 0)
                return kNoIndex;
            if (at >= t.size)
                return Fail(Format("a class definition at {} outside the table", at)), kNoIndex;
            if (const u32* known = classDefAt.Find(u32(at)))
                return *known;
            if (!ReadClassDef(t.From(at), scratch))
                return Fail(Format("a damaged class definition at {}", at)), kNoIndex;
            if (!Spend(scratch.Size() + 1))
                return kNoIndex;
            u32 index = u32(l.classDefs.Size());
            l.classDefs.Push({u32(l.ranges.Size()), u32(scratch.Size())});
            l.ranges.Append(scratch.Data(), scratch.Size());
            classDefAt.Set(u32(at), index);
            return index;
        }

        // `count` u16 values at `at` into u16s.
        LayoutList U16s(usize at, usize count)
        {
            if (!t.Has(at, count * 2))
                return Fail(Format("an array at {} outside the table", at)), LayoutList();
            if (!Spend(count))
                return {};
            LayoutList list{u32(l.u16s.Size()), u32(count)};
            for (usize i = 0; i < count; i++)
                l.u16s.Push(t.U16(at + 2 * i));
            return list;
        }

        u32 Value(usize at, u16 format)
        {
            if (!Spend(1))
                return kNoIndex;
            ValueRecord v;
            usize p = at;
            if (format & 1)
                v.xPlacement = t.I16(p), p += 2;
            if (format & 2)
                v.yPlacement = t.I16(p), p += 2;
            if (format & 4)
                v.xAdvance = t.I16(p), p += 2;
            if (format & 8)
                v.yAdvance = t.I16(p);
            l.values.Push(v);
            return u32(l.values.Size() - 1);
        }

        u32 AnchorAt(usize at)
        {
            if (!Spend(1))
                return kNoIndex;
            Anchor a;
            if (at != 0)
            {
                u16 format = t.U16(at);
                if (at >= t.size || format < 1 || format > 3)
                    return Fail(Format("a damaged anchor at {}", at)), kNoIndex;
                a.x = t.I16(at + 2);
                a.y = t.I16(at + 4);
                if (format == 2)
                    a.point = t.U16(at + 6);
                a.present = true;
            }
            l.anchors.Push(a);
            return u32(l.anchors.Size() - 1);
        }

        // An array of `count` u16 offsets at `at`, each from `base`, read by `item` into one list each.
        template <class F>
        LayoutList OffsetLists(usize at, usize count, usize base, F&& item)
        {
            if (!t.Has(at, count * 2))
                return Fail(Format("offsets at {} outside the table", at)), LayoutList();
            if (!Spend(count))
                return {};
            u32 first = u32(l.lists.Size());
            l.lists.Resize(l.lists.Size() + count);
            for (usize i = 0; i < count && !failed; i++)
            {
                u16 off = t.U16(at + 2 * i);
                LayoutList list = off == 0 ? LayoutList() : item(base + off);
                l.lists[first + i] = list;
            }
            return {first, u32(count)};
        }

        LayoutList Records(usize at, usize count)
        {
            if (!t.Has(at, count * 4))
                return Fail(Format("lookup records at {} outside the table", at)), LayoutList();
            if (!Spend(count))
                return {};
            LayoutList list{u32(l.records.Size()), u32(count)};
            for (usize i = 0; i < count; i++)
                l.records.Push({t.U16(at + 4 * i), t.U16(at + 4 * i + 2)});
            return list;
        }

        // Coverage indices of `count` coverage offsets from `base`.
        LayoutList Coverages(usize at, usize count, usize base)
        {
            if (!t.Has(at, count * 2))
                return Fail(Format("coverage offsets at {} outside the table", at)), LayoutList();
            if (!Spend(count))
                return {};
            Array<u16> found;
            for (usize i = 0; i < count && !failed; i++)
                found.Push(u16(Coverage(base + t.U16(at + 2 * i))));
            LayoutList list{u32(l.u16s.Size()), u32(count)};
            l.u16s.Append(found.Data(), found.Size());
            return list;
        }

        // The rules of one rule set of a context (chained or not) lookup, format 1 or 2.
        LayoutList RuleSet(usize set, bool chained)
        {
            u16 n = t.U16(set);
            if (!t.Has(set + 2, usize(n) * 2))
                return Fail(Format("a rule set at {} outside the table", set)), LayoutList();
            if (!Spend(n))
                return {};
            Array<SequenceRule> rules;
            for (u32 i = 0; i < n && !failed; i++)
            {
                usize r = set + t.U16(set + 2 + 2 * usize(i));
                SequenceRule rule;
                if (chained)
                {
                    u16 back = t.U16(r);
                    rule.backtrack = U16s(r + 2, back);
                    usize at = r + 2 + 2 * usize(back);
                    u16 input = t.U16(at);
                    rule.input = U16s(at + 2, input > 0 ? input - 1 : 0);
                    at += 2 + 2 * usize(input > 0 ? input - 1 : 0);
                    u16 ahead = t.U16(at);
                    rule.lookahead = U16s(at + 2, ahead);
                    at += 2 + 2 * usize(ahead);
                    rule.records = Records(at + 2, t.U16(at));
                }
                else
                {
                    u16 input = t.U16(r), count = t.U16(r + 2);
                    rule.input = U16s(r + 4, input > 0 ? input - 1 : 0);
                    rule.records = Records(r + 4 + 2 * usize(input > 0 ? input - 1 : 0), count);
                }
                rules.Push(rule);
            }
            LayoutList list{u32(l.rules.Size()), u32(rules.Size())};
            l.rules.Append(rules.Data(), rules.Size());
            return list;
        }

        bool Context(usize s, LayoutSubtable& st, bool chained)
        {
            if (st.format == 1 || st.format == 2)
            {
                st.coverage = Coverage(s + t.U16(s + 2));
                usize at = s + 4;
                if (st.format == 2)
                {
                    if (chained)
                    {
                        st.classDef2 = ClassDef(t.U16(s + 4) ? s + t.U16(s + 4) : 0);
                        st.classDef1 = ClassDef(t.U16(s + 6) ? s + t.U16(s + 6) : 0);
                        st.classDef3 = ClassDef(t.U16(s + 8) ? s + t.U16(s + 8) : 0);
                        at = s + 10;
                    }
                    else
                    {
                        st.classDef1 = ClassDef(t.U16(s + 4) ? s + t.U16(s + 4) : 0);
                        at = s + 6;
                    }
                }
                st.data = OffsetLists(at + 2, t.U16(at), s, [&](usize set) { return RuleSet(set, chained); });
                return !failed;
            }
            if (st.format != 3)
                return Fail(Format("context lookup format {}", st.format));
            SequenceRule rule;
            if (chained)
            {
                usize at = s + 2;
                u16 back = t.U16(at);
                rule.backtrack = Coverages(at + 2, back, s);
                at += 2 + 2 * usize(back);
                u16 input = t.U16(at);
                rule.input = Coverages(at + 2, input, s);
                at += 2 + 2 * usize(input);
                u16 ahead = t.U16(at);
                rule.lookahead = Coverages(at + 2, ahead, s);
                at += 2 + 2 * usize(ahead);
                rule.records = Records(at + 2, t.U16(at));
            }
            else
            {
                u16 input = t.U16(s + 2), count = t.U16(s + 4);
                rule.input = Coverages(s + 6, input, s);
                rule.records = Records(s + 6 + 2 * usize(input), count);
            }
            if (failed)
                return false;
            if (rule.input.count > 0)
                st.coverage = l.u16s[rule.input.begin];
            st.data = {u32(l.rules.Size()), 1};
            l.rules.Push(rule);
            return true;
        }

        bool Subtable(usize s, u16 type, LayoutSubtable& st)
        {
            if (s + 2 > t.size)
                return Fail(Format("a subtable at {} outside the table", s));
            st.type = type;
            st.format = t.U16(s);
            bool gpos = l.gpos;
            auto bad = [&] { return Fail(Format("{} lookup type {} format {}", gpos ? "GPOS" : "GSUB", type, st.format)); };
            if (!gpos)
            {
                switch (type)
                {
                case 1:
                    st.coverage = Coverage(s + t.U16(s + 2));
                    if (st.format == 1)
                        st.delta = t.I16(s + 4);
                    else if (st.format == 2)
                        st.data = U16s(s + 6, t.U16(s + 4));
                    else
                        return bad();
                    return !failed;
                case 2:
                case 3:
                    if (st.format != 1)
                        return bad();
                    st.coverage = Coverage(s + t.U16(s + 2));
                    st.data = OffsetLists(s + 6, t.U16(s + 4), s, [&](usize seq) { return U16s(seq + 2, t.U16(seq)); });
                    return !failed;
                case 4:
                    if (st.format != 1)
                        return bad();
                    st.coverage = Coverage(s + t.U16(s + 2));
                    st.data = OffsetLists(s + 6, t.U16(s + 4), s, [&](usize set) {
                        u16 n = t.U16(set);
                        if (!t.Has(set + 2, usize(n) * 2) || !Spend(n))
                            return Fail("a ligature set outside the table"), LayoutList();
                        LayoutList list{u32(l.ligatures.Size()), n};
                        Array<LigatureEntry> found;
                        for (u32 i = 0; i < n && !failed; i++)
                        {
                            usize lig = set + t.U16(set + 2 + 2 * usize(i));
                            u16 components = t.U16(lig + 2);
                            found.Push({t.U16(lig), U16s(lig + 4, components > 0 ? components - 1 : 0)});
                        }
                        l.ligatures.Append(found.Data(), found.Size());
                        return list;
                    });
                    return !failed;
                case 5: return Context(s, st, false);
                case 6: return Context(s, st, true);
                case 8:
                {
                    if (st.format != 1)
                        return bad();
                    st.coverage = Coverage(s + t.U16(s + 2));
                    SequenceRule rule;
                    usize at = s + 4;
                    u16 back = t.U16(at);
                    rule.backtrack = Coverages(at + 2, back, s);
                    at += 2 + 2 * usize(back);
                    u16 ahead = t.U16(at);
                    rule.lookahead = Coverages(at + 2, ahead, s);
                    at += 2 + 2 * usize(ahead);
                    st.data2 = U16s(at + 2, t.U16(at));
                    st.data = {u32(l.rules.Size()), 1};
                    l.rules.Push(rule);
                    return !failed;
                }
                default: return bad();
                }
            }
            switch (type)
            {
            case 1:
            {
                st.coverage = Coverage(s + t.U16(s + 2));
                st.valueFormat1 = t.U16(s + 4);
                if (st.format == 1)
                    st.data = {Value(s + 6, st.valueFormat1), 1};
                else if (st.format == 2)
                {
                    u16 n = t.U16(s + 6);
                    usize size = ValueSize(st.valueFormat1);
                    if (!t.Has(s + 8, usize(n) * size) || !Spend(n))
                        return Fail("single adjustment values outside the table");
                    st.data = {u32(l.values.Size()), n};
                    for (u32 i = 0; i < n; i++)
                        Value(s + 8 + usize(i) * size, st.valueFormat1);
                }
                else
                    return bad();
                return !failed;
            }
            case 2:
            {
                st.coverage = Coverage(s + t.U16(s + 2));
                st.valueFormat1 = t.U16(s + 4);
                st.valueFormat2 = t.U16(s + 6);
                usize v1 = ValueSize(st.valueFormat1), v2 = ValueSize(st.valueFormat2);
                if (st.format == 1)
                {
                    st.data = OffsetLists(s + 10, t.U16(s + 8), s, [&](usize set) {
                        u16 n = t.U16(set);
                        usize step = 2 + v1 + v2;
                        if (!t.Has(set + 2, usize(n) * step) || !Spend(n))
                            return Fail("a pair set outside the table"), LayoutList();
                        LayoutList list{u32(l.pairs.Size()), n};
                        for (u32 i = 0; i < n; i++)
                        {
                            usize at = set + 2 + usize(i) * step;
                            PairValue p;
                            p.second = t.U16(at);
                            if (st.valueFormat1)
                                p.value1 = Value(at + 2, st.valueFormat1);
                            if (st.valueFormat2)
                                p.value2 = Value(at + 2 + v1, st.valueFormat2);
                            l.pairs.Push(p);
                        }
                        return list;
                    });
                }
                else if (st.format == 2)
                {
                    st.classDef1 = ClassDef(t.U16(s + 8) ? s + t.U16(s + 8) : 0);
                    st.classDef2 = ClassDef(t.U16(s + 10) ? s + t.U16(s + 10) : 0);
                    st.count1 = t.U16(s + 12);
                    st.count2 = t.U16(s + 14);
                    usize n = usize(st.count1) * st.count2;
                    if (!t.Has(s + 16, n * (v1 + v2)) || !Spend(2 * n))
                        return Fail("class pair values outside the table");
                    st.data = {u32(l.values.Size()), u32(2 * n)};
                    for (usize i = 0; i < n; i++)
                    {
                        usize at = s + 16 + i * (v1 + v2);
                        Value(at, st.valueFormat1);
                        Value(at + v1, st.valueFormat2);
                    }
                }
                else
                    return bad();
                return !failed;
            }
            case 3:
            {
                if (st.format != 1)
                    return bad();
                st.coverage = Coverage(s + t.U16(s + 2));
                u16 n = t.U16(s + 4);
                if (!t.Has(s + 6, usize(n) * 4) || !Spend(2 * usize(n)))
                    return Fail("cursive records outside the table");
                st.data = {u32(l.anchors.Size()), 2 * u32(n)};
                for (u32 i = 0; i < n && !failed; i++)
                {
                    u16 entry = t.U16(s + 6 + 4 * usize(i)), exit = t.U16(s + 8 + 4 * usize(i));
                    AnchorAt(entry ? s + entry : 0);
                    AnchorAt(exit ? s + exit : 0);
                }
                return !failed;
            }
            case 4:
            case 5:
            case 6:
            {
                if (st.format != 1)
                    return bad();
                st.coverage = Coverage(s + t.U16(s + 2));
                st.coverage2 = Coverage(s + t.U16(s + 4));
                st.count1 = t.U16(s + 6);
                usize markArray = s + t.U16(s + 8), baseArray = s + t.U16(s + 10);
                u16 marks = t.U16(markArray);
                if (!t.Has(markArray + 2, usize(marks) * 4) || !Spend(marks))
                    return Fail("a mark array outside the table");
                st.data = {u32(l.marks.Size()), marks};
                for (u32 i = 0; i < marks && !failed; i++)
                {
                    u16 cls = t.U16(markArray + 2 + 4 * usize(i));
                    u16 anchor = t.U16(markArray + 4 + 4 * usize(i));
                    u32 a = AnchorAt(anchor ? markArray + anchor : 0);
                    l.marks.Push({cls, a});
                }
                u16 bases = t.U16(baseArray);
                if (type != 5)
                {
                    usize n = usize(bases) * st.count1;
                    if (!t.Has(baseArray + 2, n * 2) || !Spend(n))
                        return Fail("a base array outside the table");
                    st.data2 = {u32(l.anchors.Size()), u32(n)};
                    for (usize i = 0; i < n && !failed; i++)
                    {
                        u16 off = t.U16(baseArray + 2 + 2 * i);
                        AnchorAt(off ? baseArray + off : 0);
                    }
                    return !failed;
                }
                st.data2 = OffsetLists(baseArray + 2, bases, baseArray, [&](usize attach) {
                    u16 components = t.U16(attach);
                    usize n = usize(components) * st.count1;
                    if (!t.Has(attach + 2, n * 2) || !Spend(n))
                        return Fail("a ligature attachment outside the table"), LayoutList();
                    LayoutList list{u32(l.anchors.Size()), u32(n)};
                    for (usize i = 0; i < n && !failed; i++)
                    {
                        u16 off = t.U16(attach + 2 + 2 * i);
                        AnchorAt(off ? attach + off : 0);
                    }
                    return list;
                });
                return !failed;
            }
            case 7: return Context(s, st, false);
            case 8: return Context(s, st, true);
            default: return bad();
            }
        }

        bool Run()
        {
            if (t.U16(0) != 1)
                return Fail(Format("layout table version {}.{}", t.U16(0), t.U16(2)));
            usize scriptList = t.U16(4), featureList = t.U16(6), lookupList = t.U16(8);

            u16 scripts = t.U16(scriptList);
            if (!t.Has(scriptList + 2, usize(scripts) * 6))
                return Fail("a script list outside the table");
            for (u32 i = 0; i < scripts && !failed; i++)
            {
                usize rec = scriptList + 2 + 6 * usize(i);
                usize s = scriptList + t.U16(rec + 4);
                LayoutScript script;
                script.tag = t.U32(rec);
                auto language = [&](usize at, u32 tag) {
                    LayoutLanguage lang;
                    lang.tag = tag;
                    lang.requiredFeature = t.U16(at + 2);
                    u16 n = t.U16(at + 4);
                    if (!t.Has(at + 6, usize(n) * 2) || !Spend(n))
                        return Fail("a language system outside the table");
                    lang.features = {u32(l.indices.Size()), n};
                    for (u32 f = 0; f < n; f++)
                        l.indices.Push(t.U16(at + 6 + 2 * usize(f)));
                    l.languages.Push(lang);
                    return true;
                };
                if (u16 def = t.U16(s))
                {
                    script.defaultLanguage = u32(l.languages.Size());
                    if (!language(s + def, Tag("dflt")))
                        return false;
                }
                u16 langs = t.U16(s + 2);
                if (!t.Has(s + 4, usize(langs) * 6))
                    return Fail("language records outside the table");
                script.languages = {u32(l.languages.Size()), langs};
                for (u32 k = 0; k < langs; k++)
                    if (!language(s + t.U16(s + 8 + 6 * usize(k)), t.U32(s + 4 + 6 * usize(k))))
                        return false;
                l.scripts.Push(script);
            }

            u16 features = t.U16(featureList);
            if (!t.Has(featureList + 2, usize(features) * 6))
                return Fail("a feature list outside the table");
            for (u32 i = 0; i < features; i++)
            {
                usize rec = featureList + 2 + 6 * usize(i);
                usize f = featureList + t.U16(rec + 4);
                u16 n = t.U16(f + 2);
                if (!t.Has(f + 4, usize(n) * 2) || !Spend(n))
                    return Fail(Format("feature {} outside the table", i));
                LayoutFeature feature;
                feature.tag = t.U32(rec);
                feature.lookups = {u32(l.indices.Size()), n};
                for (u32 k = 0; k < n; k++)
                    l.indices.Push(t.U16(f + 4 + 2 * usize(k)));
                l.features.Push(feature);
            }

            u16 lookups = t.U16(lookupList);
            if (!t.Has(lookupList + 2, usize(lookups) * 2))
                return Fail("a lookup list outside the table");
            u16 extension = l.gpos ? 9 : 7;
            for (u32 i = 0; i < lookups && !failed; i++)
            {
                usize at = lookupList + t.U16(lookupList + 2 + 2 * usize(i));
                LayoutLookup lookup;
                u16 declared = t.U16(at);
                lookup.type = declared;
                lookup.flags = t.U16(at + 2);
                u16 n = t.U16(at + 4);
                if (!t.Has(at + 6, usize(n) * 2) || !Spend(n))
                    return Fail(Format("lookup {} outside the table", i));
                if (lookup.flags & 0x10)
                    lookup.markFilteringSet = t.U16(at + 6 + 2 * usize(n));
                lookup.subtables = {u32(l.subtables.Size()), n};
                for (u32 k = 0; k < n && !failed; k++)
                {
                    usize s = at + t.U16(at + 6 + 2 * usize(k));
                    u16 type = declared;
                    if (type == extension)
                    {
                        if (t.U16(s) != 1)
                            return Fail(Format("lookup {}: extension format {}", i, t.U16(s)));
                        type = t.U16(s + 2);
                        s += t.U32(s + 4);
                        if (type == extension)
                            return Fail(Format("lookup {}: an extension of an extension", i));
                    }
                    LayoutSubtable st;
                    if (!Subtable(s, type, st))
                    {
                        error = Format("lookup {}: {}", i, error);
                        return false;
                    }
                    l.subtables.Push(st);
                    if (k == 0)
                        lookup.type = type;
                }
                l.lookups.Push(lookup);
            }
            return !failed;
        }
    };

    bool Layout::Read(const Font& font, u32 tag, String& error)
    {
        *this = Layout();
        gpos = tag == Tag("GPOS");
        Bytes t = font.Table(tag);
        if (t.Empty())
            return true;
        LayoutReader r{*this, t, error};
        if (r.Run())
            return true;
        bool g = gpos;
        *this = Layout();
        gpos = g;
        error = Format("{}: {}", g ? "GPOS" : "GSUB", error);
        return false;
    }

    u32 Layout::CoverageIndex(u32 coverage, u32 glyph) const
    {
        if (coverage >= coverages.Size())
            return kNoIndex;
        const LayoutList& c = coverages[coverage];
        const GlyphRange* r = FindRange(ranges.Data() + c.begin, ranges.Data() + c.begin + c.count, glyph);
        return r ? u32(r->value) + (glyph - r->first) : kNoIndex;
    }

    u16 Layout::ClassOf(u32 classDef, u32 glyph) const
    {
        if (classDef >= classDefs.Size())
            return 0;
        const LayoutList& c = classDefs[classDef];
        const GlyphRange* r = FindRange(ranges.Data() + c.begin, ranges.Data() + c.begin + c.count, glyph);
        return r ? r->value : 0;
    }

    const LayoutScript* Layout::FindScript(u32 tag) const
    {
        for (const LayoutScript& s : scripts)
            if (s.tag == tag)
                return &s;
        return nullptr;
    }

    const LayoutFeature* Layout::FindFeature(u32 tag) const
    {
        for (const LayoutFeature& f : features)
            if (f.tag == tag)
                return &f;
        return nullptr;
    }

    u16 GlyphDefinitions::Class(const Array<GlyphRange>& ranges, u32 glyph)
    {
        const GlyphRange* r = FindRange(ranges.Data(), ranges.Data() + ranges.Size(), glyph);
        return r ? r->value : 0;
    }

    bool GlyphDefinitions::InMarkSet(u32 set, u32 glyph) const
    {
        if (set >= markSets_.Size())
            return false;
        const Array<GlyphRange>& r = markSets_[set];
        return FindRange(r.Data(), r.Data() + r.Size(), glyph) != nullptr;
    }

    bool GlyphDefinitions::Read(const Font& font, String& error)
    {
        *this = GlyphDefinitions();
        Bytes t = font.Table(Tag("GDEF"));
        if (t.Empty())
            return true;
        if (t.U16(0) != 1)
            return error = Format("GDEF version {}.{}", t.U16(0), t.U16(2)), false;
        if (u16 off = t.U16(4); off && !ReadClassDef(t.From(off), glyphClasses_))
            return error = "GDEF: a damaged glyph class definition", false;
        if (u16 off = t.U16(10); off && !ReadClassDef(t.From(off), markClasses_))
            return error = "GDEF: a damaged mark attachment class definition", false;
        if (t.U16(2) >= 2 && t.size >= 14)
        {
            if (u16 sets = t.U16(12))
            {
                Bytes s = t.From(sets);
                u16 n = s.U16(2);
                if (s.U16(0) != 1 || !s.Has(4, usize(n) * 4))
                    return error = "GDEF: damaged mark glyph sets", false;
                for (u32 i = 0; i < n; i++)
                    if (!ReadCoverage(s.From(s.U32(4 + 4 * usize(i))), markSets_.Emplace()))
                        return error = Format("GDEF: mark glyph set {} is damaged", i), false;
            }
        }
        return true;
    }
}
