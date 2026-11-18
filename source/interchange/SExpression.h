#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace Interchange
{
    //==========================================================================
    /** An S-expression, as KiCad writes its files: lists of symbols, numbers and quoted
        strings. Used to write and read .kicad_sch files. */
    struct SExpr
    {
        enum class Kind
        {
            symbol, // a bare token: a keyword or a number
            string, // a quoted string
            list
        };

        Kind kind = Kind::list;
        juce::String text;
        std::vector<SExpr> items;

        static SExpr symbol(const juce::String& s) { return {Kind::symbol, s, {}}; }
        static SExpr string(const juce::String& s) { return {Kind::string, s, {}}; }
        static SExpr number(double v);
        static SExpr list(std::initializer_list<SExpr> children) { return {Kind::list, {}, children}; }

        /** A list starting with the symbol `head`. */
        static SExpr node(const juce::String& head, std::initializer_list<SExpr> rest = {});

        SExpr& add(SExpr child)
        {
            items.push_back(std::move(child));
            return *this;
        }

        bool isList() const noexcept { return kind == Kind::list; }

        /** The first item's text, for a list; empty otherwise. */
        juce::String head() const;

        /** The first direct child list whose head is `name`, or null. */
        const SExpr* find(const juce::String& name) const;

        /** Every direct child list whose head is `name`. */
        std::vector<const SExpr*> findAll(const juce::String& name) const;

        /** Item `index`'s text (symbol or string), or `fallback`. */
        juce::String textAt(size_t index, const juce::String& fallback = {}) const;
        double numberAt(size_t index, double fallback = 0.0) const;

        /** Written as KiCad writes it: one list per line where that helps reading. */
        juce::String toString() const;

        /** Parses the first expression in `text`. Empty on a syntax error. */
        static std::optional<SExpr> parse(const juce::String& text);
    };
} // namespace Interchange
