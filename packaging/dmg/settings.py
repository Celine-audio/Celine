# The macOS disk image: one row per format, the plugin on the left and a shortcut to the
# folder it installs into on the right, joined by a wire drawn on the background.
#
#     dmgbuild -s packaging/dmg/settings.py -D artefacts=<build>/Celine_artefacts/Release \
#              "Celine" Celine-<version>-macOS.dmg
#
# run from the repository root. dmgbuild writes Finder's layout file itself rather than
# scripting Finder, so this works the same on a headless CI runner as on a desk.
#
# The background (packaging/dmg/background.png, and @2x for Retina, which dmgbuild joins
# into one image) is drawn by packaging/make-artwork.swift. Its rows must match `rows`
# below: change one, change the other and run that script again.

import os

# Passed with -D: the build's artefacts directory, holding Standalone/, AU/, VST3/ and so on.
artefacts = defines.get("artefacts", "Builds/Celine_artefacts/Release")  # noqa: F821
product = defines.get("product", "Celine")  # noqa: F821

here = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else "packaging/dmg"

# Format, bundle and the folder it installs into, top to bottom. The plugin folders are the
# system-wide ones, so a plugin dragged there is seen by every user and every host; Finder
# asks for an administrator's password to write there.
rows = [
    ("Standalone", product + ".app", "Applications", "/Applications"),
    ("AU", product + ".component", "Components", "/Library/Audio/Plug-Ins/Components"),
    ("VST3", product + ".vst3", "VST3", "/Library/Audio/Plug-Ins/VST3"),
    ("CLAP", product + ".clap", "CLAP", "/Library/Audio/Plug-Ins/CLAP"),
    ("LV2", product + ".lv2", "LV2", "/Library/Audio/Plug-Ins/LV2"),
]

# In points, matching make-artwork.swift: icons on the left column, folders on the right,
# one row every 94 points from the first.
left_x, right_x = 230, 530
first_row_y, row_pitch = 144, 94

files = []
symlinks = {}
icon_locations = {}

for i, (folder, bundle, link, target) in enumerate(rows):
    y = first_row_y + i * row_pitch
    files.append(os.path.join(artefacts, folder, bundle))
    symlinks[link] = target
    icon_locations[bundle] = (left_x, y)
    icon_locations[link] = (right_x, y)

format = "UDZO"
filesystem = "HFS+"

icon = os.path.join(here, "volume.icns")
background = os.path.join(here, "background.png")

# Finder takes this as the whole window frame, title bar included, with y measured up from
# the bottom of the screen (measured on macOS 27: a 760 x 660 frame opened as exactly that).
#
# The show_* settings below are the DMG's say, and Finder no longer listens to all of it:
# the tab, path and status bars are the user's own Finder settings now and show in every
# window, this one included. With all three on they and the title took 124 points of a
# 740-point window (a CI-built DMG, opened on macOS 27), and cut the last row off. So the
# layout is compact enough to fit under every bar: the rows end at 572, the frame is 712,
# and with every bar on the last tile still has 16 points under it. With none, the window
# shows plain grid below the rows, and the picture (700 high) runs past what it shows.
#
# Finder cannot centre a window or keep it off the Dock and the menu bar by itself. 100
# points up clears a usual Dock, and the top (812) stays under the menu bar on a 900-point
# laptop screen, the smallest current one.
window_rect = ((160, 100), (760, 712))
default_view = "icon-view"
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False

arrange_by = None
icon_size = 56
text_size = 12
label_pos = "bottom"
show_icon_preview = False
