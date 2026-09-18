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

    Two of the four fields are free to change at any time. The label and the group are
    read only by the editor -- they never reach the file -- so relabelling a colour or
    moving it to another heading costs nothing and breaks nothing.

    **Groups are ordered by this list, and the editor starts a new heading wherever the
    group changes.** So the order here is the order somebody scans, and roles sharing a
    group have to sit together. They read top-down the way you look at the window: the
    frame first, then the two areas that own most of it, then the things that appear
    all over and cannot honestly belong to one place.

    Which is the rule for where a colour goes. A role earns a place in an *area* group
    only if that area is the one thing it paints -- the header band is the header's and
    nothing else's. A role used by controls on the chrome and controls on a panel alike
    belongs in Controls or Text, because filing it under either area would be a lie the
    first time somebody changed it and something else moved.

    **The first three columns are the house's, and are meant to read the same in every
    plugin.** What only one plugin can have a use for goes in its own PluginThemeRoles.h
    instead -- not because the split costs anything at runtime, but because a role nobody
    in the room can point at is a control that does nothing, and a window of those is
    what makes a theme editor tiring to use.

    The fourth column is not shared, and is not meant to be. A role is a job; what a
    plugin ships for that job is its own design, and two plugins agreeing on nearly all
    of them is a house style rather than a rule. Where they differ it is because the
    windows differ -- so copy this list between plugins, but read the values.

    Values are sampled from the Figma files rather than eyeballed, which is why they are
    odd numbers. The prose explaining what each one is *for* lives beside its accessor
    in Theme.h -- a macro cannot carry comments between its lines.
*/
#define CELINE_SHARED_THEME_ROLES(X)                                                    \
    X (consoleBackground, "Window ground",       "Window",   0xff17151a)                \
    X (background,        "Inset ground",        "Window",   0xff28262e)                \
    X (line,              "Border",              "Window",   0xffd9d9d9)                \
                                                                                        \
    X (headerBackground,  "Header band",         "Header",   0xff3b334b)                \
    X (headerText,        "Logo and icons",      "Header",   0xfff9fbff)                \
                                                                                        \
    X (graphText,         "Labels",              "Graph",    0xffb5b3c4)                \
    X (grid,              "Grid line",           "Graph",    0xff5c5c5c)                \
                                                                                        \
    X (button,            "Button",              "Controls", 0xff474660)                \
    X (field,             "Text field",          "Controls", 0xff28262e)                \
    X (surface,           "Control",             "Controls", 0xff37364a)                \
    X (surfaceBright,     "Hover and selection", "Controls", 0xff4f485d)                \
    X (track,             "Unfilled track",      "Controls", 0xff565656)                \
    X (handle,            "Knob cap and grip",   "Controls", 0xfff9fbff)                \
                                                                                        \
    X (text,              "Text",                "Text",     0xfff9fbff)                \
    X (textDim,           "Text, idle",          "Text",     0xffd9d9d9)                \
    X (comment,           "Text, secondary",     "Text",     0xff888791)                \
    X (textDisabled,      "Text, disabled",      "Text",     0xff888791)                \
                                                                                        \
    X (chrome,            "Popup ground",        "Panels",   0xff3b334b)                \
                                                                                        \
    X (accent,            "Accent",              "Accents",  0xff9761dc)                \
                                                                                        \
    X (danger,            "Danger",              "States",   0xfff92672)                \
    X (error,             "Error",               "States",   0xfff92672)

/** The shared list and whatever this plugin adds to it. */
#define CELINE_THEME_ROLES(X)                                                           \
    CELINE_SHARED_THEME_ROLES(X)                                                        \
    CELINE_PLUGIN_THEME_ROLES(X)
