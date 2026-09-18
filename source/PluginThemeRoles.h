#pragma once

/*
    Céline's own themeable colours, added to the house list in `ui/ThemeRoles.h`.

    These are the schematic's vocabulary: what a wire is, what a selected part is, what
    a value nobody has typed yet looks like. They are named for the job they do rather
    than for the colour they are, so the drawing's conventions survive a change of
    palette -- which is the whole point of a theme being able to reach them.

    The light panel is here for a different reason, and it is the one worth explaining.
    The house design is two-tone -- dark chrome, near-white panels -- but the shared list
    carries only the dark half, because most plugins in the house have no light surface
    in them at all and a colour nobody can paint with is a row in the theme editor that
    does nothing. This window has two of them, the control strip and the panel beside the
    sheet, so the ground, the ink that goes on it and the row that sits on it are this
    plugin's three colours rather than the house's.

    Same shape as the shared list, and the same warning: the identifier is the key in a
    `.celthm` file, so **renaming one breaks every theme anybody has saved**. Adding,
    removing and regrouping are all free.

    Groups are rendered as headings in the theme editor in list order, so the entries
    under one have to stay together -- `tests/ThemeTests.cpp` fails if they do not.
*/
#define CELINE_PLUGIN_THEME_ROLES(X)                                                    \
    X (wire,         "Wire",                "Schematic",   0xff9761dc)                  \
    X (element,      "Part",                "Schematic",   0xffd9d9d9)                  \
    X (selected,     "Selected",            "Schematic",   0xfffd971f)                  \
    X (cursorMark,   "Ruler cursor",        "Schematic",   0xff9761dc)                  \
    X (pending,      "Edited",              "Schematic",   0xfffd971f)                  \
                                                                                        \
    X (captionName,  "Part name",           "Captions",    0xffe6db74)                  \
    X (captionValue, "Value",               "Captions",    0xffa6e22e)                  \
    X (captionUnset, "Value not set",       "Captions",    0xfff92672)                  \
                                                                                        \
    X (boxEnclose,   "Box: encloses",       "Selection",   0xff8F63D5)                  \
    X (boxCrossing,  "Box: crosses",        "Selection",   0xffa6e22e)                  \
                                                                                        \
    X (boxGrey,      "Grey",                "Group boxes", 0xff8b93a1)                  \
    X (boxBlue,      "Blue",                "Group boxes", 0xff5b9bd5)                  \
    X (boxGreen,     "Green",               "Group boxes", 0xff5fb87a)                  \
    X (boxAmber,     "Amber",               "Group boxes", 0xffd9a441)                  \
    X (boxRed,       "Red",                 "Group boxes", 0xffd06666)                  \
    X (boxViolet,    "Violet",              "Group boxes", 0xffa77fd0)                  \
                                                                                        \
    X (panel,        "Panel background",    "Parts panel", 0xfff9fbff)                  \
    X (textOnPanel,  "Text",                "Parts panel", 0xff28262e)                  \
    X (pill,         "Element background",  "Parts panel", 0xffdcdee4)                  \
                                                                                        \
    X (notice,       "Notice",              "Messages",    0xff888791)                  \
    X (warning,      "Warning",             "Messages",    0xffe6db74)                  \
                                                                                        \
    X (discard,      "Delete",              "Actions",     0xff6b2f2f)
