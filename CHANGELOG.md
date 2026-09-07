# Changelog

All notable changes to Céline are recorded here.

Follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)

## [Unreleased]

### Added

- **A realtime-safety and host-stress test suite**, the same four files in every plugin.
  `RealtimeSafetyTests` replaces the allocator and fails if `processBlock` allocates —
  and proves the guard is armed first, because a check that silently stops working is
  worse than no check. `HostStressTests` does what hosts really do: block sizes from one
  sample to eight thousand including sizes never prepared for, every sample rate from
  44.1k to 192k, buffers of NaN and infinity and denormals, `processBlock` before
  `prepareToPlay`, every parameter swept while audio runs, state saved and restored
  mid-playback, the editor opened and closed under load. `ConcurrencyStressTests` runs a
  real audio thread against the message thread doing all of that at once.
  `PerformanceTests` reports the cost of a block as a share of the block's own duration,
  and fails if any single block takes longer than the audio it is for.
- Each plugin carries a `Busy` fixture saying what its own busiest state is which is what stops the rest from passing
  against a plugin that is doing nothing.
- Closing the theme editor with colours you have not saved now asks, offering **Save**,
  **Discard** or **Cancel**. Every way out goes through it — the Close button, the escape
  key and the title bar's own close button.
- `tests/ThemeReachTests.cpp`, which renders the whole editor, moves every colour the
  theme has, renders it again, and fails if anything the design ships is still on screen.
  It found four real bugs the day it was first run across all four plugins.
- **A theme is now this plugin's own**, in `<name>.celthm` under the company folder
  rather than one file shared by the house. Every instance of it on the machine wears
  the same colours whatever host or format it is loaded as, and an existing shared theme
  is inherited on first run so nothing is lost by the split. Themes stay cross-
  compatible: one exported from another Céline plugin still loads, and the colours this
  one does not have are simply skipped.
- **AAX**, on macOS and Windows. Pro Tools will not list it until it carries a PACE
  signature applied with Avid's wraptool; an unwrapped build loads in a Pro Tools
  Developer build and nowhere else.
- **A theming engine.** Every colour the interface draws with is editable at runtime,
  from **Theme…** in the settings menu, and can be written to and read from a `.celthm`
  file to be kept or shared. Changes show at once — the palette is what everything draws
  from, so there is no Apply to forget.
- The theme file is shared by every Céline plugin: one `theme.celthm` under the company
  folder, so theming one of them themes all of them. A key a build does not know is
  ignored and a key it knows but the file omits keeps its shipped value, which is what
  lets one file serve four plugins with different palettes.
- The schematic's own colours are in the theme, not just the chrome — wires, parts, the
  selection, the three caption meanings, the two selection-box rules and the six group
  box colours. A palette that could not reach the drawing could not re-skin this plugin.
- Tooltips on the controls along the bottom band and in the toolbar.

### Changed

- Menus and tooltips carry a faint rule, the same one the callout bubble wears. On macOS
  the window's own shadow gave them an edge for free; on Windows there is no shadow to
  borrow one from, so they ran into whatever was behind them. Drawn rather than
  inherited, so both platforms show the same thing.
- Tooltips cast a shadow, sitting inside a margin reserved for it. A tooltip is the one
  thing genuinely floating above the window, and a dark panel on a dark window with
  nothing lifting it off is just a slightly different dark. The shadow is drawn rather
  than asked for: JUCE's own shadower builds a rectangle, which behind a rounded panel is
  a dark wedge in each corner, so it stays declined. Menus keep the rule and no shadow --
  a menu is a desktop window sized to its items, so the only way to make a margin for one
  is to grow the window, which moves the menu off the button it was opened from and
  leaves the margin showing as a black box wherever the window turns out to have no
  per-pixel alpha. The tooltip can have one because it is a child of the editor rather
  than a window of its own.
- The look and feel is split: `ui/LookAndFeelBase` carries everything the four plugins
  draw the same way, and `ui/PluginLookAndFeel` is a subclass for what this one does
  differently. Fifteen files under `source/ui/` are now byte-identical across all four,
  which is what makes the shared kit a move rather than a merge — see `CELINEUI.md`.
- Every format now declares what this is, rather than leaving each host to file it under
  nothing: **Fx|Distortion** to VST3, **lv2:DistortionPlugin** to LV2, **distortion** to
  CLAP, and **Harmonic** to AAX. AU is unchanged — it has only a component type, `aufx`,
  and no genre to state.
- The CLAP build also declares that it handles mono as well as stereo, which it always
  did.
- **Button backgrounds and text fields are separate colours in the theme.** They shipped
  as one — every button wore the same slate as every panel — so a theme could not lift
  the controls off the surfaces they sit on. Two new roles, **Button** and **Text
  field**, ship at exactly the values they replace, so nothing looks different until
  somebody moves them.
- **Menus, tooltips and buttons are drawn the way the other Céline plugins draw them.**
  All three were still JUCE's defaults: square menus with a system border, a tooltip
  that read as an operating-system window sitting on top of the plugin rather than as
  part of it, and buttons with none of the design's rounding. The callout bubble and
  the field outlines came across with them.
- The About window is now the one the other Céline plugins use, and wears the DESIGNER
  wordmark. What it says about this plugin — the tagline, the wordmark, and the circuit
  model credits it owes on its own account — comes from `source/ProductInfo.h`.
- The interface is in namespace `Celine` under `source/ui/`, as the other plugins are,
  and `CelineLookAndFeel` is now `PluginLookAndFeel`. Several of these files are now
  byte-identical across all four, which is what a shared kit has to mean before it can
  be extracted into one.

### Fixed

- **The cabinet no longer loads from the wrong thread.** `juce::dsp::Convolution`
  documents that its methods may not be interleaved: a load has to be synchronised with
  `process()`, "which in practice means making the load() call from the audio thread".
  Loading from the message thread while audio ran — which is what every schematic edit
  did — is a data race, and its symptom is a heap-use-after-free rather than anything
  you would hear coming. The cabinet now runs on the house convolver, whose swap is
  built the other way round: the message thread prepares a filter and parks it, and the
  audio thread picks it up at a frame boundary under a try-lock it never waits on.
  ThreadSanitizer reports nothing where it reported a race before, and the sound is
  unchanged — measured against the previous engine at 4.7e-10, which is arithmetic noise.
- **A cabinet is audible in the next block instead of a second and a half later.** The
  old engine handed the file to a background thread and the filter arrived whenever that
  thread got to it: measured at about 275 blocks. The response is built on the thread
  that asked for it now.
- **A cabinet is now the same length at every sample rate.**
- **Clicking into a value box no longer draws a border round it.** The slider's text box
  asked for one in the armed colour while it was being edited -- the last rule left
  anywhere in the window, and one that appeared on a click, which is exactly what made it
  read as a system control dropped into the design.
- The digits you type into a value box are the theme's ink. JUCE fills
  `textWhenEditingColourId` from its own colour scheme rather than leaving it unset, so
  the text being edited was never taking its colour from the theme.
- **Discarding a theme now puts the colours back.** It marked the change abandoned and
  left it on screen, so "discard" only meant "do not write the file" -- the window behind
  it kept the colours you had just rejected until something else reloaded the theme.
- **The theme window no longer opens behind the plugin.** Building it by hand to
  intercept every way of closing it lost the two things `DialogWindow::LaunchOptions`
  does for you: it is on top when the host keeps its own windows on top -- Ableton does,
  and the window was unreachable without closing the plugin -- and it is told the scale
  the editor is being shown at.
- **The yellow ring around whatever you were editing is gone.** JUCE draws a focus
  outline as a separate desktop window, and its default is a rounded rectangle at a fixed
  radius of three — so on a field rounded to the house radius it traced a shape the
  control does not have, sitting slightly off its corners. It also lived only as long as
  that window did, which is why it appeared on one launch and not the next. Nothing here
  needs it: a field being edited says so with its caret and its selection.
- **The theme editor's own Close button follows the accent it is showing you.** Its
  colours were set once when the window opened, so picking a new accent recoloured every
  other control in the plugin and left the button next to the swatch on the old one. The
  window's title, subtitle and status line had the same fault.
- **The toolbar's mark did not follow the theme.** The logo and the wordmark were tinted
  once when the window opened, and tinting is destructive — so they stayed on whatever
  colour the theme happened to be at that moment.
- **The About window did not follow the theme at all.** It is a window of its own, so the
  editor's `sendLookAndFeelChange` never reached it; it now listens to the palette
  directly, and its marks are re-read from the binary rather than re-tinted.
- **Group headings no longer escape the colour list.** They are painted by the panel in
  the scrolled list's coordinates, and nothing clipped them — so a heading scrolled past
  the top carried on being drawn above the list, over the subtitle and the footer. It
  showed up as headings appearing in the middle of the window whenever something made
  the panel repaint underneath the colour picker.
- The colour picker no longer paints a square panel inside a rounded bubble. It filled
  its own background, which met the bubble's rounded corners and lost the argument.
- **Theming one instance now reaches the others.** Each plugin format is a separately
  loaded module with its own copy of everything static, so the VST3 and the AU open in
  one session were two palettes that never met — theming one left the other on the old
  colours until it was reloaded. A window reads the saved theme when it opens, which is
  the moment it can matter; nothing watches the disk in the background. Colours you are
  in the middle of choosing are never overwritten by what another instance saved.
- **The placement ghost's colour was captured before the program started.** It was a
  namespace-scope `const juce::Colour` read from the theme, which made it two bugs at
  once: a colour no theme change could move, and — because reading a colour builds the
  palette — a palette built during static initialisation, before there was a message
  loop for it to use. It is a function now, like every other colour in the window.
- Building a palette no longer schedules a save of the file it has just read. Reading
  any colour builds it, and the first read can come from a static initialiser — before
  there is a message loop for the save to wait on, which JUCE asserts about.
- **A theme you pick is kept — when you press Save.** It used to be live until you
  closed the plugin and then gone, because nothing wrote it. There is now a **Save**
  button in the theme editor, lit only while there is something to keep, and the status
  line says whether there is. Editing itself touches nothing: a colour picker sends a
  change per mouse move, and a preference is not worth a file per mouse move.
- Text fields no longer draw a ring when you click into them. The caret already says
  where the typing goes, and it was the one edge in the window that arrived on a click.
- **The loaded preset survives closing the window.** It lived only on the editor, which
  is rebuilt every time the window closes, so reopening showed an empty preset field
  above a circuit that was still loaded. It is on the processor now, and in the session
  state with it.
- **A folder chosen in one instance reaches the others.** The settings file was held
  open for the life of each instance, so one loaded earlier answered from the copy it
  read then — and wrote that copy back over the new one when it closed.
- The window no longer records a size it was resized to on the way out. A host is free
  to collapse an editor it is putting away, and recording that overwrote the size you
  had actually chosen.
- The three tooltips the plugin already set are now visible. There was no
  `juce::TooltipWindow` anywhere, and JUCE has no default, so nothing could draw them.
- No colour is written as a hex literal at a call site any more; the six group box
  colours were the last of them.
- **Square corners on rounded controls.** A text field filled its whole rectangle and
  drew a rounded rule inside it, so the four corners outside the rounding kept the fill
  — a rounded box with square spikes at its corners, on the inspector's fields and on
  the one that floats over the sheet. Menus had the same fault from the other end: their
  window was opaque, so the corners a rounded panel did not paint came out as squares of
  whatever was behind.
- **Nothing in the window draws a rule around itself any more.** The toolbar buttons,
  the preset field, the housing behind undo and redo, the dropdowns and every text field
  each drew their own border; the other Céline plugins draw none of them, and a border
  on top of a fill that already separates a control from its ground is a second edge
  doing the first one's job. A focused field still gets its ring — that one is saying
  something.
- The dropdowns are drawn by the house's `drawComboBox` rather than Céline's own, which
  outlined them at one and a half times the normal border weight.
- The toolbar buttons no longer draw a rule around themselves. They are filled with a
  colour that already separates them from the toolbar, so the outline drew a second edge
  where one was doing the job — and it is what made them look unlike the same buttons in
  the other Céline plugins.
- Engaging bypass no longer reshapes its button. It painted its own inset, radius and
  border rather than letting the base class draw it, so the two states covered slightly
  different areas.
- A theme change re-reads the look and feel before the window asks it anything. The
  rebuild button's idle fill is the look and feel's own button colour, so asking first
  answered with the colour the theme was replacing — which left that button wearing the
  old palette until something else repainted it.

- The window now reopens at the size it was left at. `setResizeLimits` constrains the bounds it finds.
The stored size was read after that call, so it returned the minimum that had just been written, and every instance opened at its smallest.
- The About window's format marks are placed after their artwork is loaded rather than before.

## [1.0.0] — 2026-09-01

First release.

[Unreleased]: https://github.com/Celine-audio/Celine/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/Celine-audio/Celine/releases/tag/v1.0.0
