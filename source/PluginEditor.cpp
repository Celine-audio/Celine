#include "PluginEditor.h"

// Unconditional: JucePlugin_Build_Standalone is also defined for the test runner, and
// getInstance() returns null outside a standalone anyway.
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "Schematic/ExampleSchematics.h"
#include "UpdateCheck.h"

#include <CelineUI/EmbeddedAssets.h>
#include <CelineUI/AboutPanel.h>
#include <CelineUI/ThemePanel.h>

namespace
{
    /** A single ellipsis character, not three full stops. */
    const juce::String ellipsis = juce::String::fromUTF8 ("\xe2\x80\xa6");

    /** A themed message with a row of buttons (juce::AlertWindow ignores the palette). The
        first button is the accented choice. Each reports its index through onChoice; the
        caller closes the window. */
    class ChoicePrompt : public juce::Component
    {
       public:
        ChoicePrompt (PluginLookAndFeel& lnf, const juce::String& text, const juce::StringArray& choices)
        {
            setLookAndFeel (&lnf);

            message.setText (text, juce::dontSendNotification);
            message.setFont (Celine::Fonts::light (14.0f));
            message.setColour (juce::Label::textColourId, Celine::Theme::text());
            message.setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (message);

            for (int i = 0; i < choices.size(); ++i)
            {
                auto* button = buttons.add (new juce::TextButton (choices[i]));

                if (i == 0)
                {
                    button->setColour (juce::TextButton::buttonColourId, Celine::Theme::accent());
                    button->setColour (juce::TextButton::textColourOffId, Celine::Theme::text());
                }

                button->onClick = [this, i] { if (onChoice) onChoice (i); };
                addAndMakeVisible (button);
            }

            // Wide enough for the buttons side by side, tall enough for the wrapped text.
            int rowWidth = 0;

            for (auto* button : buttons)
            {
                button->changeWidthToFitText (buttonHeight);
                button->setSize (juce::jmax (button->getWidth() + 16, 96), buttonHeight);
                rowWidth += button->getWidth() + buttonGap;
            }

            const int width = juce::jmax (360, rowWidth - buttonGap + 2 * margin);

            juce::AttributedString wrapped;
            wrapped.append (text, message.getFont());
            juce::TextLayout layout;
            layout.createLayout (wrapped, static_cast<float> (width - 2 * margin - 10));

            setSize (width, 2 * margin + juce::roundToInt (layout.getHeight()) + 14 + 14 + buttonHeight);
        }

        ~ChoicePrompt() override { setLookAndFeel (nullptr); }

        void paint (juce::Graphics& g) override { g.fillAll (Celine::Theme::chrome()); }

        void resized() override
        {
            auto area = getLocalBounds().reduced (margin);
            auto row = area.removeFromBottom (buttonHeight);

            // Right to left, so the first choice sits leftmost.
            for (int i = buttons.size(); --i >= 0;)
            {
                buttons[i]->setBounds (row.removeFromRight (buttons[i]->getWidth()));
                row.removeFromRight (buttonGap);
            }

            area.removeFromBottom (14);
            message.setBounds (area);
        }

        std::function<void (int choice)> onChoice;

       private:
        static constexpr int margin = 18;
        static constexpr int buttonHeight = 32;
        static constexpr int buttonGap = 8;

        juce::Label message;
        juce::OwnedArray<juce::TextButton> buttons;
    };
}


//==============================================================================
PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), canvas (p.getSchematic()), controlStrip (p)
{
    setLookAndFeel (&lookAndFeel);

    // Not opaque: the tooltip's rounded corners aren't painted, and an opaque component
    // must fill every pixel.
    tooltips.setOpaque (false);

    // One palette per process: a colour change reaches every window.
    Celine::Theme::palette().addChangeListener (this);

    // Pick up a theme another instance may have saved.
    Celine::Theme::palette().refreshFromDisk();

    // Standalone only: JUCE's Options menu and Audio/MIDI dialog use the default look and
    // feel, which in a DAW is the host's.
    if (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    buildToolbar();
    buildPanels();
    buildBottomBand();

    // After the children exist (applyColours reaches into them), before setSize triggers
    // resized().
    applyColours();

    // Show the preset already loaded.
    lastCircuitFile = processorRef.presetFile;
    showPresetFromProcessor();

    // A preset load or session restore replaces the drawing. Bounced to the message
    // thread, since some hosts restore state from another one.
    processorRef.onSchematicReplaced = [safe = juce::Component::SafePointer<PluginEditor> (this)]
    {
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr)
                safe->schematicChangedExternally();
        });
    };

    // Parameters may have moved (automation) with no editor open; sync the drawing before
    // building the strip.
    processorRef.adoptControlPositions();

    controlStrip.refresh();
    updateToolButtons();

    // The baseline: undo stops here.
    history.reset (processorRef.getSchematic().toValueTree());

    // Samples held before this window opened were never in front of anyone; report only
    // new ones.
    reportedHeldSamples = processorRef.getHeldSampleTotal();
    updateActionButtons();

    setResizable (true, true);
    minimumWidth = narrowestWindow();
    const int minWidth = minimumWidth;
    const int minHeight = minimumHeight;

    // Read before setResizeLimits: that call fires resized(), which overwrites the stored
    // size with the minimum.
    const auto storedWidth = processorRef.editorWidth.load();
    const auto storedHeight = processorRef.editorHeight.load();

    setResizeLimits (minWidth, minHeight, 4000, 3000);

    // Clamped, so a stored size from another screen stays usable.
    setSize (juce::jlimit (minWidth, 4000, storedWidth),
             juce::jlimit (minHeight, 3000, storedHeight));

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PluginEditor> (this)]
    {
        if (safe != nullptr)
            safe->canvas.zoomToFit();
    });

    // The first-run question is asked from the timer (see offerPresetFolderOnFirstRun).
    startTimerHz (15);
}

void PluginEditor::buildToolbar()
{
    settingsButton = std::make_unique<Celine::IconButton> (
        "Settings", Celine::Assets::drawable ("gear-solid-full.svg"));

    // Assets are looked up by filename, not the JUCE identifier (which drops hyphens). See
    // EmbeddedAssets.h.
    {
        struct Spec { Action action; const char* name; const char* file; std::function<void()> click; };

        const Spec specs[] = {
            { Action::Mirror, "Mirror left to right", "mirror.svg", [this] { canvas.flip(); } },
            { Action::Flip,   "Flip top to bottom",   "flip.svg",   [this] { canvas.flipVertical(); } },
            { Action::Copy,   "Duplicate selection",  "copy.svg",   [this] { canvas.duplicateSelection(); } },
            { Action::Rotate, "Rotate a quarter turn","rotate.svg", [this] { canvas.rotate(); } },
            { Action::Undo,   "Undo",                 "undo.svg",   [this] { undo(); } },
            { Action::Redo,   "Redo",                 "redo.svg",   [this] { redo(); } },
        };

        for (const auto& spec : specs)
        {
            auto& slot = actionButtons[static_cast<size_t> (spec.action)];
            slot = std::make_unique<Celine::IconButton> (
                spec.name, Celine::Assets::drawable (spec.file));
            slot->onClick = spec.click;
            slot->setWantsKeyboardFocus (false);
            addAndMakeVisible (*slot);
        }
    }

    {
        struct Spec { std::unique_ptr<Celine::IconButton>* slot; const char* name; const char* file; };

        const Spec specs[] = {
            { &selectToolButton, "Select (S)", "arrow-pointer-solid-full.svg" },
            { &deleteToolButton, "Delete (X)", "trash-can-solid-full.svg" },
            { &saveButton,       "Save",       "floppy-disk-solid-full.svg" },
            { &loadButton,       "Load",       "file-import-solid-full.svg" },
            { &importButton,     "Import a circuit into this one", "plus-solid-full.svg" },
        };

        for (const auto& spec : specs)
        {
            *spec.slot = std::make_unique<Celine::IconButton> (
                spec.name, Celine::Assets::drawable (spec.file));
            (*spec.slot)->setWantsKeyboardFocus (false);
            addAndMakeVisible (**spec.slot);
        }
    }

    // Undo and redo share one housing, painted by the editor behind them.
    actionButton (Action::Undo).setDrawsFrame (false);
    actionButton (Action::Redo).setDrawsFrame (false);

    // None of these take keyboard focus; the canvas needs it for shortcuts.
    for (auto* button : std::initializer_list<juce::Button*> {
             &presetsButton, settingsButton.get(), &rebuildButton })
    {
        button->setWantsKeyboardFocus (false);
        addAndMakeVisible (button);
    }

    using Tool = Celine::SchematicCanvas::Tool;

    selectToolButton->onClick = [this] { canvas.setTool (Tool::Select); };
    deleteToolButton->onClick = [this] { canvas.setTool (Tool::Delete); };
    saveButton->onClick       = [this] { browseForCircuit (true); };
    loadButton->onClick       = [this] { browseForCircuit (false); };
    importButton->onClick     = [this] { browseForImport(); };
    presetsButton.setTooltip ("Preset selection list");
    presetsButton.onClick     = [this] { showPresetLibrary(); };

    // Made after the field, so it lies over it. Frameless, inside the field's fill.
    presetFolderButton = std::make_unique<Celine::IconButton> (
        "Choose the preset folder", Celine::Assets::drawable ("folder-open-solid-full.svg"));
    presetFolderButton->setDrawsFrame (false);
    presetFolderButton->setWantsKeyboardFocus (false);
    presetFolderButton->setTooltip ("Choose the folder your presets are kept in. Right-click the library to reveal it.");
    presetFolderButton->onClick = [this] { choosePresetFolder(); };
    addAndMakeVisible (*presetFolderButton);

    // The library drops down from the field; hidden until asked for (see
    // showPresetLibrary).
    presetLibrary.onDismiss = [this] { closePresetLibrary(); };
    presetLibrary.onNewSheet = [this] { closePresetLibrary(); startNewSheet(); };
    presetLibrary.onFactoryPresetChosen = [this] (int index) { closePresetLibrary(); loadFactoryPreset (index); };
    presetLibrary.onPresetChosen = [this] (const juce::File& file) { closePresetLibrary(); loadPresetFile (file); };

    // The library stays up and re-lists once a folder is chosen (see choosePresetFolder).
    presetLibrary.onChooseFolder = [this] { choosePresetFolder(); };

    presetLibrary.onPreferredHeightChanged = [this] { if (isPresetLibraryShowing()) placePresetLibrary(); };
    libraryDismisser.onClick = [this] { closePresetLibrary(); };

    addChildComponent (libraryDismisser);
    addChildComponent (presetLibrary);
    settingsButton->onClick   = [this] { showSettingsMenu(); };
    rebuildButton.setTooltip ("Sends the current schematic to the audio engine");
    rebuildButton.onClick     = [this] { rebuildCircuit(); };

    // The canvas changes tool on its own (placing returns to Select); the toolbar follows.
    canvas.onToolChanged = [this] { updateToolButtons(); };
}

void PluginEditor::buildPanels()
{
    // The sheet first, so the floating panels are in front. The console starts hidden.
    addAndMakeVisible (canvas);
    addChildComponent (console);
    addAndMakeVisible (inspector);
    addAndMakeVisible (palette);

    // Both size themselves to their contents.
    inspector.onPreferredSizeChanged = [this] { layOutFloatingPanels(); };
    inspector.setShortcutsShown (inspectorShortcutsShown());
    console.onPreferredSizeChanged = [this] { layOutFloatingPanels(); };

    // Clicking a message selects the part it names.
    console.onMessageClicked = [this] (int elementId)
    {
        canvas.selectElement (elementId);
        inspector.setElement (canvas.getSelectedElement());
    };

    palette.onTypeChosen = [this] (SchematicModel::ElementType type) { canvas.setPendingType (type); };
    palette.onWireChosen = [this] { canvas.setTool (Celine::SchematicCanvas::Tool::Wire); };

    // Restore the fold state; folding changes the width, so re-lay out.
    palette.setCollapsed (processorRef.paletteCollapsed);
    palette.onCollapsedChanged = [this]
    {
        processorRef.paletteCollapsed = palette.isCollapsed();
        layOutFloatingPanels();
    };

    canvas.onSchematicChanged = [this]
    {
        recordUndoState();
        markPending();
        inspector.setElement (canvas.getSelectedElement());
    };

    // The end of a drag: the next one starts a fresh undo step.
    canvas.onGestureEnd = [this] { gestureRecorded = false; };

    // The canvas asks for a snapshot; it knows nothing about audio.
    canvas.scopeReader = [this] (int elementId, Celine::ScopeReading& out)
    { return readScopeTrace (elementId, out); };

    canvas.onUndoRequested = [this] { undo(); };
    canvas.onRedoRequested = [this] { redo(); };

    canvas.onSelectionChanged = [this]
    {
        inspector.setElement (canvas.getSelectedElement());
        updateActionButtons();
    };

    inspector.currentReader = [this] (float& current, float& power, float& peak)
    { return processorRef.readInspectedCurrent (current, power, peak); };

    inspector.onInspectedElementChanged = [this] (int elementId)
    { processorRef.setInspectedElement (elementId); };

    // No rebuild needed, but the audio thread's samples-per-column changes.
    inspector.onScopeTimebaseChanged = [this] { processorRef.refreshScopeTiming(); };
    inspector.scopeReader = canvas.scopeReader;

    inspector.onEdited = [this] { recordUndoState(); markPending(); canvas.repaint(); };

    // Pot position and switch throw are live: update the strip instead of lighting
    // Rebuild.
    inspector.onControlEdited = [this]
    {
        if (const auto* element = canvas.getSelectedElement())
            controlStrip.pushPositionFor (element->id);

        markPresetModified();
        canvas.repaint();
    };

    inspector.onCabChanged = [this] { applyCabinetChange(); };
    inspector.onCabFileRequested = [this] { browseForCabinetFile(); };

    inspector.filePlayer = &processorRef.getInputFilePlayer();
    inspector.onInputFileRequested = [this] { browseForInputFile(); };
    reportedInputFileLoads = processorRef.getInputFilePlayer().getStatus().loadsFinished;
}

void PluginEditor::buildBottomBand()
{
    // First, so the knobs and captions are in front of their bars.
    addAndMakeVisible (inputBar);
    addAndMakeVisible (outputBar);

    // Tooltips are set with the readouts (see applyLevelReadouts).
    for (auto* slider : { &inputSlider, &outputSlider })
    {
        slider->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);

        // Drawn differently from the circuit's knobs; ignores the wheel.
        slider->getProperties().set (digitalGainProperty, true);
        slider->setScrollWheelEnabled (false);

        addAndMakeVisible (slider);
    }

    bypassButton = std::make_unique<Celine::PowerButton> (
        "Bypass", Celine::Assets::drawable ("power-off-solid-full.svg"));
    bypassButton->setWantsKeyboardFocus (false);
    addAndMakeVisible (*bypassButton);

    channelModeBox.setTooltip ("Channel selector. Stereo is twice as computationally heavy.");
    channelModeBox.addItemList ({ "Stereo", "Mono L", "Mono R", "Mono L+R" }, 1);
    channelModeBox.setWantsKeyboardFocus (false);

    addAndMakeVisible (channelModeBox);

    addAndMakeVisible (controlStrip);

    // A different number of controls changes the bar's width.
    controlStrip.onContentChanged = [this] { resized(); };

    // The strip owns widgets and parameters; the editor updates the drawing.
    controlStrip.onControlMoved = [this] (const std::vector<int>& ids, float position)
    { writeControlPositionToSchematic (ids, position); };

    for (auto* label : { &inputLabel, &outputLabel, &channelModeLabel })
    {
        label->setFont (Celine::Fonts::light (Celine::Theme::bandCaptionFontHeight));
        label->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (label);
    }

    inputAttachment  = std::make_unique<SliderAttachment> (processorRef.apvts, "input",  inputSlider);
    outputAttachment = std::make_unique<SliderAttachment> (processorRef.apvts, "output", outputSlider);

    // After the attachments, which would otherwise replace this text.
    applyLevelReadouts();
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, "bypass", *bypassButton);
    channelModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processorRef.apvts, "channels", channelModeBox);
}

PluginEditor::~PluginEditor()
{
    processorRef.onSchematicReplaced = nullptr;

    Celine::Theme::palette().removeChangeListener (this);

    // A first-run or update question still up goes with the window. It is a desktop
    // window of its own, so nothing else would close it, and its contents draw with
    // this editor's look and feel, which is about to be destroyed. The modal manager
    // sees the deletion and does not delete it again.
    if (openPrompt != nullptr)
        delete openPrompt.getComponent();

    // Before the children go, so none holds a dangling LookAndFeel.
    setLookAndFeel (nullptr);

    // Only if we set it.
    if (&juce::LookAndFeel::getDefaultLookAndFeel() == &lookAndFeel)
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

//==============================================================================
void PluginEditor::schematicChangedExternally()
{
    canvas.refresh();
    canvas.zoomToFit();
    inspector.setElement (nullptr);
    controlStrip.refresh();

    // The file set by loadPresetFile(), or none (e.g. a host restoring its session).
    // Consumed either way.
    setLoadedPreset (presetBeingLoaded);
    presetBeingLoaded = juce::File{};

    // A new document: reset the history.
    history.reset (processorRef.getSchematic().toValueTree());
    updateActionButtons();

    pendingRebuild = false;
    refreshRebuildButtonColour();
    setStatus ("Loaded.", false);

    // Every load route passes through here, so this fires once per document.
    if (processorRef.wasDocumentFromNewerBuild())
    {
        juce::NativeMessageBox::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle ("Warning")
                .withMessage (juce::String::fromUTF8 (
                    "This schematic was made in a newer version of Céline. Some elements or"
                    " models might not be available and might not work properly."))
                .withButton ("I understand")
                .withAssociatedComponent (this),
            nullptr);
    }
}

void PluginEditor::updateToolButtons()
{
    using Tool = Celine::SchematicCanvas::Tool;

    const auto tool = canvas.getTool();

    selectToolButton->setActive (tool == Tool::Select);
    deleteToolButton->setActive (tool == Tool::Delete);

    // Both placing and wiring are shown by the palette; the canvas is the authority.
    const auto armed = canvas.getPendingType();
    palette.setActive (tool == Tool::Place ? &armed : nullptr, tool == Tool::Wire);
}

void PluginEditor::setLoadedPreset (const juce::File& file)
{
    // File{} means no file behind the sheet (an example, a cleared sheet, a host session).
    lastCircuitFile = file;

    processorRef.presetFile = file;
    processorRef.presetName = file != juce::File{} ? file.getFileNameWithoutExtension()
                                                   : juce::String();
    processorRef.presetIsFactory = false;
    processorRef.presetModified = false;

    showPresetFromProcessor();
}

void PluginEditor::setLoadedFactoryPreset (const juce::String& name)
{
    // No file: Save must not overwrite anything, so lastCircuitFile is kept.
    processorRef.presetName = name;
    processorRef.presetIsFactory = true;
    processorRef.presetModified = false;

    showPresetFromProcessor();
}

void PluginEditor::markPresetModified()
{
    processorRef.presetModified = true;
    presetsButton.setModified (true);
}

void PluginEditor::showPresetFromProcessor()
{
    presetsButton.setPresetName (processorRef.presetName, processorRef.presetIsFactory);
    presetsButton.setModified (processorRef.presetModified);
}

void PluginEditor::recordUndoState()
{
    // Called after the edit; the history holds the state just reached (see
    // SchematicHistory).
    auto now = processorRef.getSchematic().toValueTree();

    // A drag is one undo step: notifications until the gesture ends are collapsed.
    if (canvas.isMidGesture())
    {
        if (gestureRecorded)
        {
            history.amend (std::move (now));
            updateActionButtons();
            return;
        }

        gestureRecorded = true;
    }

    history.record (std::move (now));
    updateActionButtons();
}

void PluginEditor::undo()
{
    const auto state = history.undo();

    if (! state.isValid())
        return;

    processorRef.getSchematic().restoreFromValueTree (state);

    // Undo restores the drawing, not the knobs: re-seed the positions from the parameters.
    processorRef.adoptControlPositions();

    // Undo is an edit: it lights Rebuild rather than rebuilding.
    canvas.refresh();
    inspector.setElement (canvas.getSelectedElement());
    markPending();
    updateActionButtons();
    setStatus ("Undo", false);
}

void PluginEditor::redo()
{
    const auto state = history.redo();

    if (! state.isValid())
        return;

    processorRef.getSchematic().restoreFromValueTree (state);

    // As in undo: the knobs stay put.
    processorRef.adoptControlPositions();

    canvas.refresh();
    inspector.setElement (canvas.getSelectedElement());
    markPending();
    updateActionButtons();
    setStatus ("Redo", false);
}

void PluginEditor::updateActionButtons()
{
    actionButton (Action::Undo).setEnabled (history.canUndo());
    actionButton (Action::Redo).setEnabled (history.canRedo());

    const bool hasSelection = ! canvas.getSelection().empty();
    const bool placing = canvas.getTool() == Celine::SchematicCanvas::Tool::Place;

    for (const auto action : { Action::Mirror, Action::Flip, Action::Rotate })
        actionButton (action).setEnabled (hasSelection || placing);

    // Duplicate also copies wires; the other three only act on parts.
    actionButton (Action::Copy).setEnabled (hasSelection || ! canvas.getSelectedWires().empty() || placing);

    // The housing is painted by the editor, so repaint it too.
    repaint (undoRedoHousing);
}

void PluginEditor::markPending()
{
    // The sheet no longer matches its file. Separate from pendingRebuild, which is about
    // what is heard.
    markPresetModified();

    if (pendingRebuild)
        return;

    pendingRebuild = true;
    refreshRebuildButtonColour();
    setStatus ("Circuit changed. Press \"Rebuild\" to load it.", false);
    repaint();
}

void PluginEditor::rebuildCircuit()
{
    const auto result = processorRef.rebuild();

    if (! result.isValid())
    {
        // The previous circuit keeps running; the reason is the first diagnostic.
        console.setMessages ("Build failed", true, result.diagnostics);
        return;
    }

    pendingRebuild = false;
    refreshRebuildButtonColour();

    controlStrip.refresh();

    const auto parts = juce::String (processorRef.getSchematic().getElements().size());
    const auto issues = static_cast<int> (result.diagnostics.size());

    console.setMessages ("Built " + parts + " parts"
                             + (issues > 0 ? ", " + juce::String (issues)
                                                 + (issues == 1 ? " note" : " notes")
                                           : ""),
                         false, result.diagnostics);

    repaint();
}

//==============================================================================
void PluginEditor::writeControlPositionToSchematic (const std::vector<int>& elementIds, float position)
{
    bool moved = false;

    // Every part on the shaft.
    for (const auto elementId : elementIds)
    {
        auto* element = processorRef.getSchematic().findElement (elementId);

        if (element == nullptr)
            continue;

        const auto before = element->getControlPosition();
        element->setControlPosition (position);
        moved = moved || ! juce::approximatelyEqual (element->getControlPosition(), before);
    }

    if (! moved)
        return;

    // Switches draw their state, so redraw the sheet.
    canvas.repaint();

    // Update the inspector if it is showing one of these parts.
    if (auto* selected = canvas.getSelectedElement(); selected != nullptr
        && std::find (elementIds.begin(), elementIds.end(), selected->id) != elementIds.end())
        inspector.setElement (selected);
}

void PluginEditor::reportHeldSamples()
{
    // A sample the circuit cannot settle is held: the one before it is repeated (see
    // Circuit::process). A few are inaudible; a run of them is a part of the circuit's
    // travel the solver cannot follow, and can click or stick. Nothing else says so, so the
    // console does -- at most once a second, saying how many since the last report, and
    // as a warning, which keeps it up until something else replaces it.
    const auto total = processorRef.getHeldSampleTotal();

    if (total <= reportedHeldSamples)
        return;

    const auto now = juce::Time::getMillisecondCounter();

    if (lastHeldReportMs != 0 && now - lastHeldReportMs < 1000)
        return;

    const auto fresh = total - reportedHeldSamples;
    reportedHeldSamples = total;
    lastHeldReportMs = now;

    console.setWarningHeadline (
        "The circuit could not be solved on " + juce::String (fresh)
        + (fresh == 1 ? " sample" : " samples")
        + ", they were repeated to protect the audio thread. This is an engine limitation or a schematic issue. Please report it.");
}

void PluginEditor::timerCallback()
{
    // Backstop for parentHierarchyChanged, in case the window wasn't there yet.
    adoptNativeTitleBar();

    reportHeldSamples();

    // First-run questions and update notices, only with the window on screen, one dialog
    // at a time.
    if (isShowing() && openPrompt == nullptr)
    {
        if (! presetFolderChecked)
        {
            presetFolderChecked = true;
            offerPresetFolderOnFirstRun();
        }
        else if (! updateQuestionChecked)
        {
            updateQuestionChecked = true;
            offerUpdateChecksOnFirstRun();
            startUpdateCheckIfEnabled();
        }
        else if (const auto release = UpdateCheck::Session::takeUpdateToAnnounce())
        {
            announceUpdate (release->version);
        }
    }

    // Switch buttons have no attachment; keep them in step with the parameter.
    controlStrip.syncToggles();

    // Free the circuit the last Rebuild faded out (the audio thread can't).
    processorRef.releaseFadedCircuits();

    // Repaint for live traces, only when there is a scope.
    if (processorRef.getSchematic().countElementsOfType (SchematicModel::ElementType::Scope) > 0)
        canvas.repaint();

    // Always: readouts are cheap and current is live without a scope.
    inspector.repaintReadouts();
    reportInputFileLoads();
}

//==============================================================================
void PluginEditor::setStatus (const juce::String& message, bool isError)
{
    // Everything the plugin has to say goes to the console.
    console.setHeadline (message, isError);
}

bool PluginEditor::readScopeTrace (int elementId, Celine::ScopeReading& out) const
{
    const auto* trace = processorRef.getScopeTrace (elementId);

    if (trace == nullptr)
        return false;

    // Copied, not pointed at, so the renderer sees consistent columns. The index is read
    // first with acquire ordering.
    out.writeColumn = trace->writeColumn.load (std::memory_order_acquire);

    for (int c = 0; c < Celine::ScopeReading::columns; ++c)
    {
        out.minimum[c] = trace->minimum[c].load (std::memory_order_relaxed);
        out.maximum[c] = trace->maximum[c].load (std::memory_order_relaxed);
    }

    out.dcAverage = trace->dcAverage.load (std::memory_order_relaxed);
    out.peakToPeak = trace->peakToPeak.load (std::memory_order_relaxed);
    out.live = trace->live.load (std::memory_order_relaxed);

    // Axes come from the drawing, so they update without a rebuild.
    out.windowSeconds = static_cast<float> (PluginProcessor::scopeWindowSeconds);

    if (const auto* element = processorRef.getSchematic().findElement (elementId))
    {
        out.autoScale = element->scopeAutoScale;
        out.rangeMin = static_cast<float> (element->scopeMin);
        out.rangeMax = static_cast<float> (element->scopeMax);
        out.windowSeconds = static_cast<float> (element->scopeSeconds);
    }

    return true;
}

void PluginEditor::browseForImport()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Import a circuit into this one",
        lastCircuitFile != juce::File{}
            ? lastCircuitFile
            : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        juce::String ("*") + PluginProcessor::circuitFileExtension);

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();

        if (file != juce::File{})
            importSchematicFile (file);
    });
}

bool PluginEditor::importSchematicFile (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);

    // Refused like every other route in (see loadPresetFile); a foreign file leaves the
    // sheet alone.
    const auto document = xml != nullptr ? juce::ValueTree::fromXml (*xml) : juce::ValueTree{};
    const auto drawing = document.getChildWithName ("SCHEMATIC");

    if (! document.hasType (PluginProcessor::documentType) || ! drawing.isValid())
    {
        setStatus (file.getFileName() + " isn't a circuit file.", true);
        return false;
    }

    SchematicModel::Schematic incoming;
    incoming.restoreFromValueTree (drawing);

    if (incoming.isEmpty())
    {
        setStatus (file.getFileName() + " has nothing drawn in it.", false);
        return false;
    }

    auto& sheet = processorRef.getSchematic();

    // Counted before merging, so the warning describes what the import brought.
    const int inputsBefore = sheet.countElementsOfType (SchematicModel::ElementType::Input);
    const int outputsBefore = sheet.countElementsOfType (SchematicModel::ElementType::Output);

    // Placed to the right of the existing drawing.
    constexpr int gapInSquares = 4;

    const auto existing = sheet.getContentBounds();
    const auto arriving = incoming.getContentBounds();

    const auto delta = existing.isEmpty()
                         ? juce::Point<int> {}
                         : juce::Point<int> { existing.getRight() + gapInSquares - arriving.getX(),
                                              existing.getY() - arriving.getY() };

    const auto merged = sheet.merge (incoming, delta);

    // Selected as one block, ready to drag.
    canvas.refresh();
    canvas.setSelection (merged.elementIds, merged.wireIds);
    inspector.setElement (canvas.getSelectedElement());
    updateActionButtons();

    recordUndoState();
    markPending();
    markPresetModified();

    juce::String message;
    message << "Imported " << merged.elementIds.size()
            << (merged.elementIds.size() == 1 ? " part" : " parts");

    if (! merged.wireIds.empty())
        message << " and " << merged.wireIds.size()
                << (merged.wireIds.size() == 1 ? " wire" : " wires");

    message << " from " << file.getFileName() << ".";

    // A terminal names its net, so a second Input merges with the first. Grounds are meant
    // to merge, hence a warning rather than a refusal.
    juce::StringArray duplicated;

    if (inputsBefore > 0
        && sheet.countElementsOfType (SchematicModel::ElementType::Input) > inputsBefore)
        duplicated.add ("Input");

    if (outputsBefore > 0
        && sheet.countElementsOfType (SchematicModel::ElementType::Output) > outputsBefore)
        duplicated.add ("Output");

    if (! duplicated.isEmpty())
        message << "  It brought its own " << duplicated.joinIntoString (" and ")
                << " terminal" << (duplicated.size() == 1 ? "" : "s")
                << ", which name" << (duplicated.size() == 1 ? "s" : "")
                << " the same net as the one already here -- so the two circuits are wired in"
                   " parallel, not in series. Delete whichever you don't want, then wire the"
                   " two together.";

    setStatus (message, false);
    return true;
}

void PluginEditor::browseForCabinetFile()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Load a cabinet impulse response",
        lastCabinetFile != juce::File{}
            ? lastCabinetFile
            : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        "*.wav");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();

        if (file == juce::File{})
            return; // cancelled

        // Looked up again: the dialog is asynchronous and the selection or sheet may have
        // changed.
        auto* selected = canvas.getSelectedElement();

        if (selected == nullptr || selected->type != SchematicModel::ElementType::Output)
            return;

        lastCabinetFile = file;
        selected->cabFile = file.getFullPathName();

        // Choosing a file enables the cabinet.
        selected->cabEnabled = true;

        inspector.setElement (selected);
        applyCabinetChange();
    });
}

void PluginEditor::browseForInputFile()
{
    auto& player = processorRef.getInputFilePlayer();
    const auto current = player.getStatus().file;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    inputFileChooser = std::make_unique<juce::FileChooser> (
        "Choose an audio file to play through the circuit",
        current != juce::File{} ? current
                                : juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        formats.getWildcardForAllFormats());

    inputFileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();

        if (file == juce::File{})
            return; // cancelled

        // Choosing a file arms file input once it has loaded; it plays when Play is pressed.
        processorRef.getInputFilePlayer().load (file, true);

        setStatus ("Loading " + file.getFileName() + "...", false);
    });
}

void PluginEditor::reportInputFileLoads()
{
    const auto status = processorRef.getInputFilePlayer().getStatus();

    if (status.loadsFinished == reportedInputFileLoads)
        return;

    reportedInputFileLoads = status.loadsFinished;

    if (status.problem.isNotEmpty())
    {
        setStatus (status.problem, true);
        return;
    }

    auto message = "Loaded " + status.file.getFileName() + ": "
                 + juce::String ((double) status.length / juce::jmax (1.0, status.sampleRate), 1) + " s";

    // Report resampling.
    if (! juce::exactlyEqual (status.sourceRate, status.sampleRate))
        message << ", resampled from " << juce::String (status.sourceRate / 1000.0, 1)
                << " to " << juce::String (status.sampleRate / 1000.0, 1) << " kHz";

    if (status.truncated)
        message << ", cut to the first " << juce::String ((int) InputFilePlayer::maximumSeconds) << " s";

    setStatus (message + ".", false);
}

void PluginEditor::applyCabinetChange()
{
    // Undoable (it is in the document) but no rebuild: the cabinet isn't in the matrix.
    recordUndoState();
    markPresetModified();

    const auto problem = processorRef.refreshCabinet();

    // Not an error: the setting is kept and the signal passes without it.
    setStatus (problem, false);
}

void PluginEditor::browseForCircuit (bool saving)
{
    // One dialog for both; they differ only in title, flags and result handling.
    fileChooser = std::make_unique<juce::FileChooser> (
        saving ? "Save circuit" : "Load circuit",
        lastCircuitFile != juce::File{} ? lastCircuitFile
                                       : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        juce::String ("*") + PluginProcessor::circuitFileExtension);

    const auto chooserFlags = juce::FileBrowserComponent::canSelectFiles
                     | (saving ? juce::FileBrowserComponent::saveMode
                                     | juce::FileBrowserComponent::warnAboutOverwriting
                               : juce::FileBrowserComponent::openMode);

    fileChooser->launchAsync (chooserFlags, [this, saving] (const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();

        if (file == juce::File{})
            return; // cancelled

        if (! saving)
        {
            loadPresetFile (file);
            return;
        }

        if (file.getFileExtension().isEmpty())
            file = file.withFileExtension (PluginProcessor::circuitFileExtension);

        const auto xml = processorRef.createDocument().createXml();

        if (xml != nullptr && xml->writeTo (file))
        {
            // Saving makes an edited sheet a preset again.
            setLoadedPreset (file);
            setStatus ("Saved to " + file.getFileName(), false);
        }
        else
        {
            setStatus ("Couldn't write " + file.getFullPathName(), true);
        }
    });
}

bool PluginEditor::loadPresetFile (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);

    // Set before restoreDocument, which queues the callback that reads it.
    presetBeingLoaded = file;

    // A foreign file leaves the running circuit alone, whichever route it came by.
    if (xml == nullptr || ! processorRef.restoreDocument (juce::ValueTree::fromXml (*xml)))
    {
        // Nothing was replaced, so no callback will consume this.
        presetBeingLoaded = juce::File{};
        setStatus (file.getFileName() + " isn't a circuit file.", true);
        return false;
    }

    // After restoreDocument: schematicChangedExternally() clears the name, so put it back.
    setLoadedPreset (file);

    // restoreDocument() has already updated the canvas and the strip.
    setStatus ("Loaded " + file.getFileName(), false);
    return true;
}

//==============================================================================
void PluginEditor::showPresetLibrary()
{
    // The field toggles the library.
    if (isPresetLibraryShowing())
    {
        closePresetLibrary();
        return;
    }

    // Every opening starts from the whole list: a search left from last time hid New
    // and everything that did not match, which looked like presets gone missing.
    // Cleared here rather than on closing, so every way the library is put away is
    // covered.
    presetLibrary.setSearchText ({});

    // Re-read every time: files may have changed on disk.
    refreshPresetLibrary();

    libraryDismisser.setBounds (getLocalBounds());
    libraryDismisser.setVisible (true);
    libraryDismisser.toFront (false);

    placePresetLibrary();
    presetLibrary.setVisible (true);
    presetLibrary.toFront (false);

    // Keep the folder icon on top.
    presetFolderButton->toFront (false);

    // Typing searches straight away.
    presetLibrary.focusSearch();
}

void PluginEditor::closePresetLibrary()
{
    if (! isPresetLibraryShowing())
        return;

    presetLibrary.setVisible (false);
    libraryDismisser.setVisible (false);

    // Give the keyboard back to the sheet.
    if (canvas.isShowing())
        canvas.grabKeyboardFocus();
}

void PluginEditor::placePresetLibrary()
{
    constexpr int edge = 8;

    // Exactly the field's width; long names are cut with an ellipsis.
    const auto field = presetsButton.getBounds();

    presetLibrary.setFieldArea (field.withZeroOrigin());

    const int height = juce::jmin (presetLibrary.getPreferredHeight(), getHeight() - field.getY() - edge);

    presetLibrary.setBounds (field.withHeight (height));
    libraryDismisser.setBounds (getLocalBounds());
}

void PluginEditor::refreshPresetLibrary()
{
    presetLibrary.setFactoryPresets (SchematicModel::Examples::getNames());
    presetLibrary.setFolder (presets.hasDirectory() ? presets.getDirectory() : juce::File{});

    // Mark the loaded preset.
    presetLibrary.setCurrentPreset (presetsButton.isFactoryPreset() ? juce::File{} : processorRef.presetFile,
                                    presetsButton.isFactoryPreset() ? presetsButton.getPresetName() : juce::String());
}

void PluginEditor::startNewSheet()
{
    processorRef.getSchematic().clear();
    setLoadedPreset ({});
    canvas.refresh();
    canvas.zoomToFit();
    inspector.setElement (nullptr);
    rebuildCircuit();
}

void PluginEditor::loadFactoryPreset (int index)
{
    const auto names = SchematicModel::Examples::getNames();

    if (! juce::isPositiveAndBelow (index, names.size()))
        return;

    SchematicModel::Examples::load (processorRef.getSchematic(), index);

    canvas.refresh();
    canvas.zoomToFit();
    inspector.setElement (nullptr);
    rebuildCircuit();

    // After rebuildCircuit and the canvas refresh; neither goes through restoreDocument,
    // so no callback will overwrite this.
    setLoadedFactoryPreset (names[index]);
    setStatus ("Loaded " + names[index], false);
}

void PluginEditor::choosePresetFolder()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose a folder for " + PresetLibrary::getProductName() + " presets",
        presets.hasDirectory() ? presets.getDirectory() : PresetLibrary::getSuggestedDirectory());

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectDirectories,
        [this] (const juce::FileChooser& chooser)
        {
            const auto folder = chooser.getResult();

            if (folder == juce::File{} || ! folder.isDirectory())
                return; // cancelled, or the folder went away

            presets.setDirectory (folder);

            // If the library is up, list the new folder now.
            if (isPresetLibraryShowing())
            {
                refreshPresetLibrary();
                placePresetLibrary();
            }

            const auto count = presets.getPresets().size();
            setStatus ("Preset folder: " + folder.getFullPathName() + "  --  "
                           + juce::String (count) + (count == 1 ? " preset" : " presets"),
                       false);
        });
}

void PluginEditor::parentHierarchyChanged()
{
    // Before the window is shown, so the native frame is there from the first paint.
    adoptNativeTitleBar();
}

void PluginEditor::adoptNativeTitleBar()
{
    if (nativeTitleBarChecked)
        return;

    // Only the standalone owns its window; in a DAW it is the host's.
    if (processorRef.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
    {
        nativeTitleBarChecked = true;
        return;
    }

    auto* window = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent());

    // Not parented yet. Doesn't latch, so the timer can try again.
    if (window == nullptr)
        return;

    nativeTitleBarChecked = true;

    // JUCE's standalone window asks for minimise and close only, and a native frame
    // gets exactly the buttons asked for: on Windows that is no maximise box, so the
    // window could not be maximised by button, title-bar double-click or Win+Up. macOS
    // was unaffected, since any resizable window there gets the green full-screen button.
    // The style is read when the peer is made, so it has to be set before going native,
    // and a window that already is native has to be put back on the desktop to see it.
    constexpr int buttons = juce::DocumentWindow::minimiseButton
                          | juce::DocumentWindow::maximiseButton
                          | juce::DocumentWindow::closeButton;
    const auto stylesBefore = window->getDesktopWindowStyleFlags();
    window->setTitleBarButtonsRequired (buttons, false);
    const bool buttonsChanged = window->getDesktopWindowStyleFlags() != stylesBefore;

    // The window outlives the editor and may already be native.
    if (! window->isUsingNativeTitleBar())
        window->setUsingNativeTitleBar (true);
    else if (buttonsChanged && window->isOnDesktop())
        window->addToDesktop();

    // Native title bars drop JUCE's Options button; the Settings menu opens the audio
    // dialog instead.
}

void PluginEditor::offerPresetFolderOnFirstRun()
{
    if (presets.hasDirectory() || presets.hasBeenOffered())
        return;

    // Only asked with a window on screen: editors are also built by the VST3 manifest
    // helper and the tests, and the answer is remembered.
    jassert (isShowing());

    // Marked when asked: closing the dialog counts as an answer.
    presets.markOffered();

    // Themed (juce::AlertWindow ignores the palette).
    class FolderPrompt : public juce::Component
    {
       public:
        explicit FolderPrompt (PluginLookAndFeel& lnf)
        {
            setLookAndFeel (&lnf);
            setSize (360, 160);

            message.setText ("Choose a folder to keep your circuits in, and they will "
                             "show up in the Presets menu.\n\n"
                             "You can change it later from that menu.",
                             juce::dontSendNotification);
            message.setFont (Celine::Fonts::light (14.0f));
            message.setColour (juce::Label::textColourId, Celine::Theme::text());
            message.setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (message);

            choose.setColour (juce::TextButton::buttonColourId, Celine::Theme::accent());
            choose.setColour (juce::TextButton::textColourOffId, Celine::Theme::text());
            addAndMakeVisible (choose);
            addAndMakeVisible (later);
        }

        ~FolderPrompt() override { setLookAndFeel (nullptr); }

        void paint (juce::Graphics& g) override { g.fillAll (Celine::Theme::chrome()); }

        void resized() override
        {
            auto area = getLocalBounds().reduced (18);
            auto row = area.removeFromBottom (32);

            later.setBounds (row.removeFromRight (96));
            row.removeFromRight (8);
            choose.setBounds (row.removeFromRight (140));

            area.removeFromBottom (14);
            message.setBounds (area);
        }

        juce::TextButton choose { "Choose folder" }, later { "Not now" };

       private:
        juce::Label message;
    };

    auto prompt = std::make_unique<FolderPrompt> (lookAndFeel);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = PresetLibrary::getProductName() + " presets";
    options.dialogBackgroundColour = Celine::Theme::chrome();
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    auto* raw = prompt.get();
    options.content.setOwned (prompt.release());

    // Async: never run a modal loop inside a host.
    auto* window = options.launchAsync();
    openPrompt = window;

    const juce::Component::SafePointer<PluginEditor> safe (this);

    // exitModalState only: the window deletes itself (deleteWhenDismissed). SafePointer,
    // since it may be gone by the callback.
    const juce::Component::SafePointer<juce::DialogWindow> dialog (window);

    raw->choose.onClick = [safe, dialog]
    {
        if (dialog != nullptr)
            dialog->exitModalState (0);

        if (safe != nullptr)
            safe->choosePresetFolder();
    };

    raw->later.onClick = [dialog]
    {
        if (dialog != nullptr)
            dialog->exitModalState (0);
    };
}

//==============================================================================
juce::DialogWindow* PluginEditor::launchPrompt (const juce::String& title, std::unique_ptr<juce::Component> content)
{
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = title;
    options.dialogBackgroundColour = Celine::Theme::chrome();
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.content.setOwned (content.release());

    // Async, and the window deletes itself, so close with exitModalState().
    auto* window = options.launchAsync();
    openPrompt = window;
    return window;
}

void PluginEditor::offerUpdateChecksOnFirstRun()
{
    UpdateCheck::Preferences preferences;

    if (preferences.hasBeenOffered())
        return;

    // Only with a window on screen, marked when asked (see the preset folder question).
    // Closing it leaves the checks off.
    jassert (isShowing());
    preferences.markOffered();

    const auto name = PresetLibrary::getProductName();

    auto prompt = std::make_unique<ChoicePrompt> (
        lookAndFeel,
        "Check for a newer version of " + name + " when it starts?\n\n"
            "Once per launch, " + name + " asks GitHub for its newest release and tells "
            "you if it is newer than the one you have. Nothing about you or your "
            "sessions is sent.\n\n"
            "You can change this later in the Settings menu.",
        juce::StringArray { "Check for updates", "No thanks" });

    auto* raw = prompt.get();
    const juce::Component::SafePointer<juce::DialogWindow> dialog (launchPrompt (name + " updates", std::move (prompt)));
    const juce::Component::SafePointer<PluginEditor> safe (this);

    raw->onChoice = [safe, dialog] (int choice)
    {
        if (dialog != nullptr)
            dialog->exitModalState (0);

        if (choice != 0)
            return;

        UpdateCheck::Preferences().setEnabled (true);

        // Check this launch too.
        if (safe != nullptr)
            safe->startUpdateCheckIfEnabled();
    };
}

void PluginEditor::startUpdateCheckIfEnabled()
{
    // Already checked this launch, or turned off.
    if (UpdateCheck::Session::hasStarted() || ! UpdateCheck::Preferences().isEnabled())
        return;

    UpdateCheck::Session::startIfNeeded();
}

void PluginEditor::announceUpdate (const juce::String& version)
{
    // Turned off in another window meanwhile.
    if (! UpdateCheck::Preferences().isEnabled())
        return;

    const auto name = PresetLibrary::getProductName();

    auto prompt = std::make_unique<ChoicePrompt> (
        lookAndFeel,
        "A new update is available.\n\n"
            + name + " " + version + " is out. You have " + UpdateCheck::installedVersion() + ".",
        juce::StringArray { "Download", "Remind me later", "Turn off update checks" });

    auto* raw = prompt.get();
    const juce::Component::SafePointer<juce::DialogWindow> dialog (launchPrompt (name + " update", std::move (prompt)));
    const juce::Component::SafePointer<PluginEditor> safe (this);

    raw->onChoice = [safe, dialog] (int choice)
    {
        if (dialog != nullptr)
            dialog->exitModalState (0);

        if (choice == 0)
        {
            // The releases page: the right download depends on the machine.
            UpdateCheck::releasesPage().launchInDefaultBrowser();
        }
        else if (choice == 2)
        {
            UpdateCheck::Preferences().setEnabled (false);

            if (safe != nullptr)
                safe->setStatus ("Update checks turned off. Turn them back on from the Settings menu.", false);
        }

        // "Remind me later" does nothing: the next launch asks again.
    };
}

//==============================================================================
juce::StandalonePluginHolder* PluginEditor::standalonePluginHolder()
{
    return juce::StandalonePluginHolder::getInstance();
}

namespace
{
    const juce::String levelsInDbuKey { "levelsInDbu" };
    const juce::String inspectorShortcutsKey { "inspectorShortcuts" };
}

bool PluginEditor::inspectorShortcutsShown()
{
    const juce::PropertiesFile file (PresetLibrary().getSettingsFile(), PresetLibrary::getSettingsOptions());
    return file.getBoolValue (inspectorShortcutsKey, true);
}

void PluginEditor::setInspectorShortcutsShown (bool shown)
{
    juce::PropertiesFile file (PresetLibrary().getSettingsFile(), PresetLibrary::getSettingsOptions());
    file.setValue (inspectorShortcutsKey, shown);
    file.saveIfNeeded();
}

bool PluginEditor::levelsShownInDbu()
{
    // Opened for each read and write, since every instance shares the file.
    const juce::PropertiesFile file (PresetLibrary().getSettingsFile(), PresetLibrary::getSettingsOptions());
    return file.getBoolValue (levelsInDbuKey, false);
}

void PluginEditor::setLevelsShownInDbu (bool inDbu)
{
    juce::PropertiesFile file (PresetLibrary().getSettingsFile(), PresetLibrary::getSettingsOptions());
    file.setValue (levelsInDbuKey, inDbu);
    file.saveIfNeeded();
}

void PluginEditor::applyLevelReadouts()
{
    using Strip = Celine::ControlStrip;

    // Accepts the value with or without its unit.
    const auto number = [] (const juce::String& text) { return text.getDoubleValue(); };

    if (levelsShownInDbu())
    {
        Strip::showValueBelow (inputSlider,
                               [] (double gain) { return Strip::formatDbu (PluginProcessor::inputGainToDbu (gain)); },
                               [number] (const juce::String& text)
                               { return number (text) - PluginProcessor::fullScaleSineDbu(); });
        Strip::showValueBelow (outputSlider,
                               [] (double gain) { return Strip::formatDbu (PluginProcessor::outputGainToDbu (gain)); },
                               [number] (const juce::String& text)
                               { return PluginProcessor::fullScaleSineDbu() - number (text); });

        inputSlider.setTooltip ("Input level, as the dBu a full-scale sine reaches the circuit at");
        outputSlider.setTooltip ("Output level, as the dBu at the circuit's output that comes out at full scale");
        return;
    }

    for (auto* slider : { &inputSlider, &outputSlider })
        Strip::showValueBelow (*slider, Strip::formatDecibels, number);

    inputSlider.setTooltip ("Input level");
    outputSlider.setTooltip ("Output level");
}

void PluginEditor::showSettingsMenu()
{
    // Settings: simulation, display, audio device, then the plugin itself.
    juce::PopupMenu menu;

    // --------------------------------------------------------------------------
    // Oversampling
    menu.addSectionHeader ("Oversampling");

    for (const int factor : PluginProcessor::oversamplingFactors)
    {
        juce::PopupMenu::Item item (factor == 1 ? "Off" : juce::String (factor) + juce::String ("x"));
        item.isTicked = processorRef.oversamplingFactor == factor;
        item.setAction ([this, factor]
        {
            const auto result = processorRef.setOversamplingFactor (factor);
            controlStrip.refresh();

            if (! result.isValid())
            {
                setStatus (result.error, true);
                return;
            }

            setStatus (factor == 1  ? juce::String ("Oversampling off.")
                     : factor <= 4  ? juce::String (factor) + "x oversampling. Reduces aliasing but increases CPU usage."
                     : factor == 8  ? juce::String ("8x oversampling. About 7x usage.")
                                    : juce::String ("16x oversampling. About 13x usage, best kept for rendering."),
                       false);
        });

        menu.addItem (item);
    }

    // --------------------------------------------------------------------------
    // Fast maths: not an accuracy trade; whether it helps depends on the platform.
    menu.addSeparator();
    menu.addSectionHeader ("Performance");

    juce::PopupMenu::Item fastMath ("Fast exp / log / pow");
    fastMath.isTicked = processorRef.buildOptions.fastMath;
    fastMath.setAction ([this]
    {
        auto& option = processorRef.buildOptions.fastMath;
        option = ! option;

        // Set at build time in the engine, so it needs a rebuild.
        rebuildCircuit();
        setStatus (option ? juce::String ("Fast exp / log / pow on.") : juce::String ("Fast exp / log / pow off."), false);
    });
    menu.addItem (fastMath);

    // --------------------------------------------------------------------------
    // Level display: gain, or dBu calibration levels.
    menu.addSeparator();
    menu.addSectionHeader ("Levels");

    juce::PopupMenu::Item dbu ("Show input and output in dBu");
    dbu.isTicked = levelsShownInDbu();
    dbu.setAction ([this, wasOn = dbu.isTicked]
    {
        setLevelsShownInDbu (! wasOn);
        applyLevelReadouts();

        setStatus (wasOn ? juce::String ("Input and output shown as gain in dB.")
                         : juce::String ("Input and output shown in dBu: the level of a full-scale sine at the circuit. "
                                         "These are the calibration levels for a capture rendered here."),
                   false);
    });
    menu.addItem (dbu);

    // --------------------------------------------------------------------------
    // Whether the inspector shows the shortcut list when nothing is selected.
    menu.addSeparator();
    menu.addSectionHeader ("Inspector");

    juce::PopupMenu::Item shortcuts ("Show inspector when nothing is selected");
    shortcuts.isTicked = inspectorShortcutsShown();
    shortcuts.setAction ([this, wasOn = shortcuts.isTicked]
    {
        setInspectorShortcutsShown (! wasOn);
        inspector.setShortcutsShown (! wasOn);

        setStatus (wasOn ? juce::String ("The inspector now appears only while a part is selected.")
                         : juce::String ("The inspector shows the shortcuts when nothing is selected."),
                   false);
    });
    menu.addItem (shortcuts);

    // --------------------------------------------------------------------------
    // Standalone only: open the audio device dialog directly.
    if (auto* holder = standalonePluginHolder())
    {
        menu.addSeparator();
        menu.addSectionHeader ("Audio");

        juce::PopupMenu::Item audio ("Audio / MIDI settings" + ellipsis);
        audio.setAction ([holder] { holder->showAudioSettingsDialog(); });

        menu.addItem (audio);
    }

    // --------------------------------------------------------------------------
    // About, in every build (in a host it is the only route to the licence notice).
    menu.addSeparator();
    menu.addSectionHeader (PresetLibrary::getProductName());

    // Whether to check for updates at launch.
    juce::PopupMenu::Item updates ("Check for updates at launch");
    updates.isTicked = UpdateCheck::Preferences().isEnabled();
    updates.setAction ([this, wasOn = updates.isTicked]
    {
        UpdateCheck::Preferences().setEnabled (! wasOn);

        if (wasOn)
        {
            setStatus ("Update checks off.", false);
            return;
        }

        // Check this launch, if not done already.
        setStatus ("Update checks on. " + PresetLibrary::getProductName()
                       + " will look for a newer version once each time it starts.",
                   false);
        startUpdateCheckIfEnabled();
    });
    menu.addItem (updates);

    juce::PopupMenu::Item theme ("Theme" + ellipsis);
    theme.setAction ([this] { Celine::showThemeWindow (this); });
    menu.addItem (theme);

    juce::PopupMenu::Item about ("About " + PresetLibrary::getProductName() + ellipsis);
    about.setAction ([this] { showAboutDialog(); });
    menu.addItem (about);

    // A popup menu has no parent, so set its look and feel explicitly.
    menu.setLookAndFeel (&lookAndFeel);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (settingsButton.get()));
}

//==============================================================================

void PluginEditor::refreshRebuildButtonColour()
{
    rebuildButton.setColour (juce::TextButton::buttonColourId,
                             pendingRebuild
                                 ? Celine::Theme::pending()
                                 : getLookAndFeel().findColour (juce::TextButton::buttonColourId));
}

void PluginEditor::applyColours()
{
    // Bypass is red; set here so theme changes reach it.
    if (bypassButton != nullptr)
        bypassButton->setActiveColour (Celine::Theme::danger());

    // Re-read from the binary: Assets::tint is destructive, so re-tinting would tint the
    // previous result.
    logo = Celine::Assets::drawable ("logo.svg");

    // headerText(): every white mark in the chrome shares it.
    if (logo != nullptr)
        Celine::Assets::tint (*logo, Celine::Theme::headerText());

    // The one dropdown on a light panel.
    channelModeBox.setColour (juce::ComboBox::backgroundColourId, Celine::Theme::accent());
    channelModeBox.setColour (juce::ComboBox::textColourId, Celine::Theme::textOnPanel());
    channelModeBox.setColour (juce::ComboBox::arrowColourId, Celine::Theme::textOnPanel());

    // The bottom band is on the light panel: use the panel's ink.
    for (auto* label : { &inputLabel, &outputLabel, &channelModeLabel })
        label->setColour (juce::Label::textColourId, Celine::Theme::textOnPanel());

    refreshRebuildButtonColour();
}

void PluginEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // First: applyColours reads colours back out of it. Re-reads every colour JUCE was
    // told (see PluginLookAndFeel::applyPalette).
    lookAndFeel.applyPalette();

    applyColours();

    // Let every child that cached a colour take it again (lookAndFeelChanged()).
    sendLookAndFeelChange();

    repaint();
}

void PluginEditor::showAboutDialog()
{
    // The shared About window; this plugin's content comes from ProductInfo.h.
    showAboutWindow (this);
}

// ==============================================================================
void PluginEditor::paint (juce::Graphics& g)
{
    // The frame: one colour from the toolbar down the rulers to the inspector.
    g.fillAll (Celine::Theme::headerBackground());

    // The shared housing for undo and redo, with a divider between them.
    if (! undoRedoHousing.isEmpty())
    {
        const auto housing = undoRedoHousing.toFloat().reduced (Celine::Theme::borderWidth * 0.5f);

        // Fill only, dimmed when neither undo nor redo is available.
        g.setColour (history.canUndo() || history.canRedo()
                         ? Celine::Theme::button()
                         : Celine::Theme::button().withMultipliedAlpha (0.5f));
        g.fillRoundedRectangle (housing, Celine::Theme::cornerRadius);
        g.drawLine (housing.getCentreX(), housing.getY() + 1.0f,
                    housing.getCentreX(), housing.getBottom() - 1.0f, 1.0f);
    }

    if (logo != nullptr && ! logoBounds.isEmpty())
        logo->drawWithin (g, logoBounds.toFloat(), juce::RectanglePlacement::centred, 1.0f);
}

void PluginEditor::layOutToolbar (juce::Rectangle<int>& area)
{

    // --------------------------------------------------------------------------
    // Toolbar
    auto toolbar = area.removeFromTop (Celine::Theme::toolbarHeight).reduced (6, 6);

    // Every button 33 px square on a 40 px pitch; groups separated by a wider gap.
    const int size = Celine::Theme::buttonSize;
    const int gap = Celine::Theme::buttonGap;
    const int groupGap = 16;

    auto place = [&toolbar] (juce::Component& c)
    {
        c.setBounds (toolbar.removeFromLeft (size).withSizeKeepingCentre (size, size));
        toolbar.removeFromLeft (gap);
    };

    // One unbroken pitch from Select to Redo.
    place (*selectToolButton);
    place (*deleteToolButton);

    // Mirror, flip, rotate, copy.
    for (const auto action : { Action::Mirror, Action::Flip, Action::Rotate, Action::Copy })
        place (actionButton (action));

    // Undo and redo abut inside one housing painted by the editor.
    undoRedoHousing = toolbar.removeFromLeft (size * 2).withSizeKeepingCentre (size * 2, size);
    actionButton (Action::Undo).setBounds (undoRedoHousing.withWidth (size));
    actionButton (Action::Redo).setBounds (undoRedoHousing.withTrimmedLeft (size));

    // --------------------------------------------------------------------------
    // The right-hand cluster, laid out from the far corner inwards.
    if (logo != nullptr)
    {
        // A wider margin beside the wordmark.
        toolbar.removeFromRight (10);

        const auto ink = logo->getDrawableBounds();
        const float aspect = ink.getHeight() > 0.0f ? ink.getWidth() / ink.getHeight() : 3.0f;
        const int logoHeight = 28;
        const int logoWidth = juce::roundToInt (logoHeight * aspect);
        logoBounds = toolbar.removeFromRight (logoWidth).withSizeKeepingCentre (logoWidth, logoHeight);
        toolbar.removeFromRight (16);
    }

    settingsButton->setBounds (toolbar.removeFromRight (size).withSizeKeepingCentre (size, size));
    toolbar.removeFromRight (gap);
    rebuildButton.setBounds (toolbar.removeFromRight (Celine::Theme::rebuildWidth)
                                 .withSizeKeepingCentre (Celine::Theme::rebuildWidth, size));
    toolbar.removeFromRight (gap);
    bypassButton->setBounds (toolbar.removeFromRight (size).withSizeKeepingCentre (size, size));

    // --------------------------------------------------------------------------
    // Save, Load, the preset field and Import, centred between the two end groups.
    {
        // Only what is left between the end groups, never more. Change the gap below if this
        // looks wrong: it comes off before the field is sized.
        const int sideMargin = groupGap - gap;

        // Save and Load before the field, Import after.
        constexpr int buttonsInGroup = 3;
        const int available = toolbar.getWidth() - buttonsInGroup * (size + gap) - 2 * sideMargin;
        const int fieldWidth = juce::jlimit (0, Celine::Theme::presetWidth, available);
        const int groupWidth = buttonsInGroup * (size + gap) + fieldWidth;

        // Hide the field when too narrow to read; the buttons stay.
        presetsButton.setVisible (fieldWidth >= 56);

        toolbar.removeFromLeft (juce::jmax (sideMargin, (toolbar.getWidth() - groupWidth) / 2));

        place (*saveButton);
        place (*loadButton);
        presetsButton.setBounds (toolbar.removeFromLeft (fieldWidth)
                                     .withSizeKeepingCentre (fieldWidth, size));

        // The folder icon at the field's right end; the field keeps that space clear.
        {
            constexpr int inset = 3;
            const auto end = presetsButton.getBounds().reduced (inset);
            const int icon = end.getHeight();

            presetFolderButton->setBounds (end.withLeft (end.getRight() - icon));
            presetFolderButton->setVisible (presetsButton.isVisible());
            presetsButton.setTrailingSpace (icon + inset);
            presetLibrary.setFieldTrailingSpace (icon + 2 * inset);
        }

        // Only the gap: removeFromLeft above already took the field's room.
        toolbar.removeFromLeft (gap);
        place (*importButton);
    }

    // --------------------------------------------------------------------------
    // Control strip along the bottom
}

void PluginEditor::layOutPanels (juce::Rectangle<int> area)
{
    using Strip = Celine::ControlStrip;
    constexpr int margin = Celine::Theme::floatingMargin;

    // The sheet takes everything below the toolbar; the band floats over its bottom edge.
    canvas.setBounds (area);

    bandRow = area.withTrimmedLeft (Celine::SchematicCanvas::leftRulerThickness)
                  .reduced (margin)
                  .removeFromBottom (Strip::preferredHeight);
    bandTop = bandRow.getY();

    // The panels first: the band's end bars line up under them.
    layOutFloatingPanels();
}

void PluginEditor::layOutBand()
{
    using Strip = Celine::ControlStrip;
    constexpr int margin = Celine::Theme::floatingMargin;

    const auto area = canvas.getBounds();
    auto row = bandRow;

    if (area.isEmpty() || row.isEmpty())
        return;

    // Caption over control, as ControlStrip::layOutCell does, so all knobs share a
    // baseline.
    auto placeKnob = [] (juce::Rectangle<int> cell, juce::Slider& knob, juce::Label& label)
    {
        knob.setBounds (Strip::layOutCell (cell, &label, Strip::knobSize + Strip::valueHeight)
                            .withSizeKeepingCentre (Strip::cellWidth - 4, Strip::knobSize + Strip::valueHeight));
    };

    // INPUT on a bar at the left, ending where the folded palette ends; OUTPUT and the
    // channel picker on a bar at the right, starting under the inspector. Never narrower
    // than their contents, which sit against the inner edge at the strip's padding;
    // spare width goes to the window edge.
    const int inputContent = Strip::cellWidth + 2 * Strip::padding;
    // The folded palette's edge (not its tab), folded or not, so opening the palette
    // doesn't move the band.
    const int paletteEdge = palette.getX() + Celine::ElementPalette::getFoldedBodyWidth();
    const int inputRight = juce::jmax (row.getX() + inputContent, paletteEdge);
    const auto inputArea = row.removeFromLeft (juce::jmin (row.getWidth(), inputRight - row.getX()));
    inputBar.setBounds (inputArea.withLeft (area.getX()));
    placeKnob (inputArea.withTrimmedLeft (inputArea.getWidth() - inputContent).reduced (Strip::padding, 4),
               inputSlider, inputLabel);

    constexpr int channelsWidth = Strip::comboWidth + 12;
    const int outputContent = Strip::cellWidth + channelsWidth + 2 * Strip::padding;
    // The inspector's left edge, whether or not it is showing, so the bar never moves.
    const int outputLeft = juce::jmin (row.getRight() - outputContent, inspector.getX());
    const auto outputArea = row.removeFromRight (juce::jlimit (0, row.getWidth(), row.getRight() - outputLeft));
    outputBar.setBounds (outputArea.withRight (area.getRight()));
    auto fixed = outputArea.withWidth (outputContent).reduced (Strip::padding, 4);

    channelModeBox.setBounds (
        Strip::layOutCell (fixed.removeFromRight (channelsWidth), &channelModeLabel,
                           Strip::comboHeight)
            .withSizeKeepingCentre (Strip::comboWidth, Strip::comboHeight));

    placeKnob (fixed, outputSlider, outputLabel);

    // The circuit's controls: a bar as wide as they are, centred on the window if there
    // is room, else between the ends; it scrolls when full. None if there are no
    // controls.
    const auto between = row.reduced (margin, 0);
    const int width = juce::jmin (controlStrip.getPreferredWidth(), between.getWidth());
    const int centred = area.getCentreX() - width / 2;
    const int x = juce::jlimit (between.getX(), juce::jmax (between.getX(), between.getRight() - width), centred);

    controlStrip.setVisible (! controlStrip.isEmpty() && width > 0);
    controlStrip.setBounds (x, row.getY(), juce::jmax (0, width), row.getHeight());
}

void PluginEditor::layOutFloatingPanels()
{
    const auto sheet = canvas.getBounds();

    if (sheet.isEmpty())
        return;

    constexpr int margin = Celine::Theme::floatingMargin;

    // The console always keeps room for one line.
    constexpr int consoleLine = 43;

    // The panels' area: the sheet less its rulers and a margin.
    const auto room = sheet.withTrimmedTop (Celine::SchematicCanvas::topRulerThickness)
                          .withTrimmedLeft (Celine::SchematicCanvas::leftRulerThickness)
                          .reduced (margin)
                          .withBottom (bandTop - margin);

    if (room.getWidth() <= 0 || room.getHeight() <= 0)
    {
        layOutBand();
        return;
    }

    // --------------------------------------------------------------------------
    // The palette down the left and the inspector down the right, both from the top.
    const auto wanted = inspector.getPreferredSize();
    const auto tallPalette = palette.getPreferredSize (room.getHeight());
    const int paletteFootprint = juce::jmin (tallPalette.x, room.getWidth() / 2);

    const int columnWidth = juce::jmin (room.getRight() - (room.getX() + paletteFootprint) - margin,
                                        Celine::Theme::inspectorWidth);
    const auto column = room.withTrimmedLeft (room.getWidth() - columnWidth);

    // The console goes between the two; the minimum window width guarantees room. If a
    // host ignores the minimum, the palette stops short and the console goes under it.
    const int betweenWidth = column.getX() - margin - (room.getX() + paletteFootprint + margin);
    const bool consoleBetween = betweenWidth >= Celine::MessageConsole::minimumWidth;

    const auto paletteSize = consoleBetween ? tallPalette
                                            : palette.getPreferredSize (room.getHeight() - margin - consoleLine);
    // Both centred vertically in their room.
    const int paletteRoom = consoleBetween ? room.getHeight() : room.getHeight() - margin - consoleLine;
    palette.setBounds (room.getX(), room.getY() + juce::jmax (0, (paletteRoom - paletteSize.y) / 2),
                       juce::jmin (paletteSize.x, room.getWidth() / 2), paletteSize.y);

    const int inspectorWidth = juce::jmin (wanted.x, column.getWidth());
    const int inspectorHeight = juce::jmin (wanted.y, column.getHeight());
    inspector.setBounds (column.getRight() - inspectorWidth, column.getY() + (column.getHeight() - inspectorHeight) / 2,
                         inspectorWidth, inspectorHeight);

    // --------------------------------------------------------------------------
    // The console at the top, as big as its messages, centred on the sheet if the panels
    // allow, else between them.
    {
        const auto span = consoleBetween ? room.withLeft (palette.getRight() + margin)
                                                .withRight (column.getX() - margin)
                                         : room.withRight (column.getX() - margin)
                                                .withTop (palette.getBottom() + margin);

        const auto size = console.getPreferredSize (span.getWidth());
        const int width = juce::jmin (size.x, span.getWidth());
        const int height = juce::jmin (size.y, span.getHeight(), juce::jmax (sheet.getHeight() / 3, consoleLine));

        const int centre = sheet.getCentreX();
        const int halfRoom = juce::jmin (centre - span.getX(), span.getRight() - centre);
        const int x = width <= 2 * halfRoom ? centre - width / 2 : span.getCentreX() - width / 2;

        // At the top between the panels, or under the palette in the fallback.
        console.setBounds (x, consoleBetween ? room.getY() : room.getBottom() - height, width, height);
    }

    // --------------------------------------------------------------------------
    // Fit-to-screen frames the drawing in what the palette and inspector leave
    // uncovered (the console is ignored).
    canvas.setFramingInsets ({ Celine::SchematicCanvas::topRulerThickness,
                               palette.getRight() - sheet.getX(),
                               sheet.getBottom() - bandTop,
                               inspector.isVisible() ? sheet.getRight() - inspector.getX() : margin });

    layOutBand();
}

int PluginEditor::narrowestWindow() const
{
    constexpr int margin = Celine::Theme::floatingMargin;

    // The ruler, palette, console at its narrowest and the inspector, with margins.
    const int floating = Celine::SchematicCanvas::leftRulerThickness
                       + margin + palette.getMaximumWidth()
                       + margin + Celine::MessageConsole::minimumWidth
                       + margin + Celine::Theme::inspectorWidth + margin;

    // Never narrower than the floor.
    return juce::jmax (floating, smallestWidth);
}

void PluginEditor::resized()
{
    // Store the size for the next window, here since a host may save while the editor is
    // open. Only sizes at or above the minimum: hosts may shrink an editor while closing
    // it.
    if (getWidth() >= minimumWidth && getHeight() >= minimumHeight)
    {
        processorRef.editorWidth = getWidth();
        processorRef.editorHeight = getHeight();
    }

    auto area = getLocalBounds();
    layOutToolbar (area);
    layOutPanels (area);

    // Kept under the field.
    if (isPresetLibraryShowing())
        placePresetLibrary();
}
