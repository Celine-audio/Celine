#pragma once

#include "PluginProcessor.h"
#include "PresetLibrary.h"
#include "Schematic/SchematicHistory.h"
#include "UI/CelineLookAndFeel.h"
#include "UI/ControlStrip.h"
#include "UI/EditorPanels.h"
#include "UI/SchematicCanvas.h"
#include "UI/Theme.h"
#include "UI/ToolbarWidgets.h"

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
                     private juce::Timer
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
    const SchematicUI::SchematicCanvas& getCanvas() const noexcept { return canvas; }

    /** Which preset the toolbar is currently claiming, or empty for none. */
    juce::String getLoadedPresetName() const { return presetsButton.getPresetName(); }

    /** Whether that preset is a shipped example rather than a file. */
    bool isFactoryPresetLoaded() const { return presetsButton.isFactoryPreset(); }

private:
    // Construction, in three parts: the strip along the top, the four panels
    // filling the middle, and the band of fixed controls along the bottom.
    /** The strip along the top; takes its band off `area`. */
    void layOutToolbar (juce::Rectangle<int>& area);

    /** The control band along the bottom, then the palette, inspector, console
        and sheet in what is left. */
    void layOutPanels (juce::Rectangle<int> area);

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
    void showAboutDialog();

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

    /** Copies one scope's picture out of the processor for drawing. */
    bool readScopeTrace (int elementId, SchematicUI::ScopeReading& out) const;

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
        Windows a frame that is not the system one. */
    void adoptNativeTitleBar();

    /** Standalone only: re-skins the "audio input is muted" bar JUCE puts above
        the editor.

        The bar's paint() hardcodes its colour and the class is private, so there
        is nothing to set and no LookAndFeel hook to answer. Rather than fork
        JUCE, an opaque child is laid over it and JUCE's own Settings button is
        brought to the front and recoloured, so the one thing on the bar that
        does something still works. */
    void styleStandaloneNotification();

    /** The skin, kept alive as long as the editor is. Null in a plugin, where
        there is no such bar. */
    std::unique_ptr<juce::Component> notificationSkin;

    /** Asks for a preset folder, once, the first time the plugin is opened on
        this machine. Does nothing on every run after that, whatever the answer
        was. */
    void offerPresetFolderOnFirstRun();

    PluginProcessor& processorRef;

    /** Declared before every child component on purpose: members are destroyed
        in reverse order, so this outlives the things that draw with it. */
    SchematicUI::CelineLookAndFeel lookAndFeel;

    //==========================================================================
    /** Only the canvas grows. The palette and the inspector are lists of fixed-
        width things, and the control strip is a row of knobs -- stretching any of
        them buys nothing, so the whole of a resize goes to the sheet. */
    static constexpr int paletteWidth = SchematicUI::Theme::paletteWidth;
    static constexpr int inspectorWidth = SchematicUI::Theme::inspectorWidth;

    // Toolbar. What a click on the sheet does -- the three are mutually
    // exclusive and the active one is lit, because with several modes it has to
    // be obvious which is in force before you click.
    /** Rebuild is the only word left in the row. Everything else is an icon at
        the design's 33x33, because a toolbar of mixed text and icons cannot be
        put on one grid -- and the grid is what makes it read as a row of
        equals rather than a queue. */
    juce::TextButton rebuildButton { "Rebuild" };

    /** The two tools, which are mutually exclusive and show it in teal. */
    std::unique_ptr<SchematicUI::IconButton> selectToolButton, deleteToolButton;

    /** The file pair. */
    std::unique_ptr<SchematicUI::IconButton> saveButton, loadButton;

    /** Import: a second sheet dropped onto this one rather than replacing it.

        Next to the preset field because it belongs to the same group -- Save,
        Load and Presets are all "which drawing am I working on", and this is the
        one that answers "both of them". */
    std::unique_ptr<SchematicUI::IconButton> importButton;

    /** Where resized() put the undo/redo housing, so paint() can draw it behind
        the two frameless buttons that sit in it. */
    juce::Rectangle<int> undoRedoHousing;

    /** The actions that operate on the selection, as icons. Built in the
        constructor because each needs its artwork. Kept in one array so the
        toolbar lays them out as a group and nothing has to remember the order
        twice. */
    enum class Action { Mirror, Flip, Copy, Rotate, Undo, Redo, count };
    std::array<std::unique_ptr<SchematicUI::IconButton>,
               static_cast<size_t> (Action::count)> actionButtons;

    SchematicUI::IconButton& actionButton (Action a)
    {
        return *actionButtons[static_cast<size_t> (a)];
    }

    /** Says which preset is loaded, not just that a menu exists. */
    SchematicUI::PresetButton presetsButton;

    /** Built in the constructor, since it needs the icon artwork. */
    std::unique_ptr<SchematicUI::IconButton> settingsButton;

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

    /** Where the last save or load went, so the next dialog opens there. */
    juce::File lastCircuitFile;

    /** And where the last impulse response came from. Its own, not shared with
        the above: cabinets and sheets live in different folders, and a chooser
        that opens on the other one is a chooser you navigate out of every
        time. */
    juce::File lastCabinetFile;

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
    // Main panels
    SchematicUI::ElementPalette palette;
    SchematicUI::SchematicCanvas canvas;
    SchematicUI::ElementInspector inspector;

    /** Everything the last build had to say, one message per row. */
    SchematicUI::MessageConsole console;

    /** How much of the right-hand panel the console gets. The inspector is a
        fixed list of fields and needs a known amount; the console takes what is
        left, which on a tall window is most of it. */
    static constexpr int inspectorHeight = 456;

    //==========================================================================
    /** The knobs the circuit turned out to have. Owns its own widgets and the
        two-way traffic between them and the drawing -- see ControlStrip. */
    SchematicUI::ControlStrip controlStrip;

    //==========================================================================
    // Always present, not part of any circuit
    juce::Slider inputSlider, outputSlider;
    /** The plugin's own gain either end of the band, either side of whatever
        knobs the circuit itself has. The captions match the parameter ids --
        and the ids are what they are for good, since renaming one breaks every
        saved automation lane that points at it. */
    juce::Label inputLabel { {}, "INPUT" }, outputLabel { {}, "OUTPUT" };
    std::unique_ptr<SchematicUI::PowerButton> bypassButton;
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
