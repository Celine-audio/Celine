#pragma once

#include "PluginProcessor.h"
#include "PresetLibrary.h"
#include "Schematic/SchematicHistory.h"
#include "ui/PluginLookAndFeel.h"
#include "ui/ControlStrip.h"
#include "ui/EditorPanels.h"
#include "ui/SchematicCanvas.h"
#include <CelineUI/Theme.h>
#include "ui/ToolbarWidgets.h"

// Declared rather than included: the definition lives in a header that only the
// standalone build compiles, and a pointer to an incomplete type is all this
// needs. Keeping it a declaration is what lets one signature serve every format.
namespace juce
{
    class StandalonePluginHolder;
}

//==============================================================================
/**
    The circuit sandbox's window: a palette, a sheet to draw on, an inspector,
    and the knobs the drawn circuit turned out to have.

    The Rebuild button is the seam. Drawing is free and instant; turning the
    drawing into a running circuit costs a stamp, a factorisation and a bias
    solve, so it happens when asked rather than on every mouse move. Anything
    the drawing changes marks the button as pending, so it is obvious when what
    you hear has fallen behind what you see.
*/
class PluginEditor : public juce::AudioProcessorEditor,
                     private juce::Timer,
                     private juce::ChangeListener
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;

    /** The processor replaced the schematic under us -- a preset load, or the
        host restoring a session. */
    void schematicChangedExternally();

    /** Reads a .celsch and hands it to the processor. The one way a preset gets
        loaded -- the Load dialog and the preset menu both come through here, so
        a file that isn't ours is refused identically whichever route it took. */
    bool loadPresetFile (const juce::File& file);

    /** Merges a `.celsch` into the current drawing rather than replacing it,
        and selects what arrived so the next drag puts it where it belongs.

        The one route by which a file becomes part of the sheet, so a file that
        isn't ours is refused here identically however it was picked. Unlike a
        failed *load*, a failed import must leave the drawing untouched: there is
        no previous state to fall back to. */
    bool importSchematicFile (const juce::File& file);

    /** Loads one of the shipped example circuits by index. */
    void loadFactoryPreset (int index);

    /** The sheet, to look at rather than to drive.

        Const on purpose: everything that *changes* the drawing goes through the
        editor, which is what keeps undo, the Rebuild button and the preset's
        modified dot in step. This is for asking what is currently selected. */
    const Celine::SchematicCanvas& getCanvas() const noexcept { return canvas; }

    /** Which preset the toolbar is currently claiming, or empty for none. */
    juce::String getLoadedPresetName() const { return presetsButton.getPresetName(); }

    /** Whether that preset is a shipped example rather than a file. */
    bool isFactoryPresetLoaded() const { return presetsButton.isFactoryPreset(); }

private:
    // Construction, in three parts: the strip along the top, the four panels
    // filling the middle, and the band of fixed controls along the bottom.
    /** The strip along the top; takes its band off `area`. */
    void layOutToolbar (juce::Rectangle<int>& area);

    /** The control band along the bottom, then the sheet in what is left, and
        the floating panels over it. */
    void layOutPanels (juce::Rectangle<int> area);

    /** The palette, inspector and console, each sized to what it is showing and
        placed over the sheet. Separate from layOutPanels because two of them
        change size on their own -- a part selected, a build reported -- and the
        window has not moved. */
    void layOutFloatingPanels();

    void buildToolbar();
    void buildPanels();
    void buildBottomBand();

    void rebuildCircuit();
    void markPending();
    /** Knob to part: a control in the strip moved, so the parts on its shaft
        follow -- and the sheet and the inspector redraw to match. */
    void writeControlPositionToSchematic (const std::vector<int>& elementIds, float position);
    void showSettingsMenu();

    /** Credits and licensing, in a window rather than only in the repository.

        The AGPL asks that people be told what they are running and where its
        source is, and the icons' CC BY licence makes attribution a condition of
        use rather than a courtesy -- neither of which a README reaches, since
        the plugin is what gets copied around. This is that notice, travelling
        with the binary. */
    /** Puts the processor's idea of the loaded preset into the toolbar field. The
        processor is where it lives, so this is what a rebuilt editor catches up from
        as well as what a change writes through to. */
    /** The smallest the window is allowed to be. Named here rather than left in the
        constructor because resized() has to know it too -- it will not record a size
        below this as one worth reopening at.

        1024 x 667 is the chosen floor. The height is the shortest window whose
        palette shows every row without scrolling -- its 469 px list, plus the
        toolbar, the top ruler, the bottom band and a margin above and below -- and
        "The smallest window shows the whole palette" holds it to that. The width can
        still come out wider -- see narrowestWindow(), which is why it is a member
        rather than a constant -- if the panels ever need more than that to keep the
        console room between them. */
    int minimumWidth = 0;
    static constexpr int smallestWidth = 1024;
    static constexpr int minimumHeight = 667;

    /** The narrowest window in which the console fits between the palette and the
        inspector with every margin kept, so the palette can always run the full
        height of the sheet. Measured from the palette at its widest -- unfolded and
        scrolling -- because its scrollbar's width is the look and feel's to decide. */
    int narrowestWindow() const;

    void showPresetFromProcessor();

    void showAboutDialog();

    /** The theme moved. Re-reads everything that is *told* its colours rather than
        asked, then lets every child do the same. */
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    /** Every colour this window hands to a child rather than reading as it paints.
        Called from the constructor and again on every theme change -- a colour handed
        to setColour is a snapshot, and a snapshot does not follow a theme. */
    void applyColours();

    /** The rebuild button is the design's one stateful colour: the pending amber while
        the sheet is ahead of what you can hear, and the ordinary button fill once it is
        not. Gathered here because three places used to set it and a fourth -- a theme
        change -- has to agree with all of them. */
    void refreshRebuildButtonColour();

    /** The object that owns the audio device in a standalone build, or null in
        a host -- where the device is the host's business and there is no such
        thing. The one place that knows whether this build has one, so the
        conditional compilation appears once rather than at every use. */
    static juce::StandalonePluginHolder* standalonePluginHolder();
    void updateToolButtons();
    /** The Save and Load dialogs, which differ only in title, flags and what
        they do with the answer. */
    void browseForCircuit (bool saving);

    /** Picks an impulse response for the selected Output terminal. */
    void browseForCabinetFile();

    /** Picks an audio file for the Input terminal's player. Not the selected
        part's file -- the player is the plugin's, not any one terminal's, and
        nothing about it goes on the sheet. */
    void browseForInputFile();

    /** Says in the console how the last file load went, once per load. Polled
        from the timer, because the load finishes on a thread that cannot
        touch the editor. */
    void reportInputFileLoads();

    /** Copies one scope's picture out of the processor for drawing. */
    bool readScopeTrace (int elementId, Celine::ScopeReading& out) const;

    /** Picks a `.celsch` to drop onto the sheet alongside what is already
        drawn. */
    void browseForImport();

    /** The cabinet changed: reload it and say so if it could not be. Live --
        no rebuild, because nothing about a cabinet is in the matrix. */
    void applyCabinetChange();
    void setStatus (const juce::String& message, bool isError);
    void timerCallback() override;

    //==========================================================================
    // Undo. Snapshots of the drawing -- see SchematicHistory.
    void recordUndoState();
    void undo();
    void redo();
    void updateActionButtons();

    SchematicModel::SchematicHistory history;

    /** True once the current mouse gesture has had its "before" state recorded,
        so a drag across twenty grid squares is one undo step and not twenty.
        Cleared when the canvas says the gesture ended. */
    bool gestureRecorded = false;

    //==========================================================================
    // Presets: one remembered folder, and the menu that lists it.
    void showPresetsMenu();
    void choosePresetFolder();

    /** Standalone only: hands the window frame back to the OS.

        JUCE's standalone window draws its own title bar, which on macOS means
        no traffic lights and the wrong behaviour under Mission Control, and on
        Windows a frame that is not the system one.

        The "audio input is muted" bar above the editor used to be re-skinned
        here as well. It is not any more: Celine builds against its own fork of
        JUCE, where that bar paints from the look and feel in force
        (juce_StandaloneFilterWindow.h), so it arrives in the theme -- and every
        plugin in the house gets that rather than each covering it by hand. */
    void adoptNativeTitleBar();

    /** Asks for a preset folder, once, the first time the plugin is opened on
        this machine. Does nothing on every run after that, whatever the answer
        was. */
    void offerPresetFolderOnFirstRun();

    PluginProcessor& processorRef;

    /** Declared before every child component on purpose: members are destroyed
        in reverse order, so this outlives the things that draw with it. */
    PluginLookAndFeel lookAndFeel;

    // Every control in the window has a tooltip and none of them can show one without
    // this: JUCE needs a window to draw them in, and there is no default. Without it
    // the tooltips this plugin already set were simply never seen.
    juce::TooltipWindow tooltips { this, 600 };

    //==========================================================================
    /** Only the canvas grows. The panels float over it at the size of what they
        show, and the control strip is a row of knobs -- stretching any of them buys
        nothing, so the whole of a resize goes to the sheet. */
    static constexpr int paletteWidth = Celine::Theme::paletteWidth;

    // Toolbar. What a click on the sheet does -- the three are mutually
    // exclusive and the active one is lit, because with several modes it has to
    // be obvious which is in force before you click.
    /** Rebuild is the only word left in the row. Everything else is an icon at
        the design's 33x33, because a toolbar of mixed text and icons cannot be
        put on one grid -- and the grid is what makes it read as a row of
        equals rather than a queue. */
    juce::TextButton rebuildButton { "Rebuild" };

    /** The two tools, which are mutually exclusive and show it in teal. */
    std::unique_ptr<Celine::IconButton> selectToolButton, deleteToolButton;

    /** The file pair. */
    std::unique_ptr<Celine::IconButton> saveButton, loadButton;

    /** Import: a second sheet dropped onto this one rather than replacing it.

        Next to the preset field because it belongs to the same group -- Save,
        Load and Presets are all "which drawing am I working on", and this is the
        one that answers "both of them". */
    std::unique_ptr<Celine::IconButton> importButton;

    /** Where resized() put the undo/redo housing, so paint() can draw it behind
        the two frameless buttons that sit in it. */
    juce::Rectangle<int> undoRedoHousing;

    /** The actions that operate on the selection, as icons. Built in the
        constructor because each needs its artwork. Kept in one array so the
        toolbar lays them out as a group and nothing has to remember the order
        twice. */
    enum class Action { Mirror, Flip, Copy, Rotate, Undo, Redo, count };
    std::array<std::unique_ptr<Celine::IconButton>,
               static_cast<size_t> (Action::count)> actionButtons;

    Celine::IconButton& actionButton (Action a)
    {
        return *actionButtons[static_cast<size_t> (a)];
    }

    /** Says which preset is loaded, not just that a menu exists. */
    Celine::PresetButton presetsButton;

    /** Built in the constructor, since it needs the icon artwork. */
    std::unique_ptr<Celine::IconButton> settingsButton;

    /** The wordmark in the top-right corner. A DrawableComposite rather than an
        image so it stays sharp at any window size, and not a button because it
        does nothing -- it is the only purely decorative thing in the window. */
    std::unique_ptr<juce::Drawable> logo;

    /** Worked out in resized(), used in paint(). */
    juce::Rectangle<int> logoBounds;

    /** The whole bottom band, one of the two light surfaces. ControlStrip
        covers only its middle -- INPUT sits left, VOLUME and CHANNELS right, and
        those are the editor's own children -- so the editor paints the band and
        the strip paints its part of it. */
    juce::Rectangle<int> controlBandBounds;

    /** Kept alive for the duration of an async file dialog. */
    std::unique_ptr<juce::FileChooser> fileChooser;

    /** The input file's dialog, kept apart from the one above. Replacing a
        FileChooser destroys the dialog it was running, so sharing one meant
        that picking a take to play closed a cabinet dialog that was still open,
        and the other way round. */
    std::unique_ptr<juce::FileChooser> inputFileChooser;

    /** Where the last save or load went, so the next dialog opens there. */
    juce::File lastCircuitFile;

    /** And where the last impulse response came from. Its own, not shared with
        the above: cabinets and sheets live in different folders, and a chooser
        that opens on the other one is a chooser you navigate out of every
        time. */
    juce::File lastCabinetFile;

    /** How many file loads the player had finished when this editor last said
        anything about one. Starts at the player's count, so reopening the
        window does not announce a file loaded an hour ago. */
    int reportedInputFileLoads = 0;

    /** The preset being loaded, handed forward to the callback that arrives
        afterwards. `onSchematicReplaced` is bounced through the message thread,
        so schematicChangedExternally() runs after loadPresetFile() returns --
        and since it cannot tell a preset load from a host restoring a session,
        without this it would wipe the name a beat later. */
    juce::File presetBeingLoaded;

    /** Keeps the preset button saying what is actually loaded. Called from
        every route that changes which preset the sheet came from. */
    void setLoadedPreset (const juce::File& file);
    void setLoadedFactoryPreset (const juce::String& name);
    void markPresetModified();

    /** The user's preset folder, remembered between runs. Owned by the editor
        rather than the processor because the only things that need it are a
        menu and a dialog, and neither exists without a window. */
    PresetLibrary presets;

    //==========================================================================
    // Main panels. The sheet fills the middle of the window and the other three
    // float over it -- see layOutFloatingPanels.
    Celine::ElementPalette palette;
    Celine::SchematicCanvas canvas;
    Celine::ElementInspector inspector;

    /** Everything the last build had to say, one message per row. */
    Celine::MessageConsole console;

    //==========================================================================
    /** The knobs the circuit turned out to have. Owns its own widgets and the
        two-way traffic between them and the drawing -- see ControlStrip. */
    Celine::ControlStrip controlStrip;

    //==========================================================================
    // Always present, not part of any circuit
    juce::Slider inputSlider, outputSlider;
    /** The plugin's own gain either end of the band, either side of whatever
        knobs the circuit itself has. The captions match the parameter ids --
        and the ids are what they are for good, since renaming one breaks every
        saved automation lane that points at it. */
    juce::Label inputLabel { {}, "INPUT" }, outputLabel { {}, "OUTPUT" };
    std::unique_ptr<Celine::PowerButton> bypassButton;
    juce::ComboBox channelModeBox;
    juce::Label channelModeLabel { {}, "CHANNELS" };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> inputAttachment, outputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> channelModeAttachment;

    bool pendingRebuild = false;

    /** Whether this window has already got as far as considering the first-run
        preset question. One window asks at most once, however long it lives. */
    bool presetFolderChecked = false;

    bool nativeTitleBarChecked = false;

    /** The standalone window's own Options button, which is how JUCE reaches
        the audio device settings. Going native zero-sizes it -- see
        adoptNativeTitleBar -- so we hold on to it and offer it from the
        Settings menu instead of losing the only route to choosing an output. */

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
