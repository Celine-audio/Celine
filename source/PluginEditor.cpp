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

#include "UI/EmbeddedAssets.h"

namespace
{
    const juce::Colour chromeColour = SchematicUI::Theme::chrome();
    const juce::Colour textColour = SchematicUI::Theme::text();
    const juce::Colour pendingColour = SchematicUI::Theme::pending();
}

//==============================================================================
PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), canvas (p.getSchematic()), controlStrip (p)
{
    setLookAndFeel (&lookAndFeel);

    // Standalone only: JUCE's own Options menu and Audio/MIDI dialog are never
    // our children, so they resolve against the *default* look and feel -- which
    // inside a DAW belongs to the host.
    if (processorRef.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    buildToolbar();
    buildPanels();
    buildBottomBand();

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
    const int minWidth = paletteWidth + inspectorWidth + 400;
    const int minHeight = 460;
    setResizeLimits (minWidth, minHeight, 4000, 3000);

    // Clamped rather than trusted: a size from a state blob written by an older
    // build, or on a bigger screen, must not produce a window you cannot use.
    setSize (juce::jlimit (minWidth, 4000, processorRef.editorWidth.load()),
             juce::jlimit (minHeight, 3000, processorRef.editorHeight.load()));

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
    settingsButton = std::make_unique<SchematicUI::IconButton> (
        "Settings", SchematicUI::Assets::drawable ("gear-solid-full.svg"));

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
            slot = std::make_unique<SchematicUI::IconButton> (
                spec.name, SchematicUI::Assets::drawable (spec.file));
            slot->onClick = spec.click;
            slot->setWantsKeyboardFocus (false);
            addAndMakeVisible (*slot);
        }
    }

    {
        struct Spec { std::unique_ptr<SchematicUI::IconButton>* slot; const char* name; const char* file; };

        const Spec specs[] = {
            { &selectToolButton, "Select (S)", "arrow-pointer-solid-full.svg" },
            { &deleteToolButton, "Delete (X)", "trash-can-solid-full.svg" },
            { &saveButton,       "Save",       "floppy-disk-solid-full.svg" },
            { &loadButton,       "Load",       "file-import-solid-full.svg" },
            { &importButton,     "Import a circuit into this one", "plus-solid-full.svg" },
        };

        for (const auto& spec : specs)
        {
            *spec.slot = std::make_unique<SchematicUI::IconButton> (
                spec.name, SchematicUI::Assets::drawable (spec.file));
            (*spec.slot)->setWantsKeyboardFocus (false);
            addAndMakeVisible (**spec.slot);
        }
    }

    logo = SchematicUI::Assets::drawable ("logo.svg");

    if (logo != nullptr)
        SchematicUI::Assets::tint (*logo, SchematicUI::Theme::text());

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

    using Tool = SchematicUI::SchematicCanvas::Tool;

    selectToolButton->onClick = [this] { canvas.setTool (Tool::Select); };
    deleteToolButton->onClick = [this] { canvas.setTool (Tool::Delete); };
    saveButton->onClick       = [this] { browseForCircuit (true); };
    loadButton->onClick       = [this] { browseForCircuit (false); };
    importButton->onClick     = [this] { browseForImport(); };
    presetsButton.onClick     = [this] { showPresetsMenu(); };
    settingsButton->onClick   = [this] { showSettingsMenu(); };
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
    palette.onWireChosen = [this] { canvas.setTool (SchematicUI::SchematicCanvas::Tool::Wire); };

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
    canvas.scopeReader = [this] (int elementId, SchematicUI::ScopeReading& out)
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
    for (auto* slider : { &inputSlider, &outputSlider })
    {
        slider->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);

        // Drawn differently from the drawn circuit's own knobs, which share this
        // band; deaf to the wheel, as those are.
        slider->getProperties().set (SchematicUI::digitalGainProperty, true);
        slider->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider->setScrollWheelEnabled (false);

        addAndMakeVisible (slider);
    }

    bypassButton = std::make_unique<SchematicUI::PowerButton> (
        "Bypass", SchematicUI::Assets::drawable ("power-off-solid-full.svg"));
    bypassButton->setWantsKeyboardFocus (false);
    addAndMakeVisible (*bypassButton);

    channelModeBox.addItemList ({ "Stereo", "Mono L", "Mono R", "Mono L+R" }, 1);
    channelModeBox.setWantsKeyboardFocus (false);

    // The one dropdown on a light panel, so it is coloured here rather than in
    // the look and feel the inspector's boxes share.
    channelModeBox.setColour (juce::ComboBox::backgroundColourId, SchematicUI::Theme::teal());
    channelModeBox.setColour (juce::ComboBox::textColourId, SchematicUI::Theme::textOnPanel());
    channelModeBox.setColour (juce::ComboBox::outlineColourId, SchematicUI::Theme::textOnPanel());
    channelModeBox.setColour (juce::ComboBox::arrowColourId, SchematicUI::Theme::textOnPanel());
    addAndMakeVisible (channelModeBox);

    addAndMakeVisible (controlStrip);

    // The strip owns the widgets and the parameters; what a moved control means
    // for the drawing is the editor's business, since it owns the sheet.
    controlStrip.onControlMoved = [this] (const std::vector<int>& ids, float position)
    { writeControlPositionToSchematic (ids, position); };

    for (auto* label : { &inputLabel, &outputLabel, &channelModeLabel })
    {
        label->setFont (SchematicUI::Fonts::light (15.0f));
        label->setJustificationType (juce::Justification::centred);
        label->setColour (juce::Label::textColourId, SchematicUI::Theme::textOnPanel());
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
    rebuildButton.setColour (juce::TextButton::buttonColourId,
                             getLookAndFeel().findColour (juce::TextButton::buttonColourId));
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
    using Tool = SchematicUI::SchematicCanvas::Tool;

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
    presetsButton.setPresetName (file != juce::File{} ? file.getFileNameWithoutExtension()
                                                      : juce::String(),
                                 false);
    presetsButton.setModified (false);
}

void PluginEditor::setLoadedFactoryPreset (const juce::String& name)
{
    // No file behind it, so Save must not think it can overwrite anything --
    // lastCircuitFile stays where it was and the dialog opens where it did.
    presetsButton.setPresetName (name, true);
    presetsButton.setModified (false);
}

void PluginEditor::markPresetModified()
{
    presetsButton.setModified (true);
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
                                         || canvas.getTool() == SchematicUI::SchematicCanvas::Tool::Place);

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
    rebuildButton.setColour (juce::TextButton::buttonColourId, pendingColour);
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
    rebuildButton.setColour (juce::TextButton::buttonColourId,
                             getLookAndFeel().findColour (juce::TextButton::buttonColourId));

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

bool PluginEditor::readScopeTrace (int elementId, SchematicUI::ScopeReading& out) const
{
    const auto* trace = processorRef.getScopeTrace (elementId);

    if (trace == nullptr)
        return false;

    // Copied out rather than pointed at: a renderer holding a pointer would see
    // a column change between drawing the line into it and the line out of it.
    // The index is read first, with acquire ordering, so every column it claims
    // is complete has actually been written.
    out.writeColumn = trace->writeColumn.load (std::memory_order_acquire);

    for (int c = 0; c < SchematicUI::ScopeReading::columns; ++c)
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
            using namespace SchematicUI;

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
                button->setColour (juce::TextButton::buttonColourId, SchematicUI::Theme::surface());
                button->setColour (juce::TextButton::textColourOffId, SchematicUI::Theme::text());
                button->setColour (juce::TextButton::textColourOnId, SchematicUI::Theme::text());
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
        explicit FolderPrompt (SchematicUI::CelineLookAndFeel& lnf)
        {
            setLookAndFeel (&lnf);
            setSize (360, 160);

            message.setText ("Choose a folder to keep your circuits in, and they will "
                             "show up in the Presets menu.\n\n"
                             "You can change it later from that menu.",
                             juce::dontSendNotification);
            message.setFont (SchematicUI::Fonts::light (14.0f));
            message.setColour (juce::Label::textColourId, SchematicUI::Theme::text());
            message.setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (message);

            choose.setColour (juce::TextButton::buttonColourId, SchematicUI::Theme::violet());
            choose.setColour (juce::TextButton::textColourOffId, SchematicUI::Theme::text());
            addAndMakeVisible (choose);
            addAndMakeVisible (later);
        }

        ~FolderPrompt() override { setLookAndFeel (nullptr); }

        void paint (juce::Graphics& g) override { g.fillAll (SchematicUI::Theme::chrome()); }

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
    options.dialogBackgroundColour = SchematicUI::Theme::chrome();
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

        juce::PopupMenu::Item audio ("Audio / MIDI settings...");
        audio.setAction ([holder] { holder->showAudioSettingsDialog(); });
        menu.addItem (audio);
    }

    // Every build, not the standalone only: in a host this is the only route to
    // the licence notice.
    menu.addSeparator();

    juce::PopupMenu::Item about ("About " + PresetLibrary::getProductName() + "...");
    about.setAction ([this] { showAboutDialog(); });
    menu.addItem (about);

    // See showPresetsMenu: a menu has no parent to inherit from.
    menu.setLookAndFeel (&lookAndFeel);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (settingsButton.get()));
}

//==============================================================================
namespace
{
    /** The About window's prose. One string rather than a stack of labels:
        a licence summary trimmed to fit a layout is a licence summary that
        has been changed. */
    juce::String aboutBodyText()
    {
        const juce::String text = juce::String::fromUTF8 (
            "Copyright \xc2\xa9 2026 C\xc3\xa9line Audio.\n"
            "\n"
            "Built " __DATE__ " -- JUCE 9.0.1, C++23.\n"
            "\n"
            "\n"
            "LICENCE\n"
            "\n"
            "C\xc3\xa9line is free software: you may redistribute it and modify it under the terms of the GNU Affero General Public Licence, version 3.\n"
            "\n"
            "It comes with ABSOLUTELY NO WARRANTY, to the extent permitted by law.\n"
            "\n"
            "Source, including the exact commit this build came from:\n"
            "    https://github.com/Celine-audio/Celine\n"
            "\n"
            "Full licence text:\n"
            "    https://www.gnu.org/licenses/agpl-3.0.html\n"
            "\n"
            "\n"
            "WHY AGPL\n"
            "\n"
            "C\xc3\xa9line being free open-source software using the JUCE framework, using its free licence, it inherits its AGPLv3 terms. C\xc3\xa9line is then under the GNU AGPL v3 licence.\n"
            "\n"
            "In practice:\n"
            "\n"
            "  * Using it costs nothing and obliges nothing. The licence governs distributing the software, not what you make with it. Audio you record through C\xc3\xa9line and the circuits you draw are your own work.\n"
            "\n"
            "  * You may fork, modify and redistribute it, provided you do so under the AGPLv3 license and pass the source on. You may not relicense it or ship a closed-source build of it.\n"
            "\n"
            "  * Anyone you give a binary to is entitled to the corresponding source for that exact build. Development happens in public and each release is built from a tagged commit, which is how that right is served.\n"
            "\n"
            "\n"
            "THIRD-PARTY COMPONENTS\n"
            "\n"
            "Bundled inside every build, keeping their own licences rather than C\xc3\xa9line's:\n"
            "\n"
            "  JUCE 9.0.1 ................... AGPLv3, \xc2\xa9 Raw Material Software Limited\n"
            "  clap-juce-extensions ......... MIT, \xc2\xa9 2019-2020 Paul Walker\n"
            "  Jura typeface ................ SIL Open Font Licence 1.1, \xc2\xa9 2019 The Jura Project Authors\n"
            "  JetBrains Mono typeface ...... SIL Open Font Licence 1.1, \xc2\xa9 2020 The JetBrains Mono Project Authors\n"
            "  Font Awesome Free icons ...... CC BY 4.0, \xc2\xa9 Fonticons, Inc.\n"
            "\n"
            "Libraries JUCE vendors inside its own modules, compiled in as part of JUCE and all permissively licensed:\n"
            "\n"
            "  VST\xc2\xae" "3 SDK .................... MIT, \xc2\xa9 2025 Steinberg Media Technologies GmbH\n"
            "  ASIO\xc2\xae SDK .................... GPLv3 option, \xc2\xa9 2025 Steinberg Media Technologies GmbH (Windows standalone)\n"
            "  LunaSVG and PlutoVG .......... MIT. JUCE 9's SVG parser\n"
            "  LV2 SDK ...................... ISC\n"
            "  HarfBuzz ..................... MIT\n"
            "  SheenBidi .................... Apache 2.0\n"
            "  zlib, pnglib ................. zlib\n"
            "  jpeglib ...................... Independent JPEG Group\n"
            "  FLAC, Ogg Vorbis ............. BSD\n"
            "  AudioUnitSDK ................. Apache 2.0 (macOS builds only)\n"
            "\n"
            "VST and ASIO are registered trademarks of Steinberg Media Technologies GmbH.\n"
            "\n"
            "The ASIO SDK is dual-licensed : Steinberg\x27s own licence, or the GPLv3. C\xc3\xa9line takes the GPL option, which is what keeps an ASIO-enabled build AGPLv3.\n"
            "\n"
            "Font Awesome Free is CC BY 4.0, which makes attribution a condition of use rather than a courtesy.\n"
            "\n"
            "Used only to build and test C\xc3\xa9line: Pamplejuce (MIT, \xc2\xa9 2022 Sudara Williams), Catch2 3.8.1 (Boost Software Licence 1.0) and CPM.cmake (MIT).\n"
            "\n"
            "The repository's LICENSE and THIRD-PARTY-NOTICES files carry the full account, including the verbatim licence of every bundled work.\n"
            "\n"
            "\n"
            "CIRCUIT MODELS\n"
            "\n"
            "The valve models implement the equations of Norman Koren, and of Dempwolf and Z\xc3\xb6lzer, \"A physically-motivated triode model for circuit simulations\" (DAFx-11). Device parameters are fitted to manufacturer datasheets, which are credited in the header that uses them.\n"
            "\n"
            "What the simulation does not model is documented in LIMITATIONS.md. Read it before trusting a result or blaming a circuit.");


        return text;
    }

    class AboutPanel : public juce::Component
    {
       public:
        /** The size below which the footer's marks overlap the Close button. */
        // Width is set by the widest notices row, below which the dot-leader
        // table wraps; height by the footer.
        enum { minimumWidth = 640, minimumHeight = 460 };


        AboutPanel (SchematicUI::CelineLookAndFeel& lnf,
                    const juce::String& heading,
                    const juce::String& bodyText)
        {
            setLookAndFeel (&lnf);
            setSize (700, 640);

            logo = SchematicUI::Assets::drawable ("logo.svg");

            if (logo != nullptr)
                SchematicUI::Assets::tint (*logo, SchematicUI::Theme::text());

            // Untinted, unlike the wordmark: these are shown as supplied.
            asioLogo = SchematicUI::Assets::drawable ("asio-compatible.png");
            vstLogo  = SchematicUI::Assets::drawable ("vst-compatible.png");
            auLogo   = SchematicUI::Assets::drawable ("format-au.svg");
            clapLogo = SchematicUI::Assets::drawable ("format-clap.png");
            lv2Logo  = SchematicUI::Assets::drawable ("format-lv2.svg");

            title.setText (heading, juce::dontSendNotification);
            title.setFont (SchematicUI::Fonts::bold (18.0f));
            title.setColour (juce::Label::textColourId, SchematicUI::Theme::text());
            title.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (title);

            body.setMultiLine (true, true);
            body.setReadOnly (true);
            body.setScrollbarsShown (true);
            body.setCaretVisible (false);

            // Read-only still allows Select All and Copy, which is how the
            // source URL gets out of here.
            body.setPopupMenuEnabled (true);

            // Monospaced so the notices' dot leaders line up.
            body.setFont (SchematicUI::Fonts::mono (13.0f));
            body.setColour (juce::TextEditor::backgroundColourId, SchematicUI::Theme::consoleBackground());
            body.setColour (juce::TextEditor::textColourId, SchematicUI::Theme::text());
            body.setColour (juce::TextEditor::outlineColourId, SchematicUI::Theme::line());
            body.setColour (juce::TextEditor::focusedOutlineColourId, SchematicUI::Theme::line());
            body.setText (bodyText, false);
            addAndMakeVisible (body);

            close.setColour (juce::TextButton::buttonColourId, SchematicUI::Theme::violet());
            close.setColour (juce::TextButton::textColourOffId, SchematicUI::Theme::text());
            addAndMakeVisible (close);
        }

        ~AboutPanel() override { setLookAndFeel (nullptr); }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (SchematicUI::Theme::chrome());

            if (logo != nullptr && ! logoBounds.isEmpty())
                logo->drawWithin (g, logoBounds.toFloat(), juce::RectanglePlacement::centred, 1.0f);

            for (const auto& mark : { std::pair { asioLogo.get(), asioBounds },
                                      std::pair { vstLogo.get(), vstBounds },
                                      std::pair { auLogo.get(), auBounds },
                                      std::pair { clapLogo.get(), clapBounds },
                                      std::pair { lv2Logo.get(), lv2Bounds } })
                if (mark.first != nullptr && ! mark.second.isEmpty())
                    mark.first->drawWithin (g, mark.second.toFloat(),
                                            juce::RectanglePlacement::centred, 1.0f);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (18);

            // Roomy on purpose. Apple's guidelines require the Audio Units mark
            // to be "clearly subordinate in both size and placement to the
            // primary company or product identity", and the marks below sit at
            // their own required minimums -- so the way to satisfy that is to
            // give Céline's wordmark the space, not to shrink theirs.
            auto header = area.removeFromTop (78);

            // Off the ink rather than the viewBox, for the reason the toolbar's
            // copy gives: the wordmark is not centred in its own box.
            if (logo != nullptr)
            {
                const auto ink = logo->getDrawableBounds();
                const float aspect = ink.getHeight() > 0.0f ? ink.getWidth() / ink.getHeight() : 1.0f;
                const int height = 66;

                logoBounds = header.removeFromLeft (juce::roundToInt (height * aspect))
                                 .withSizeKeepingCentre (juce::roundToInt (height * aspect), height);
                header.removeFromLeft (10);
            }

            title.setBounds (header);

            area.removeFromTop (12);

            // 72 px clears the smallest size any of these marks may be shown
            // at. CLAP and LV2 have no minimum and are set by eye.
            auto row = area.removeFromBottom (96);
            close.setBounds (row.removeFromRight (96).withSizeKeepingCentre (96, 32));

            constexpr int gap = 20;

            const auto place = [&row] (const std::unique_ptr<juce::Drawable>& d, int height,
                                       juce::Rectangle<int>& out)
            {
                if (d == nullptr)
                    return;

                const auto ink = d->getDrawableBounds();
                const float aspect = ink.getHeight() > 0.0f ? ink.getWidth() / ink.getHeight() : 1.0f;
                const int width = juce::roundToInt (static_cast<float> (height) * aspect);

                out = row.removeFromLeft (width).withSizeKeepingCentre (width, height);
            };

            place (asioLogo, 72, asioBounds); row.removeFromLeft (gap);
            place (vstLogo, 72, vstBounds);   row.removeFromLeft (gap);
            place (auLogo, 72, auBounds);     row.removeFromLeft (gap);
            place (clapLogo, 64, clapBounds); row.removeFromLeft (gap);
            place (lv2Logo, 48, lv2Bounds);

            area.removeFromBottom (12);

            body.setBounds (area);
        }

        juce::TextButton close { "Close" };

       private:
        std::unique_ptr<juce::Drawable> logo;
        juce::Rectangle<int> logoBounds;
        std::unique_ptr<juce::Drawable> asioLogo, vstLogo, auLogo, clapLogo, lv2Logo;
        juce::Rectangle<int> asioBounds, vstBounds, auBounds, clapBounds, lv2Bounds;
        juce::Label title;
        juce::TextEditor body;
    };
} // namespace

void PluginEditor::showAboutDialog()
{
    const juce::String product = PresetLibrary::getProductName();

#ifdef VERSION
    const juce::String version { VERSION };
#else
    const juce::String version;
#endif

    auto panel = std::make_unique<AboutPanel> (
        lookAndFeel, version.isEmpty() ? product : product + " " + version, aboutBodyText());
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "About " + product;
    options.dialogBackgroundColour = SchematicUI::Theme::chrome();
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    auto* raw = panel.get();
    options.content.setOwned (panel.release());

    // Async, and self-deleting once dismissed -- see the preset prompt for why
    // neither of those is optional inside a host.
    auto* window = options.launchAsync();

    // The window, not the content: a DialogWindow sizes itself around whatever
    // it is given, so a constraint set on the panel alone is one the drag never
    // consults. The maximum is generous rather than absent -- there is no use
    // for an About box the size of a display, and a window that can be dragged
    // there is one somebody will lose.
    if (window != nullptr)
        window->setResizeLimits (AboutPanel::minimumWidth, AboutPanel::minimumHeight, 1100, 1300);

    const juce::Component::SafePointer<juce::DialogWindow> dialog (window);

    raw->close.onClick = [dialog]
    {
        if (dialog != nullptr)
            dialog->exitModalState (0);
    };
}

//==============================================================================
//==============================================================================
void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (chromeColour);

    // drawWithin centres the artwork's *viewBox*, and the wordmark's ink is not
    // centred in its own -- there is far more empty space above the glyphs than
    // below. Placed off getDrawableBounds() instead, or it sits visibly high.
    // The bottom band. Painted by the editor because it is wider than the
    // ControlStrip child that sits in the middle of it -- see controlBandBounds.
    if (! controlBandBounds.isEmpty())
    {
        g.setColour (SchematicUI::Theme::panel());
        g.fillRect (controlBandBounds);
    }

    // The shared housing for undo and redo, with the divider the design puts
    // between them. Drawn here rather than by either button, because it belongs
    // to the pair and not to one of them.
    if (! undoRedoHousing.isEmpty())
    {
        const auto housing = undoRedoHousing.toFloat().reduced (SchematicUI::Theme::borderWidth * 0.5f);

        g.setColour (SchematicUI::Theme::surface());
        g.fillRoundedRectangle (housing, SchematicUI::Theme::cornerRadius);

        // Greyed with the pair, since this is the frame those two buttons do
        // not draw for themselves. Either one being usable keeps it lit: the
        // housing says "this group does something", and undo alone is enough.
        g.setColour (history.canUndo() || history.canRedo()
                         ? SchematicUI::Theme::line()
                         : SchematicUI::Theme::lineDisabled());
        g.drawRoundedRectangle (housing, SchematicUI::Theme::cornerRadius,
                                SchematicUI::Theme::borderWidth);
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
    auto toolbar = area.removeFromTop (SchematicUI::Theme::toolbarHeight).reduced (6, 6);

    // The design's grid: every button 33 square on a 40px pitch, so the row is
    // one rhythm from end to end. Groups are separated by a wider gap rather
    // than by a divider.
    const int size = SchematicUI::Theme::buttonSize;
    const int gap = SchematicUI::Theme::buttonGap;
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
    rebuildButton.setBounds (toolbar.removeFromRight (SchematicUI::Theme::rebuildWidth)
                                 .withSizeKeepingCentre (SchematicUI::Theme::rebuildWidth, size));
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
        const int fieldWidth = juce::jlimit (0, SchematicUI::Theme::presetWidth, available);
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
    controlBandBounds = area.removeFromBottom (SchematicUI::ControlStrip::preferredHeight);
    auto strip = controlBandBounds.reduced (6, 4);

    // Caption over control, in the strip's own proportions -- see
    // ControlStrip::layOutCell, which the drawn knobs go through, so the fixed
    // pair and the circuit's own knobs sit on one baseline.
    using Strip = SchematicUI::ControlStrip;

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
    processorRef.editorWidth = getWidth();
    processorRef.editorHeight = getHeight();

    auto area = getLocalBounds();
    layOutToolbar (area);
    layOutPanels (area);
}
