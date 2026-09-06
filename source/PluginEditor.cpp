#include "PluginEditor.h"

// Included unconditionally rather than behind JucePlugin_Build_Standalone,
// which is not the discriminator it looks like: it is defined for every target
// built from a FORMATS list containing Standalone, the test runner included.
// getInstance() returns null when no standalone holder exists, which is the
// answer a host wants anyway.
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "Schematic/ExampleSchematics.h"

#include "ui/EmbeddedAssets.h"
#include "ui/AboutPanel.h"
#include "ui/ThemePanel.h"

namespace
{
    /** One character, not three full stops. The house sets it in the menus, and a
        run of periods is a different glyph at a different width. */
    const juce::String ellipsis = juce::String::fromUTF8 ("\xe2\x80\xa6");
}


//==============================================================================
PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), canvas (p.getSchematic()), controlStrip (p)
{
    setLookAndFeel (&lookAndFeel);

    // A tooltip paints a rounded panel, so it must not be opaque -- an opaque component
    // has to fill every pixel it owns, and the four corners outside the rounding are
    // exactly the ones it does not paint; left opaque they came out as square spikes of
    // whatever was in the buffer. TooltipWindow sets the flag in its constructor and
    // offers no way to ask otherwise. Safe because this one is parented to the editor
    // rather than put on the desktop, so what shows through the corners is this window.
    tooltips.setOpaque (false);

    // The palette is one per process, so a colour changed in any window moves every
    // window in the session -- which is what somebody picking a colour expects.
    Celine::Theme::palette().addChangeListener (this);

    // Once, here, rather than on a timer: another instance may have saved a theme since
    // this module last looked, and a window opening is the moment that can matter. The
    // disk is not touched again unless somebody asks it to be.
    Celine::Theme::palette().refreshFromDisk();

    // Standalone only: JUCE's own Options menu and Audio/MIDI dialog are never
    // our children, so they resolve against the *default* look and feel -- which
    // inside a DAW belongs to the host.
    if (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    buildToolbar();
    buildPanels();
    buildBottomBand();

    // After the children exist and before the window is sized: applyColours reaches
    // into them, and resized() runs off the back of setSize further down.
    applyColours();

    // A rebuilt editor catches up with what is already loaded. Without this the sheet
    // came back and the field above it did not, which reads as the preset having been
    // lost rather than as the window having been reopened.
    lastCircuitFile = processorRef.presetFile;
    showPresetFromProcessor();

    // A preset load or a host restoring a session replaces the drawing under us.
    // Bounced through the message thread, since some hosts restore state from
    // another one.
    processorRef.onSchematicReplaced = [safe = juce::Component::SafePointer<PluginEditor> (this)]
    {
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr)
                safe->schematicChangedExternally();
        });
    };

    // The plugin may have been playing, and being automated, with no editor
    // attached, so the parameters have moved on and the drawing has not heard.
    // Settle that before the strip is built, or the refresh below snaps an
    // automated knob back.
    processorRef.adoptControlPositions();

    controlStrip.refresh();
    updateToolButtons();

    // The baseline: undo must not rewind past what the editor opened onto.
    history.reset (processorRef.getSchematic().toValueTree());
    updateActionButtons();

    setResizable (true, true);
    const int minWidth = minimumWidth;
    const int minHeight = minimumHeight;

    // Read before setResizeLimits, not after. That call constrains the bounds it
    // finds -- still 0x0 here -- up to the minimum, and that fires resized(), which
    // writes the size back to the processor. Reading afterwards returns the minimum
    // it has just written, so the window opened at its smallest size every time and
    // the size you left it at was never restored.
    const auto storedWidth = processorRef.editorWidth.load();
    const auto storedHeight = processorRef.editorHeight.load();

    setResizeLimits (minWidth, minHeight, 4000, 3000);

    // Clamped rather than trusted: a size from a state blob written by an older
    // build, or on a bigger screen, must not produce a window you cannot use.
    setSize (juce::jlimit (minWidth, 4000, storedWidth),
             juce::jlimit (minHeight, 3000, storedHeight));

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PluginEditor> (this)]
    {
        if (safe != nullptr)
            safe->canvas.zoomToFit();
    });

    // The first-run question is not asked from here: it waits for the timer to
    // see a window genuinely on screen. See offerPresetFolderOnFirstRun.
    startTimerHz (15);
}

void PluginEditor::buildToolbar()
{
    settingsButton = std::make_unique<Celine::IconButton> (
        "Settings", Celine::Assets::drawable ("gear-solid-full.svg"));

    // Assets are looked up by *filename*. Asking for the C++ identifier JUCE
    // derives instead fails silently -- it strips hyphens rather than replacing
    // them, so the lookup returns null and the button draws as an empty
    // rectangle. See EmbeddedAssets.h.
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

    // The house mark is loaded and tinted in applyColours, not here: tinting is
    // destructive, so a theme change has to start again from the artwork.

    // Undo and redo share one housing, painted by the editor behind them.
    actionButton (Action::Undo).setDrawsFrame (false);
    actionButton (Action::Redo).setDrawsFrame (false);

    // None of these may hold the keyboard: the canvas needs it for the part and
    // tool shortcuts, and a toolbar button that takes focus on click silently
    // disables them until the next click on the sheet.
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
    presetsButton.setTooltip ("The circuit that is loaded, and the presets you can load. "
                              "A dot beside the name means it has been edited since.");
    presetsButton.onClick     = [this] { showPresetsMenu(); };
    settingsButton->onClick   = [this] { showSettingsMenu(); };
    rebuildButton.setTooltip ("Hands the drawing to the audio engine. Nothing you draw is "
                              "heard until this is pressed, which is why it turns amber "
                              "while the sheet is ahead of what you are listening to.");
    rebuildButton.onClick     = [this] { rebuildCircuit(); };

    // The canvas changes tool on its own -- placing a part drops it back to
    // Select -- so the toolbar follows the canvas rather than the reverse.
    canvas.onToolChanged = [this] { updateToolButtons(); };
}

void PluginEditor::buildPanels()
{
    addAndMakeVisible (palette);
    addAndMakeVisible (canvas);
    addAndMakeVisible (inspector);
    addAndMakeVisible (console);

    // Clicking a message selects the part it names.
    console.onMessageClicked = [this] (int elementId)
    {
        canvas.selectElement (elementId);
        inspector.setElement (canvas.getSelectedElement());
    };

    palette.onTypeChosen = [this] (SchematicModel::ElementType type) { canvas.setPendingType (type); };
    palette.onWireChosen = [this] { canvas.setTool (Celine::SchematicCanvas::Tool::Wire); };

    canvas.onSchematicChanged = [this]
    {
        recordUndoState();
        markPending();
        inspector.setElement (canvas.getSelectedElement());
    };

    // The end of a drag, so the next one starts a fresh undo step.
    canvas.onGestureEnd = [this] { gestureRecorded = false; };

    // Fetched on demand rather than pushed: the canvas draws a schematic and
    // knows nothing about audio, so it asks for a snapshot of some numbers.
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

    // Retiming a probe needs no rebuild -- the circuit has not changed -- but it
    // does change how many samples go into a column, which the audio thread has
    // to be told about.
    inspector.onScopeTimebaseChanged = [this] { processorRef.refreshScopeTiming(); };
    inspector.scopeReader = canvas.scopeReader;

    inspector.onEdited = [this] { recordUndoState(); markPending(); canvas.repaint(); };

    // A pot's position and a switch's throw are the two things the inspector can
    // change that the running circuit already follows, so they reach the strip
    // now rather than lighting Rebuild for a change you can already hear.
    inspector.onControlEdited = [this]
    {
        if (const auto* element = canvas.getSelectedElement())
            controlStrip.pushPositionFor (element->id);

        markPresetModified();
        canvas.repaint();
    };

    inspector.onCabChanged = [this] { applyCabinetChange(); };
    inspector.onCabFileRequested = [this] { browseForCabinetFile(); };
    inspector.onRotateRequested = [this] { canvas.rotate(); };
    inspector.onFlipRequested = [this] { canvas.flip(); };
    inspector.onDeleteRequested = [this] { canvas.deleteSelection(); };
}

void PluginEditor::buildBottomBand()
{
    inputSlider.setTooltip ("Level into the circuit. Valves and diodes answer to how hard "
                            "they are driven, so this sets how the circuit behaves and not "
                            "only how loud it is.");
    outputSlider.setTooltip ("Level out, after the circuit. Use it to match the bypassed "
                             "level once the input has been set where you want it.");

    for (auto* slider : { &inputSlider, &outputSlider })
    {
        slider->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);

        // Drawn differently from the drawn circuit's own knobs, which share this
        // band; deaf to the wheel, as those are.
        slider->getProperties().set (digitalGainProperty, true);
        slider->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider->setScrollWheelEnabled (false);

        addAndMakeVisible (slider);
    }

    bypassButton = std::make_unique<Celine::PowerButton> (
        "Bypass", Celine::Assets::drawable ("power-off-solid-full.svg"));
    bypassButton->setWantsKeyboardFocus (false);
    addAndMakeVisible (*bypassButton);

    channelModeBox.setTooltip ("How many circuits run, and what feeds them. Stereo simulates "
                               "the drawing twice, once per channel; the mono settings "
                               "simulate it once, from the left, the right, or the two summed.");
    channelModeBox.addItemList ({ "Stereo", "Mono L", "Mono R", "Mono L+R" }, 1);
    channelModeBox.setWantsKeyboardFocus (false);

    addAndMakeVisible (channelModeBox);

    addAndMakeVisible (controlStrip);

    // The strip owns the widgets and the parameters; what a moved control means
    // for the drawing is the editor's business, since it owns the sheet.
    controlStrip.onControlMoved = [this] (const std::vector<int>& ids, float position)
    { writeControlPositionToSchematic (ids, position); };

    for (auto* label : { &inputLabel, &outputLabel, &channelModeLabel })
    {
        label->setFont (Celine::Fonts::light (15.0f));
        label->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (label);
    }

    inputAttachment  = std::make_unique<SliderAttachment> (processorRef.apvts, "input",  inputSlider);
    outputAttachment = std::make_unique<SliderAttachment> (processorRef.apvts, "output", outputSlider);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, "bypass", *bypassButton);
    channelModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processorRef.apvts, "channels", channelModeBox);
}

PluginEditor::~PluginEditor()
{
    processorRef.onSchematicReplaced = nullptr;

    Celine::Theme::palette().removeChangeListener (this);

    // Before the children go: a component holding a dangling LookAndFeel is a
    // crash on the way out.
    setLookAndFeel (nullptr);

    // Only if we were the ones holding it.
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

    // Whatever loadPresetFile() left for us, or nothing at all when the drawing
    // was replaced by something with no file behind it -- a host restoring its
    // session, usually. Consumed either way, so the next replacement starts
    // from a clean slate rather than inheriting this one's name.
    setLoadedPreset (presetBeingLoaded);
    presetBeingLoaded = juce::File{};

    // A new document, so the old history no longer describes anything reachable.
    history.reset (processorRef.getSchematic().toValueTree());
    updateActionButtons();

    pendingRebuild = false;
    refreshRebuildButtonColour();
    setStatus ("Loaded.", false);

    // At the moment the import lands, not as a line in the build's notes. This
    // callback is the one point every route in passes through -- Load, the
    // preset menu, a host restoring its session -- so it fires once per
    // document. A dialog rather than the console because it is a fact about the
    // *file*: what is on screen may not be what was drawn.
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

    // Placing and wiring are both shown by the palette rather than the toolbar,
    // and the canvas is the authority on both -- either can be armed from the
    // keyboard as well as by clicking, so the highlight follows the canvas.
    const auto armed = canvas.getPendingType();
    palette.setActive (tool == Tool::Place ? &armed : nullptr, tool == Tool::Wire);
}

void PluginEditor::setLoadedPreset (const juce::File& file)
{
    // A file means a preset; File{} means the sheet came from somewhere with no
    // file behind it -- an example, a cleared sheet, or a host session.
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
    // No file behind it, so Save must not think it can overwrite anything --
    // lastCircuitFile stays where it was and the dialog opens where it did.
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
    // Called *after* the edit, so the history holds the state just reached and
    // the one before it is whatever it held already. See SchematicHistory.
    auto now = processorRef.getSchematic().toValueTree();

    // A drag is one step: the canvas says when the gesture ends, so the run of
    // notifications between mouseDown and mouseUp collapses into one record.
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

    // Undo is for the drawing, not the knobs: a snapshot carries every pot
    // position, so restoring one would rewind those behind the automation lane's
    // back -- and only on the drawing, the parameters being the live truth.
    // Seeding back off the parameters is what keeps them where they were.
    processorRef.adoptControlPositions();

    // The drawing changed but the running circuit has not: undoing is an edit
    // like any other, so it lights Rebuild rather than silently rebuilding and
    // interrupting whatever is playing.
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

    // As in undo: the sheet comes back, the knobs stay put.
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

    for (const auto action : { Action::Mirror, Action::Flip, Action::Copy, Action::Rotate })
        actionButton (action).setEnabled (hasSelection
                                         || canvas.getTool() == Celine::SchematicCanvas::Tool::Place);

    // setEnabled repaints the button it is called on, but the undo/redo housing
    // is painted by the editor, so nothing above would bring it up to date.
    repaint (undoRedoHousing);
}

void PluginEditor::markPending()
{
    // The drawing no longer matches the file it came from. Tracked separately
    // from pendingRebuild, which is about what you can *hear* -- a rebuilt sheet
    // still differs from the preset on disk until it is saved.
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
        // The previous circuit is still running, and the reason is already the
        // first diagnostic, so the status line does not repeat it.
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

    // Every part on the shaft: a ganged pair turns as one, and both wipers
    // redraw.
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

    // A switch draws itself open or closed, so the sheet redraws when one is
    // thrown from the strip.
    canvas.repaint();

    // And the inspector is showing one of these parts' fields if it happens to
    // be selected, so it would otherwise sit there contradicting the strip.
    if (auto* selected = canvas.getSelectedElement(); selected != nullptr
        && std::find (elementIds.begin(), elementIds.end(), selected->id) != elementIds.end())
        inspector.setElement (selected);
}

void PluginEditor::timerCallback()
{
    // Backstop: the real attempt happens in parentHierarchyChanged, before the
    // window is shown. This catches being parented to something that was not
    // yet the window.
    adoptNativeTitleBar();

    if (! presetFolderChecked && isShowing())
    {
        presetFolderChecked = true;
        offerPresetFolderOnFirstRun();
    }

    // Keep switch buttons showing what the parameter actually says, since they
    // have no attachment to do it for them.
    controlStrip.syncToggles();

    // A live trace has to be asked for again to move -- and only when there is
    // one, or the editor repaints the whole sheet several times a second for
    // nothing.
    if (processorRef.getSchematic().countElementsOfType (SchematicModel::ElementType::Scope) > 0)
        canvas.repaint();

    // Not gated on that: they are small, they decline in a line when nothing is
    // selected, and a part's current is live on sheets with no scope at all.
    inspector.repaintReadouts();
}

//==============================================================================
void PluginEditor::setStatus (const juce::String& message, bool isError)
{
    // One place for everything the plugin has to say.
    console.setHeadline (message, isError);
}

bool PluginEditor::readScopeTrace (int elementId, Celine::ScopeReading& out) const
{
    const auto* trace = processorRef.getScopeTrace (elementId);

    if (trace == nullptr)
        return false;

    // Copied out rather than pointed at: a renderer holding a pointer would see
    // a column change between drawing the line into it and the line out of it.
    // The index is read first, with acquire ordering, so every column it claims
    // is complete has actually been written.
    out.writeColumn = trace->writeColumn.load (std::memory_order_acquire);

    for (int c = 0; c < Celine::ScopeReading::columns; ++c)
    {
        out.minimum[c] = trace->minimum[c].load (std::memory_order_relaxed);
        out.maximum[c] = trace->maximum[c].load (std::memory_order_relaxed);
    }

    out.dcAverage = trace->dcAverage.load (std::memory_order_relaxed);
    out.peakToPeak = trace->peakToPeak.load (std::memory_order_relaxed);
    out.live = trace->live.load (std::memory_order_relaxed);

    // The axes come off the *drawing*, not off the trace: they are a way of
    // looking at the circuit rather than anything the circuit did, so they
    // change the moment they are typed and never wait for a rebuild.
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

    // Refused exactly the way every other route in refuses -- see
    // loadPresetFile. A file with the right suffix that isn't ours has to leave
    // the drawing alone rather than merge half of itself into it, which unlike a
    // failed *load* would leave the sheet in a state nobody drew.
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

    // Counted before the merge so the warning below can talk about what the
    // import *brought*, rather than about what the sheet now has.
    const int inputsBefore = sheet.countElementsOfType (SchematicModel::ElementType::Input);
    const int outputsBefore = sheet.countElementsOfType (SchematicModel::ElementType::Output);

    // Clear to the right of everything already drawn: most sheets are near the
    // origin, and landing two of those on each other is unreadable.
    constexpr int gapInSquares = 4;

    const auto existing = sheet.getContentBounds();
    const auto arriving = incoming.getContentBounds();

    const auto delta = existing.isEmpty()
                         ? juce::Point<int> {}
                         : juce::Point<int> { existing.getRight() + gapInSquares - arriving.getX(),
                                              existing.getY() - arriving.getY() };

    const auto merged = sheet.merge (incoming, delta);

    // Selected as one block, so the next drag puts it where it belongs.
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

    // The one thing an import can quietly get wrong. A terminal *names its net*,
    // so a second Input does not sit downstream of the first -- it is the same
    // node, and the two circuits end up side by side across one input rather
    // than one feeding the other. Ground is the exception and the reason this is
    // a warning rather than a rule: grounds are *meant* to merge.
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

        // Asked for again rather than captured. The dialog is asynchronous, and
        // between opening it and answering it the selection can have moved, the
        // sheet can have been replaced by a preset load, or the part can have
        // been deleted -- a captured Element* would be a pointer into any of
        // those.
        auto* selected = canvas.getSelectedElement();

        if (selected == nullptr || selected->type != SchematicModel::ElementType::Output)
            return;

        lastCabinetFile = file;
        selected->cabFile = file.getFullPathName();

        // Choosing a file is a clear enough statement of intent to arm the
        // switch: nobody goes looking for an impulse response in order to leave
        // it switched out.
        selected->cabEnabled = true;

        inspector.setElement (selected);
        applyCabinetChange();
    });
}

void PluginEditor::applyCabinetChange()
{
    // Undoable, because the file and the switch live on the part and travel in
    // the document -- but emphatically *not* pending, because nothing about a
    // cabinet reaches the matrix. Pressing Rebuild would rebuild a circuit that
    // has not changed.
    recordUndoState();
    markPresetModified();

    const auto problem = processorRef.refreshCabinet();

    // Never an error. A missing impulse response is a normal thing to happen to
    // a sheet that has travelled between machines -- the drawing is fine, the
    // setting is kept, and the signal goes through without it.
    setStatus (problem, false);
}

void PluginEditor::browseForCircuit (bool saving)
{
    // The two dialogs differ only in title, flags and what to do with the
    // answer. Two copies would be two places for "where does it open" to drift.
    fileChooser = std::make_unique<juce::FileChooser> (
        saving ? "Save circuit" : "Load circuit",
        lastCircuitFile != juce::File{} ? lastCircuitFile
                                       : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        juce::String ("*") + PluginProcessor::circuitFileExtension);

    const auto flags = juce::FileBrowserComponent::canSelectFiles
                     | (saving ? juce::FileBrowserComponent::saveMode
                                     | juce::FileBrowserComponent::warnAboutOverwriting
                               : juce::FileBrowserComponent::openMode);

    fileChooser->launchAsync (flags, [this, saving] (const juce::FileChooser& chooser)
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
            // Saving is how an edited sheet becomes a preset again.
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

    // Set before restoreDocument, because that is what queues the callback that
    // reads it.
    presetBeingLoaded = file;

    // A file that isn't ours must leave the running circuit alone rather than
    // half-load and take the sound with it. That goes for a preset picked off
    // the menu as much as one picked out of a dialog -- a folder is just a
    // folder, and anything at all can be sitting in it with the right suffix.
    if (xml == nullptr || ! processorRef.restoreDocument (juce::ValueTree::fromXml (*xml)))
    {
        // Nothing was replaced, so no callback is coming to consume this.
        presetBeingLoaded = juce::File{};
        setStatus (file.getFileName() + " isn't a circuit file.", true);
        return false;
    }

    // After restoreDocument, not before: schematicChangedExternally() fires from
    // in there and clears the name, since it can't tell a preset load from a
    // host restoring a session. This puts the name back for the case that does
    // have a file behind it.
    setLoadedPreset (file);

    // restoreDocument() rebuilt and announced itself, so the canvas and the
    // control strip have already caught up.
    setStatus ("Loaded " + file.getFileName(), false);
    return true;
}

//==============================================================================
namespace
{
    // Three id ranges in one menu, kept well apart so a folder with a lot of
    // presets in it can never collide with a command.
    constexpr int userPresetBaseId = 1;
    constexpr int factoryPresetBaseId = 50000;
    constexpr int choosePresetFolderId = 100001;
    constexpr int revealPresetFolderId = 100002;
    constexpr int clearSheetId = 100003;
} // namespace

void PluginEditor::showPresetsMenu()
{
    juce::PopupMenu menu;

    // Factory first: on a fresh install it is the only thing in here.
    const auto factory = SchematicModel::Examples::getNames();

    menu.addSeparator();
    menu.addItem (clearSheetId, "New (empty sheet)");

    menu.addSectionHeader ("Factory presets");

    for (int i = 0; i < factory.size(); ++i)
        menu.addItem (juce::PopupMenu::Item (factory[i])
                          .setID (factoryPresetBaseId + i)
                          .setTicked (presetsButton.isFactoryPreset()
                                      && factory[i] == presetsButton.getPresetName()));

    // Read fresh every time: a preset folder is an ordinary folder that files
    // get dropped into behind our back.
    const auto files = presets.getPresets();
    const bool haveFolder = presets.hasDirectory();

    menu.addSectionHeader ("User presets");

    if (! haveFolder)
    {
        menu.addItem (juce::PopupMenu::Item ("No preset folder chosen yet").setEnabled (false));
    }
    else if (files.isEmpty())
    {
        menu.addItem (juce::PopupMenu::Item ("Nothing in " + presets.getDirectory().getFileName())
                          .setEnabled (false));
    }
    else
    {
        for (int i = 0; i < files.size(); ++i)
        {
            const auto name = files[i].getFileNameWithoutExtension();

            // Ticked, so the menu agrees with the button.
            menu.addItem (juce::PopupMenu::Item (name)
                              .setID (userPresetBaseId + i)
                              .setTicked (! presetsButton.isFactoryPreset()
                                          && name == presetsButton.getPresetName()));
        }
    }

    menu.addSeparator();
    menu.addItem (choosePresetFolderId, haveFolder ? "Change preset folder..." : "Choose preset folder...");

    if (haveFolder)
        menu.addItem (revealPresetFolderId, "Reveal preset folder");

    // Menus inherit a look and feel from nothing: withTargetComponent says only
    // *where* to open, and a PopupMenu is a parentless desktop window, so
    // untold it resolves against the default -- the host's, inside a DAW.
    menu.setLookAndFeel (&lookAndFeel);

    // The listing goes into the callback by value: the menu is async, and the
    // folder can change under it between opening and choosing.
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetsButton),
                        [this, files, factory] (int choice)
    {
        if (choice == 0)
            return; // dismissed

        if (choice == choosePresetFolderId)
        {
            choosePresetFolder();
            return;
        }

        if (choice == revealPresetFolderId)
        {
            presets.getDirectory().revealToUser();
            return;
        }

        if (choice == clearSheetId)
        {
            processorRef.getSchematic().clear();
            setLoadedPreset ({});
            canvas.refresh();
            canvas.zoomToFit();
            inspector.setElement (nullptr);
            rebuildCircuit();
            return;
        }

        if (juce::isPositiveAndBelow (choice - factoryPresetBaseId, factory.size()))
        {
            loadFactoryPreset (choice - factoryPresetBaseId);
            return;
        }

        if (juce::isPositiveAndBelow (choice - userPresetBaseId, files.size()))
            loadPresetFile (files[choice - userPresetBaseId]);
    });
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

    // After rebuildCircuit, which does not touch the preset name, and after the
    // canvas refresh -- neither of which goes through restoreDocument, so no
    // async callback is coming to overwrite this.
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
                return; // cancelled, or a folder that went away while we asked

            presets.setDirectory (folder);

            const auto count = presets.getPresets().size();
            setStatus ("Preset folder: " + folder.getFullPathName() + "  --  "
                           + juce::String (count) + (count == 1 ? " preset" : " presets"),
                       false);
        });
}

void PluginEditor::parentHierarchyChanged()
{
    // As early as possible: the window exists but is not on screen, so the
    // native frame is there from the first paint.
    adoptNativeTitleBar();
}

namespace
{
    /** The themed cover for JUCE's muted-input bar: its yellow is painted by a
        private class whose paint() cannot be reached, so covering it is the only
        way to change it without forking JUCE. */
    class NotificationSkin : public juce::Component
    {
       public:
        NotificationSkin()
        {
            setOpaque (true);
            setInterceptsMouseClicks (false, false);
        }

        /** Stay the size of the bar being covered.

            The cover is a child JUCE knows nothing about, so its host never
            lays it out: sized once at construction it kept the width the window
            had then, and widening painted JUCE's yellow past that old edge.
            parentSizeChanged() is the hook JUCE already calls for this, so
            there is nothing to register or unregister. */
        void parentSizeChanged() override
        {
            if (auto* parent = getParentComponent())
                setBounds (parent->getLocalBounds());
        }

        void paint (juce::Graphics& g) override
        {
            using namespace Celine;

            g.fillAll (Theme::chrome());

            // A violet rule along the bottom, where JUCE draws a darkgoldenrod
            // one -- the bar is a piece of chrome above the toolbar, so it ends
            // the way the toolbar's own edges do.
            g.setColour (Theme::violet());
            g.fillRect (0, getHeight() - 2, getWidth(), 2);

            g.setColour (Theme::text());
            g.setFont (Fonts::light (14.0f));
            g.drawText ("Audio input is muted to avoid a feedback loop.",
                        getLocalBounds().reduced (12, 0),
                        juce::Justification::centredLeft, true);
        }
    };
} // namespace

void PluginEditor::adoptNativeTitleBar()
{
    if (nativeTitleBarChecked)
        return;

    // Only the standalone build owns its window; in a DAW the top-level window
    // is the host's. Hence the wrapper check rather than "is there a
    // DocumentWindow above me", which in a host there often is.
    if (processorRef.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
    {
        nativeTitleBarChecked = true;
        return;
    }

    auto* window = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent());

    // Not parented yet. Deliberately does *not* latch here, so the next call --
    // from the timer, if the hierarchy callback came too early -- gets another
    // go rather than leaving the window JUCE-framed forever.
    if (window == nullptr)
        return;

    nativeTitleBarChecked = true;

    styleStandaloneNotification();

    // Asked rather than assumed: the window outlives the editor, so this has to
    // cope with arriving at one that is already native.
    if (! window->isUsingNativeTitleBar())
        window->setUsingNativeTitleBar (true);

    // Going native collapses JUCE's Options button, so the Settings menu asks
    // StandalonePluginHolder for the audio device dialog directly.
}

void PluginEditor::styleStandaloneNotification()
{
    if (notificationSkin != nullptr)
        return;

    // The bar is a *sibling*: JUCE's content component holds the editor and the
    // notification area side by side.
    auto* content = getParentComponent();

    if (content == nullptr)
        return;

    for (auto* sibling : content->getChildren())
    {
        // By elimination, the type being private: JUCE's content component
        // holds exactly two children, this editor and the notification area.
        // Not by height as well -- the bar starts zero-sized, and the cover
        // grows with it in parentSizeChanged().
        if (sibling == this)
            continue;

        auto skin = std::make_unique<NotificationSkin>();
        skin->setBounds (sibling->getLocalBounds());
        sibling->addAndMakeVisible (*skin);

        // Brought in front of the cover and recoloured, which works where the
        // bar itself does not: a TextButton's colours are settable.
        for (auto* child : sibling->getChildren())
        {
            if (auto* button = dynamic_cast<juce::TextButton*> (child))
            {
                button->setColour (juce::TextButton::buttonColourId, Celine::Theme::surface());
                button->setColour (juce::TextButton::textColourOffId, Celine::Theme::text());
                button->setColour (juce::TextButton::textColourOnId, Celine::Theme::text());
                button->setLookAndFeel (&lookAndFeel);
                button->toFront (false);
            }
        }

        notificationSkin = std::move (skin);
        return;
    }
}

void PluginEditor::offerPresetFolderOnFirstRun()
{
    if (presets.hasDirectory() || presets.hasBeenOffered())
        return;

    // Only ever asked with a window in front of a person: editors get built
    // where nobody is looking -- the VST3 manifest helper, the test harness --
    // and the answer is remembered, so either would answer on the user's
    // behalf.
    jassert (isShowing());

    // Marked when the question goes up, not when it comes back: closing the
    // dialog without choosing is an answer.
    presets.markOffered();

    // Built rather than asked for: juce::AlertWindow is drawn by the platform's
    // look and feel and follows none of the palette.
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

            choose.setColour (juce::TextButton::buttonColourId, Celine::Theme::violet());
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

    // Async, because a plugin must never spin a modal loop inside its host.
    auto* window = options.launchAsync();

    const juce::Component::SafePointer<PluginEditor> safe (this);

    // exitModalState and nothing else: launchAsync sets deleteWhenDismissed, so
    // the window deletes itself once the queue unwinds and deleting it here too
    // is a double free. SafePointer for the same reason -- between the click and
    // the callback it may already have gone.
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
juce::StandalonePluginHolder* PluginEditor::standalonePluginHolder()
{
    return juce::StandalonePluginHolder::getInstance();
}

void PluginEditor::showSettingsMenu()
{
    // Accuracy and CPU, and nothing else.
    juce::PopupMenu menu;

    //--------------------------------------------------------------------------
    // Oversampling: the only real fix for aliasing.
    menu.addSectionHeader ("Oversampling");

    for (const int factor : { 1, 2, 4 })
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

            setStatus (factor == 1
                           ? "Oversampling off."
                           : juce::String (factor) + "x oversampling. Reduces aliasing but increases CPU usage.",
                       false);
        });

        menu.addItem (item);
    }

    menu.addSeparator();

    struct Toggle
    {
        const char* name;
        bool SchematicModel::BuildOptions::* member;
    };

    const auto addToggles = [this, &menu] (const Toggle* toggles, size_t count)
    {
        for (size_t i = 0; i < count; ++i)
        {
            const auto member = toggles[i].member;

            juce::PopupMenu::Item item (toggles[i].name);
            item.isTicked = processorRef.buildOptions.*member;
            item.setAction ([this, member, name = juce::String (toggles[i].name)]
            {
                auto& option = processorRef.buildOptions.*member;
                option = ! option;

                // All of them change what the matrix contains or how it is
                // solved, so they need the circuit built again rather than a
                // live re-stamp.
                rebuildCircuit();
                setStatus (name + (option ? " on" : " off"), false);
            });

            menu.addItem (item);
        }
    };

    //--------------------------------------------------------------------------
    // All accurate by default: this section is what you give up to save CPU.
    menu.addSectionHeader ("Performance");

    const Toggle performanceToggles[] = {
        { "Model valve interelectrode capacitance",
          &SchematicModel::BuildOptions::interelectrodeCapacitance },
        { "Model transistor junction capacitance",
          &SchematicModel::BuildOptions::transistorJunctionCapacitance },
        { "Model transistor Early effect",
          &SchematicModel::BuildOptions::transistorEarlyEffect },
        { "Fast exp / log / pow",
          &SchematicModel::BuildOptions::fastMath },
        { "Predict the Newton convergence starting point",
          &SchematicModel::BuildOptions::predictNewtonSeed },
    };

    addToggles (performanceToggles, std::size (performanceToggles));

    // Standalone only: in a host the device is the host's business. Straight to
    // the dialog rather than through JUCE's Options button, whose menu of four
    // is three things nobody clicking this is asking for.
    if (auto* holder = standalonePluginHolder())
    {
        menu.addSeparator();

        juce::PopupMenu::Item audio ("Audio / MIDI settings" + ellipsis);
        audio.setAction ([holder] { holder->showAudioSettingsDialog(); });
        menu.addItem (audio);
    }

    // Every build, not the standalone only: in a host this is the only route to
    // the licence notice.
    menu.addSeparator();

    juce::PopupMenu::Item theme ("Theme" + ellipsis);
    theme.setAction ([this] { Celine::showThemeWindow (this); });
    menu.addItem (theme);

    juce::PopupMenu::Item about ("About " + PresetLibrary::getProductName() + ellipsis);
    about.setAction ([this] { showAboutDialog(); });
    menu.addItem (about);

    // See showPresetsMenu: a menu has no parent to inherit from.
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
    // Bypass shouts in red where every other armed control wears the theme's armed
    // colour. Set here rather than once at construction, so a theme change moves it.
    if (bypassButton != nullptr)
        bypassButton->setActiveColour (Celine::Theme::danger());

    // Re-read from the binary rather than re-tinted in place. Assets::tint writes the
    // fill into the drawable, so a second pass would be colouring the result of the
    // first rather than the artwork -- which is how a mark ends up stuck on whatever
    // colour it was first given.
    logo = Celine::Assets::drawable ("logo.svg");

    if (logo != nullptr)
        Celine::Assets::tint (*logo, Celine::Theme::text());

    // The one dropdown on a light panel, so it is coloured here rather than in the look
    // and feel the inspector's boxes share.
    channelModeBox.setColour (juce::ComboBox::backgroundColourId, Celine::Theme::teal());
    channelModeBox.setColour (juce::ComboBox::textColourId, Celine::Theme::textOnPanel());
    channelModeBox.setColour (juce::ComboBox::arrowColourId, Celine::Theme::textOnPanel());

    // The bottom band stands on the light panel too, so its labels take the panel's ink
    // rather than the chrome's.
    for (auto* label : { &inputLabel, &outputLabel, &channelModeLabel })
        label->setColour (juce::Label::textColourId, Celine::Theme::textOnPanel());

    refreshRebuildButtonColour();
}

void PluginEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // First, because applyColours below reads colours back out of it: the rebuild
    // button's idle fill is the look and feel's own button colour, and asking before
    // this ran would answer with the colour the theme is replacing.
    //
    // Everything JUCE draws for us is *told* its colours, so the look and feel has to
    // re-read them before anything repaints -- see PluginLookAndFeel::applyPalette.
    lookAndFeel.applyPalette();

    applyColours();

    // And every child that took a colour once and kept it gets a chance to take it
    // again. JUCE walks the tree for us; a control that snapshots colours says so by
    // overriding lookAndFeelChanged().
    sendLookAndFeelChange();

    repaint();
}

void PluginEditor::showAboutDialog()
{
    // The house window, shared with the other Céline plugins. What it says about this
    // one comes from ProductInfo.h -- the tagline, the wordmark, and the notices this
    // plugin owes on its own account.
    showAboutWindow (this);
}

//==============================================================================
//==============================================================================
void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (Celine::Theme::chrome());

    // drawWithin centres the artwork's *viewBox*, and the wordmark's ink is not
    // centred in its own -- there is far more empty space above the glyphs than
    // below. Placed off getDrawableBounds() instead, or it sits visibly high.
    // The bottom band. Painted by the editor because it is wider than the
    // ControlStrip child that sits in the middle of it -- see controlBandBounds.
    if (! controlBandBounds.isEmpty())
    {
        g.setColour (Celine::Theme::panel());
        g.fillRect (controlBandBounds);
    }

    // The shared housing for undo and redo, with the divider the design puts
    // between them. Drawn here rather than by either button, because it belongs
    // to the pair and not to one of them.
    if (! undoRedoHousing.isEmpty())
    {
        const auto housing = undoRedoHousing.toFloat().reduced (Celine::Theme::borderWidth * 0.5f);

        // Fill only, like every other button in the row. This is the frame those two
        // buttons do not draw for themselves, and it used to carry a rule as well --
        // which made the pair the one outlined thing in a toolbar of filled ones.
        //
        // Dimmed with the pair rather than outlined: either one being usable keeps it
        // lit, because the housing says "this group does something" and undo alone is
        // enough for that.
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

    //--------------------------------------------------------------------------
    // Toolbar
    auto toolbar = area.removeFromTop (Celine::Theme::toolbarHeight).reduced (6, 6);

    // The design's grid: every button 33 square on a 40px pitch, so the row is
    // one rhythm from end to end. Groups are separated by a wider gap rather
    // than by a divider.
    const int size = Celine::Theme::buttonSize;
    const int gap = Celine::Theme::buttonGap;
    const int groupGap = 16;

    auto place = [&toolbar] (juce::Component& c)
    {
        c.setBounds (toolbar.removeFromLeft (size).withSizeKeepingCentre (size, size));
        toolbar.removeFromLeft (gap);
    };

    // One unbroken pitch from Select to Redo. The design does not divide them:
    // the pointer, the bin and the six actions are all "do this to what is
    // already on the sheet", and a gap in the middle would claim a distinction
    // that isn't there.
    place (*selectToolButton);
    place (*deleteToolButton);

    // Mirror, flip, rotate, copy -- the design's order, which puts the three
    // that turn a part together and the one that makes another one last.
    for (const auto action : { Action::Mirror, Action::Flip, Action::Rotate, Action::Copy })
        place (actionButton (action));

    // Undo and redo abut inside one housing, which the editor paints behind
    // them -- so no gap between the pair and no frame on either.
    undoRedoHousing = toolbar.removeFromLeft (size * 2).withSizeKeepingCentre (size * 2, size);
    actionButton (Action::Undo).setBounds (undoRedoHousing.withWidth (size));
    actionButton (Action::Redo).setBounds (undoRedoHousing.withTrimmedLeft (size));

    //--------------------------------------------------------------------------
    // The right-hand cluster, laid out from the far corner inwards.
    if (logo != nullptr)
    {
        // A wider margin than the buttons get, because the wordmark is the one
        // thing in the row that is not a control and reads better with air.
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

    //--------------------------------------------------------------------------
    // Save, Load and the preset field are one group and sit *with* the field
    // rather than out at the end of the action row: all three are about which
    // file the sheet came from, and the design puts them together in the middle
    // of the bar. Centred in whatever is left between the two end groups, which
    // lands them where the mockup draws them and keeps them there as the window
    // is resized.
    {
        // Whatever is genuinely left between the two end groups, and never more:
        // a floor here is a width the toolbar cannot always honour, and forcing
        // one slid the overflow out under the bypass button.
        //
        // The gap below is **the number to change if this looks wrong**. It
        // comes off before the field is sized, because the field absorbs it --
        // sized first, it gets truncated by removeFromLeft instead.
        const int sideMargin = groupGap - gap;

        // Three buttons stand around the field now, not two: Save and Load
        // before it, Import after. All three are "which drawing am I working
        // on", and Import is the one that answers "both of them".
        constexpr int buttonsInGroup = 3;
        const int available = toolbar.getWidth() - buttonsInGroup * (size + gap) - 2 * sideMargin;
        const int fieldWidth = juce::jlimit (0, Celine::Theme::presetWidth, available);
        const int groupWidth = buttonsInGroup * (size + gap) + fieldWidth;

        // Below a certain width the field is a sliver that reads as a rendering
        // fault rather than as a control. The three buttons stay -- they are
        // square and still usable -- and the field comes back when there is room.
        presetsButton.setVisible (fieldWidth >= 56);

        toolbar.removeFromLeft (juce::jmax (sideMargin, (toolbar.getWidth() - groupWidth) / 2));

        place (*saveButton);
        place (*loadButton);
        presetsButton.setBounds (toolbar.removeFromLeft (fieldWidth)
                                     .withSizeKeepingCentre (fieldWidth, size));

        // Only the gap: removeFromLeft above already took the field's own room,
        // and took nothing when it was too narrow to show.
        toolbar.removeFromLeft (gap);
        place (*importButton);
    }

    //--------------------------------------------------------------------------
    // Control strip along the bottom
}

void PluginEditor::layOutPanels (juce::Rectangle<int> area)
{
    controlBandBounds = area.removeFromBottom (Celine::ControlStrip::preferredHeight);
    auto strip = controlBandBounds.reduced (6, 4);

    // Caption over control, in the strip's own proportions -- see
    // ControlStrip::layOutCell, which the drawn knobs go through, so the fixed
    // pair and the circuit's own knobs sit on one baseline.
    using Strip = Celine::ControlStrip;

    auto placeKnob = [] (juce::Rectangle<int> cell, juce::Slider& knob, juce::Label& label)
    {
        knob.setBounds (Strip::layOutCell (cell, &label, Strip::knobSize)
                            .withSizeKeepingCentre (Strip::knobSize, Strip::knobSize));
    };

    // INPUT hard left, VOLUME hard right, the circuit's own knobs between: it
    // reads as signal flow, and bracketing the drawn controls says that neither
    // is part of any circuit. The left column is as wide as the palette above
    // it, which puts INPUT over the palette's centre line and starts the drawn
    // knobs at the sheet's left edge.
    placeKnob (strip.removeFromLeft (paletteWidth - 6), inputSlider, inputLabel);

    constexpr int channelsWidth = Strip::comboWidth + 12;
    auto fixed = strip.removeFromRight (Strip::cellWidth + channelsWidth);

    channelModeBox.setBounds (
        Strip::layOutCell (fixed.removeFromRight (channelsWidth), &channelModeLabel,
                           Strip::comboHeight)
            .withSizeKeepingCentre (Strip::comboWidth, Strip::comboHeight));

    placeKnob (fixed, outputSlider, outputLabel);

    // Whatever is left in the middle. The strip lays its own knobs out, and
    // scrolls them when there are more than fit.
    controlStrip.setBounds (strip);

    //--------------------------------------------------------------------------
    // Palette, canvas, inspector
    palette.setBounds (area.removeFromLeft (paletteWidth));
    // Right-hand column: the inspector on top, the console filling the rest.
    auto rightColumn = area.removeFromRight (inspectorWidth);

    // The inspector gets what it needs and the console takes the rest -- but the
    // console never gets less than a few rows, which is what the second term
    // buys on a short window. Halving the column instead, as this did, put the
    // console's top a hundred pixels above where the design draws it on any
    // window tall enough for the inspector to fit.
    inspector.setBounds (rightColumn.removeFromTop (
        juce::jmin (inspectorHeight, juce::jmax (120, rightColumn.getHeight() - 160))));
    console.setBounds (rightColumn);
    canvas.setBounds (area);
}

void PluginEditor::resized()
{
    // Handed to the processor so the window comes back the size it was left.
    // Recorded here rather than in the destructor because a host may save its
    // session while the editor is still open.
    //
    // Only a size somebody could actually have left it at. A host is free to resize an
    // editor it is putting away -- to nothing, or to whatever its own frame is before
    // it lays out -- and recording that would overwrite the real size with a number the
    // window can never open at, which is indistinguishable from never having saved it.
    if (getWidth() >= minimumWidth && getHeight() >= minimumHeight)
    {
        processorRef.editorWidth = getWidth();
        processorRef.editorHeight = getHeight();
    }

    auto area = getLocalBounds();
    layOutToolbar (area);
    layOutPanels (area);
}
