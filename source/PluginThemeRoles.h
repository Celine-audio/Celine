#pragma once

/* Céline's own themeable colours, added to the shared list in `CelineUI/ThemeRoles.h`.
   Named for their job, not their colour.

   The identifier is the key in a `.celthm` file: renaming one breaks saved themes.
   Entries under a group must stay together (`tests/ThemeTests.cpp` checks). */
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
    X (discard,      "Delete",              "Actions",     0xff6b2f2f)                  \
                                                                                        \
    X (traceFirst,   "Trace 1",             "Traces",      0xff2a78d6)                  \
    X (traceSecond,  "Trace 2",             "Traces",      0xffeb6834)                  \
    X (traceThird,   "Trace 3",             "Traces",      0xff1baf7a)                  \
    X (traceFourth,  "Trace 4",             "Traces",      0xffeda100)                  \
    X (traceFifth,   "Trace 5",             "Traces",      0xffe87ba4)                  \
    X (traceSixth,   "Trace 6",             "Traces",      0xff008300)                  \
    X (traceSeventh, "Trace 7",             "Traces",      0xff6250d6)                  \
    X (traceEighth,  "Trace 8",             "Traces",      0xffe34948)
