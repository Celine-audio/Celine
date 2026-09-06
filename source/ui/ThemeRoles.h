#pragma once

#include "../PluginThemeRoles.h"

/*
    Every themeable colour in the house, listed once.

    One list rather than several, because a colour has four things to say about itself
    and they all have to agree: what the code calls it, what the theme editor calls it,
    which group it is edited under, and what it is when nobody has changed it. Written
    out four times over -- an enum, a table, a set of accessors, a file format -- they
    agree only until one of them is edited. Here the preprocessor writes the other
    three from this one.

    X (identifier, "Label in the editor", "Group", 0xAARRGGBB)

    The identifier is also the key in a .celthm file, so **renaming one breaks every
    theme anybody has saved**. Add freely; rename only with a reason.

    Values are sampled from the Figma files rather than eyeballed, which is why they are
    odd numbers. The prose explaining what each one is *for* lives beside its accessor
    in Theme.h -- a macro cannot carry comments between its lines.
*/
#define CELINE_SHARED_THEME_ROLES(X)                                                    \
    X (chrome,            "Chrome",              "Surfaces", 0xff3b334b)                \
    X (background,        "Background",          "Surfaces", 0xff28262e)                \
    X (panel,             "Panel",               "Surfaces", 0xfff9fbff)                \
    X (surface,           "Control",             "Surfaces", 0xff37364a)                \
    X (surfaceBright,     "Control, hovered",    "Surfaces", 0xff4f485d)                \
    X (button,            "Button",              "Surfaces", 0xff37364a)                \
    X (field,             "Text field",          "Surfaces", 0xff28262e)                \
    X (line,              "Border",              "Surfaces", 0xffd9d9d9)                \
    X (consoleBackground, "Graph ground",        "Surfaces", 0xff17151a)                \
    X (pill,              "List row",            "Surfaces", 0xffdcdee4)                \
    X (grid,              "Grid line",           "Surfaces", 0xff5c5c5c)                \
                                                                                        \
    X (text,              "Text",                "Text",     0xfff9fbff)                \
    X (textDim,           "Text, idle",          "Text",     0xffd9d9d9)                \
    X (comment,           "Text, secondary",     "Text",     0xff888791)                \
    X (textDisabled,      "Text, disabled",      "Text",     0xff888791)                \
    X (textOnPanel,       "Text on panel",       "Text",     0xff28262e)                \
                                                                                        \
    X (accent,            "Accent",              "Accents",  0xff9761dc)                \
    X (accentAlt,         "Accent, secondary",   "Accents",  0xff4fc9e8)                \
    X (teal,              "Armed",               "Accents",  0xff8F63D5)                \
    X (violet,            "Brand violet",        "Accents",  0xff9761dc)                \
    X (toolActive,        "Control in force",    "Accents",  0xff8F63D5)                \
    X (track,             "Unfilled track",      "Accents",  0xff565656)                \
                                                                                        \
    X (record,            "Recording",           "States",   0xfff92672)                \
    X (danger,            "Danger",              "States",   0xfff92672)                \
    X (warning,           "Warning",             "States",   0xffe6db74)                \
    X (error,             "Error",               "States",   0xfff92672)

/** The shared list and whatever this plugin adds to it. */
#define CELINE_THEME_ROLES(X)                                                           \
    CELINE_SHARED_THEME_ROLES(X)                                                        \
    CELINE_PLUGIN_THEME_ROLES(X)
