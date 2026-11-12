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
#include "ui/PresetLibraryPanel.h"

// Forward-declared: only the standalone build has the definition.
namespace juce
{
    class StandalonePluginHolder;
}

//==============================================================================
/** The plugin window: palette, sheet, inspector, console and the circuit's controls.

    Drawing changes are instant; Rebuild turns the drawing into a running circuit, and
    lights up while the sound is behind the drawing. */
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

    /** The processor replaced the schematic (preset load or session restore). */
    void schematicChangedExternally();

    /** Loads a .celsch through the processor. Every preset load goes through here. */
    bool loadPresetFile (const juce::File& file);

    /** Merges a .celsch into the current drawing and selects what was added. On failure
        the drawing is left untouched. */
    bool importSchematicFile (const juce::File& file);

    /** Loads a shipped example by index. */
    void loadFactoryPreset (int index);

    /** Read-only access to the sheet. Changes go through the editor, which keeps undo,
        Rebuild and the modified dot in step. */
    const Celine::SchematicCanvas& getCanvas() const noexcept { return canvas; }

    /** The preset the toolbar shows, or empty. */
    juce::String getLoadedPresetName() const { return presetsButton.getPresetName(); }

    /** Whether that preset is a shipped example. */
    bool isFactoryPresetLoaded() const { return presetsButton.isFactoryPreset(); }

    /** The dBu display setting, stored per machine. */
    static bool levelsShownInDbu();
    static void setLevelsShownInDbu (bool inDbu);

    /** Whether the inspector shows the shortcut list when nothing is selected. Per machine,
        on by default. */
    static bool inspectorShortcutsShown();
    static void setInspectorShortcutsShown (bool shown);

private:
    /** The top toolbar; takes its height off `area`. */
    void layOutToolbar (juce::Rectangle<int>& area);

    /** The bottom band, then the sheet, then the floating panels. */
    void layOutPanels (juce::Rectangle<int> area);

    /** Places the palette, inspector and console over the sheet, each sized to its
        contents. Called again whenever one of them changes size. */
    void layOutFloatingPanels();

    /** The bottom band's bars, aligned under the palette and inspector. Runs after
        layOutFloatingPanels. */
    void layOutBand();

    void buildToolbar();
    void buildPanels();
    void buildBottomBand();

    void rebuildCircuit();
    void markPending();
    /** A strip control moved: update the parts it works, and redraw. */
    void writeControlPositionToSchematic (const std::vector<int>& elementIds, float position);
    void showSettingsMenu();

    /** Sets what the INPUT and OUTPUT knobs display: gain in dB, or the equivalent dBu
        level when the dBu setting is on (see PluginProcessor::inputGainToDbu). */
    void applyLevelReadouts();

    /** The smallest window size. resized() won't store a size below it. 1024 x 689 fits
        the whole palette without scrolling; the width may grow, see narrowestWindow(). */
    int minimumWidth = 0;
    static constexpr int smallestWidth = 1024;
    static constexpr int minimumHeight = 689;

    /** The narrowest window that still fits the console between palette and inspector. */
    int narrowestWindow() const;

    void showPresetFromProcessor();

    void showAboutDialog();

    /** The theme changed: re-apply colours here and in every child. */
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    /** Colours handed to children with setColour. Called again on every theme change. */
    void applyColours();

    /** Rebuild's colour: amber while a rebuild is pending, the normal fill otherwise. */
    void refreshRebuildButtonColour();

    /** The standalone window's audio holder, or null in a plugin host. */
    static juce::StandalonePluginHolder* standalonePluginHolder();
    void updateToolButtons();
    /** The Save and Load dialogs. */
    void browseForCircuit (bool saving);

    /** Picks an impulse response for the selected Output terminal. */
    void browseForCabinetFile();

    /** Picks an audio file for the input file player. */
    void browseForInputFile();

    /** Reports the last file load in the console, once. Polled from the timer. */
    void reportInputFileLoads();

    /** Copies one scope's trace out of the processor. */
    bool readScopeTrace (int elementId, Celine::ScopeReading& out) const;

    /** Picks a .celsch to add to the current sheet. */
    void browseForImport();

    /** Reloads the cabinet and reports a failure. Live: no rebuild needed. */
    void applyCabinetChange();
    void setStatus (const juce::String& message, bool isError);
    void timerCallback() override;

    // ==========================================================================
    // Undo (see SchematicHistory).
    void recordUndoState();
    void undo();
    void redo();
    void updateActionButtons();

    SchematicModel::SchematicHistory history;

    /** True once the current gesture's starting state is recorded, so a drag is one undo
        step. */
    bool gestureRecorded = false;

    // ==========================================================================
    // Presets.

    /** Opens or closes the library under the preset field. */
    void showPresetLibrary();
    void closePresetLibrary();
    bool isPresetLibraryShowing() const { return presetLibrary.isVisible(); }

    /** Under the preset field, as tall as its rows or the room available. */
    void placePresetLibrary();

    /** Gives the library the folder, the factory examples and the loaded preset. */
    void refreshPresetLibrary();

    /** Clears the sheet ("New empty sheet"). */
    void startNewSheet();

    void choosePresetFolder();

    /** Standalone only: uses the OS window frame instead of JUCE's. */
    void adoptNativeTitleBar();

    /** Asks for a preset folder the first time the plugin opens on this machine. */
    void offerPresetFolderOnFirstRun();

    // ==========================================================================
    // Update checks (see UpdateCheck.h).

    /** Asks once per machine whether to check for updates. Closing the question means no. */
    void offerUpdateChecksOnFirstRun();

    /** Starts this launch's check if enabled. */
    void startUpdateCheckIfEnabled();

    /** "A new update is available": Download, Remind me later, or turn checks off. */
    void announceUpdate (const juce::String& version);

    /** Shows a themed question in its own window. One at a time. */
    juce::DialogWindow* launchPrompt (const juce::String& title, std::unique_ptr<juce::Component> content);

    PluginProcessor& processorRef;

    /** Declared before the child components so it outlives them. */
    PluginLookAndFeel lookAndFeel;

    // Needed for any tooltip to show.
    juce::TooltipWindow tooltips { this, 600 };

    // ==========================================================================
    // Resizing gives all the extra space to the sheet.

    // Toolbar.
    /** The only text button in the toolbar. */
    juce::TextButton rebuildButton { "Rebuild" };

    /** The two tools; mutually exclusive. */
    std::unique_ptr<Celine::IconButton> selectToolButton, deleteToolButton;

    /** Save and Load. */
    std::unique_ptr<Celine::IconButton> saveButton, loadButton;

    /** Adds another sheet to this one. */
    std::unique_ptr<Celine::IconButton> importButton;

    /** The preset folder icon at the right end of the preset field. Always clickable. */
    std::unique_ptr<Celine::IconButton> presetFolderButton;

    /** The undo/redo housing, drawn in paint() behind the two buttons. */
    juce::Rectangle<int> undoRedoHousing;

    /** The selection actions, laid out as a group. */
    enum class Action { Mirror, Flip, Copy, Rotate, Undo, Redo, count };
    std::array<std::unique_ptr<Celine::IconButton>,
               static_cast<size_t> (Action::count)> actionButtons;

    Celine::IconButton& actionButton (Action a)
    {
        return *actionButtons[static_cast<size_t> (a)];
    }

    /** The preset field. */
    Celine::PresetButton presetsButton;

    /** The preset library. Kept alive so opened folders stay open. */
    Celine::PresetLibraryPanel presetLibrary;

    /** Covers the window behind the open library; a click on it closes the library. */
    struct LibraryDismisser : juce::Component
    {
        std::function<void()> onClick;
        void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    };

    LibraryDismisser libraryDismisser;

    /** Built in the constructor, since it needs the icon artwork. */
    std::unique_ptr<Celine::IconButton> settingsButton;

    /** The wordmark in the top-right corner. Decorative. */
    std::unique_ptr<juce::Drawable> logo;

    /** Set in resized(), used in paint(). */
    juce::Rectangle<int> logoBounds;

    /** The INPUT and OUTPUT bars at the ends of the bottom band: square at the window
        edge, rounded on the inside. The ControlStrip sits between them. */
    class BandBar : public juce::Component
    {
       public:
        explicit BandBar (bool roundedOnLeft) : roundedLeft (roundedOnLeft) {}

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            const float corner = Celine::Theme::bandCornerRadius;
            juce::Path shape;
            shape.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), corner, corner,
                                       roundedLeft, ! roundedLeft, roundedLeft, ! roundedLeft);
            g.setColour (Celine::Theme::panel());
            g.fillPath (shape);
        }

       private:
        const bool roundedLeft;
    };

    BandBar inputBar { false }, outputBar { true };

    /** Top of the bottom band. */
    int bandTop = 0;

    /** The band's row: its span and bar height. */
    juce::Rectangle<int> bandRow;

    /** Kept alive while a file dialog is open. */
    std::unique_ptr<juce::FileChooser> fileChooser;

    /** The input file's dialog. Separate, because replacing a FileChooser closes its
        dialog. */
    std::unique_ptr<juce::FileChooser> inputFileChooser;

    /** Where the last save or load went; the next dialog opens there. */
    juce::File lastCircuitFile;

    /** Where the last impulse response came from. */
    juce::File lastCabinetFile;

    /** The player's finished-load count last reported, so reopening the window doesn't
        report an old load. */
    int reportedInputFileLoads = 0;

    /** The preset being loaded, so schematicChangedExternally(), which runs later on the
        message thread, knows it was a preset load and keeps the name. */
    juce::File presetBeingLoaded;

    /** Updates the preset field. Called wherever the loaded preset changes. */
    void setLoadedPreset (const juce::File& file);
    void setLoadedFactoryPreset (const juce::String& name);
    void markPresetModified();

    /** The user's preset folder. */
    PresetLibrary presets;

    // ==========================================================================
    // Main panels. The sheet fills the window; the others float over it.
    Celine::ElementPalette palette;
    Celine::SchematicCanvas canvas;
    Celine::ElementInspector inspector;

    /** The last build's messages. */
    Celine::MessageConsole console;

    //==========================================================================
    /** The circuit's controls (see ControlStrip). */
    Celine::ControlStrip controlStrip;

    // ==========================================================================
    // The plugin's own controls.
    juce::Slider inputSlider, outputSlider;
    /** Input and output gain at the ends of the band. */
    juce::Label inputLabel { {}, "INPUT" }, outputLabel { {}, "OUTPUT" };
    std::unique_ptr<Celine::PowerButton> bypassButton;
    juce::ComboBox channelModeBox;
    juce::Label channelModeLabel { {}, "CHANNELS" };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> inputAttachment, outputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> channelModeAttachment;

    bool pendingRebuild = false;

    /** Whether this window has considered the first-run preset question yet. */
    bool presetFolderChecked = false;

    /** Same, for the update question (asked after the preset one). */
    bool updateQuestionChecked = false;

    /** The prompt this window has up, if any. Null once answered. */
    juce::Component::SafePointer<juce::DialogWindow> openPrompt;

    bool nativeTitleBarChecked = false;

    /** Held samples already reported, and when the last report went up. */
    juce::int64 reportedHeldSamples = 0;
    juce::uint32 lastHeldReportMs = 0;
    void reportHeldSamples();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
