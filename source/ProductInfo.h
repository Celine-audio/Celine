#pragma once

#include <juce_core/juce_core.h>

/**
    The handful of facts about this plugin that are not in CMakeLists.txt, gathered
    in one place so that starting a new project means editing one file rather than
    hunting through prose.

    The About window builds itself from these, so filling them in is the whole of the
    licence-notice work a new plugin has to do.
*/
namespace ProductInfo
{
    /** One line saying what the plugin is, shown under the mark in the About window.
        Sentence case, no full stop -- it is a label, not a sentence. */
    inline constexpr auto tagline = "Circuit designer.";

    /** Where the source lives. The AGPL requires that anyone given a binary can get
        the corresponding source, and this is the address that serves that right, so
        it has to be real before you ship anything. */
    inline constexpr auto repositoryUrl = "https://github.com/Celine-audio/Celine";

    /** The plugin's name drawn as artwork, embedded under this filename.

        DESIGNER rather than CELINE, and shown in the About window alone -- not in the
        toolbar, where the other house plugins wear theirs. This is the plugin the
        company is named after, so the house mark in the titlebar *is* its name; the
        wordmark says which Céline this one is, which is a thing to read once in the
        About window rather than to sit above the sheet all day. */
    inline constexpr auto wordmarkAsset = "designer.svg";

    /** Anything this plugin has to credit that the house list does not already cover.

        The About window is the only route to the licence notice inside a host, so a
        plugin with obligations of its own -- a model taken from a paper, a dataset,
        artwork under its own terms -- says so here rather than in a fork of the panel.
        Empty is the normal answer, and appends nothing. */
    inline constexpr auto extraNotices =
        "\n"
        "\n"
        "CIRCUIT MODELS\n"
        "\n"
        "The valve models implement the equations of Norman Koren, and of Dempwolf and "
        "Z\xc3\xb6lzer, \"A physically-motivated triode model for circuit simulations\" (DAFx-11). "
        "Device parameters are fitted to manufacturer datasheets, which are credited in the "
        "header that uses them.\n"
        "\n"
        "What the simulation does not model is documented in LIMITATIONS.md. Read it before "
        "trusting a result or blaming a circuit.\n";

    inline constexpr auto companyName = "C\xc3\xa9line Audio";
    inline constexpr auto copyrightYear = "2026";

    /** The framework version the notices claim, kept here so the About window and the
        THIRD-PARTY-NOTICES file cannot drift apart. */
    inline constexpr auto juceVersion = "9.0.1";
}
