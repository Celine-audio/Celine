#include "SExpression.h"

namespace Interchange
{
    SExpr SExpr::number(double v)
    {
        // Four decimals is a ten-thousandth of a millimetre, finer than KiCad's own grid.
        auto s = juce::String(v, 4);

        if (s.containsChar('.'))
            s = s.trimCharactersAtEnd("0").trimCharactersAtEnd(".");

        if (s == "-0")
            s = "0";

        return symbol(s);
    }

    SExpr SExpr::node(const juce::String& head, std::initializer_list<SExpr> rest)
    {
        SExpr e;
        e.items.push_back(symbol(head));
        for (const auto& r : rest)
            e.items.push_back(r);
        return e;
    }

    juce::String SExpr::head() const
    {
        return isList() && ! items.empty() && items.front().kind != Kind::list ? items.front().text : juce::String();
    }

    const SExpr* SExpr::find(const juce::String& name) const
    {
        for (const auto& item : items)
            if (item.head() == name)
                return &item;

        return nullptr;
    }

    std::vector<const SExpr*> SExpr::findAll(const juce::String& name) const
    {
        std::vector<const SExpr*> found;

        for (const auto& item : items)
            if (item.head() == name)
                found.push_back(&item);

        return found;
    }

    juce::String SExpr::textAt(size_t index, const juce::String& fallback) const
    {
        if (index < items.size() && items[index].kind != Kind::list)
            return items[index].text;

        return fallback;
    }

    double SExpr::numberAt(size_t index, double fallback) const
    {
        const auto t = textAt(index);
        return t.isEmpty() ? fallback : t.getDoubleValue();
    }

    namespace
    {
        juce::String quote(const juce::String& s)
        {
            return "\"" + s.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n") + "\"";
        }

        /** A list short enough, or deep enough, to sit on one line. */
        bool fitsOnOneLine(const SExpr& e)
        {
            for (const auto& item : e.items)
                if (item.isList() && ! item.items.empty() && item.items.size() > 1
                    && (item.head() == "symbol" || item.head() == "property" || item.head() == "pin"
                        || item.head() == "polyline" || item.head() == "wire" || item.head() == "lib_symbols"))
                    return false;

            return e.items.size() <= 8;
        }

        void write(const SExpr& e, juce::String& out, int depth)
        {
            if (e.kind == SExpr::Kind::symbol)
            {
                out << e.text;
                return;
            }

            if (e.kind == SExpr::Kind::string)
            {
                out << quote(e.text);
                return;
            }

            const bool oneLine = fitsOnOneLine(e);
            out << "(";

            for (size_t i = 0; i < e.items.size(); ++i)
            {
                if (i > 0)
                {
                    if (oneLine || ! e.items[i].isList())
                        out << " ";
                    else
                        out << "\n" << juce::String::repeatedString("\t", depth + 1);
                }

                write(e.items[i], out, depth + 1);
            }

            out << ")";
        }
    } // namespace

    juce::String SExpr::toString() const
    {
        juce::String out;
        write(*this, out, 0);
        return out + "\n";
    }

    std::optional<SExpr> SExpr::parse(const juce::String& text)
    {
        std::vector<SExpr> stack;
        stack.push_back({});

        auto p = text.getCharPointer();

        while (! p.isEmpty())
        {
            const auto c = *p;

            if (juce::CharacterFunctions::isWhitespace(c))
            {
                ++p;
            }
            else if (c == '(')
            {
                stack.push_back({});
                ++p;
            }
            else if (c == ')')
            {
                if (stack.size() < 2)
                    return std::nullopt;

                auto done = std::move(stack.back());
                stack.pop_back();
                stack.back().items.push_back(std::move(done));
                ++p;

                if (stack.size() == 1)
                    return stack.front().items.front();
            }
            else if (c == '"')
            {
                ++p;
                juce::String s;

                while (! p.isEmpty() && *p != '"')
                {
                    auto ch = *p;

                    if (ch == '\\')
                    {
                        ++p;
                        if (p.isEmpty())
                            return std::nullopt;
                        ch = *p == 'n' ? '\n' : *p;
                    }

                    s += juce::String::charToString(ch);
                    ++p;
                }

                if (p.isEmpty())
                    return std::nullopt;

                ++p;
                stack.back().items.push_back(string(s));
            }
            else
            {
                auto start = p;
                while (! p.isEmpty() && ! juce::CharacterFunctions::isWhitespace(*p) && *p != '(' && *p != ')')
                    ++p;

                stack.back().items.push_back(symbol(juce::String(start, p)));
            }
        }

        return std::nullopt;
    }
} // namespace Interchange
