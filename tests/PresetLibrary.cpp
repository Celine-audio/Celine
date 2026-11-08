#include <PluginProcessor.h>
#include <PresetLibrary.h>
#include <Schematic/ExampleSchematics.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
    /** A scratch folder that deletes itself. Tests write real files. */
    struct TempFolder
    {
        TempFolder()
            : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("celine-presets-test-" + juce::Uuid().toString()))
        {
            folder.createDirectory();
        }

        ~TempFolder() { folder.deleteRecursively(); }

        juce::File folder;
    };

    /** A settings file in the scratch folder, never the user's. */
    juce::File settingsIn (const juce::File& folder) { return folder.getChildFile ("settings.xml"); }

    /** Writes a real, loadable preset (the document PluginProcessor saves). */
    void writePreset (const juce::File& file)
    {
        PluginProcessor plugin;
        SchematicModel::Examples::load (plugin.getSchematic(), 0);

        const auto xml = plugin.createDocument().createXml();
        REQUIRE (xml != nullptr);
        REQUIRE (xml->writeTo (file));
    }
} // namespace

TEST_CASE ("The preset folder is remembered, and only presets are listed", "[presets]")
{
    TempFolder scratch;
    const auto settings = settingsIn (scratch.folder);

    auto presetFolder = scratch.folder.getChildFile ("Presets");
    REQUIRE (presetFolder.createDirectory());

    // --------------------------------------------------------------------------
    // No folder chosen must differ from an empty folder.
    {
        PresetLibrary library (settings);
        CHECK (! library.hasDirectory());
        CHECK (! library.hasBeenOffered());
        CHECK (library.getPresets().isEmpty());
    }

    // --------------------------------------------------------------------------
    // Presets, decoys, and names that sort naturally.
    writePreset (presetFolder.getChildFile ("Bass 10.celsch"));
    writePreset (presetFolder.getChildFile ("Bass 9.celsch"));
    writePreset (presetFolder.getChildFile ("apple.celsch"));

    // Old .ampsch files are no longer listed.
    writePreset (presetFolder.getChildFile ("Zebra.ampsch"));
    presetFolder.getChildFile ("notes.txt").replaceWithText ("not a circuit");
    presetFolder.getChildFile ("cover.png").replaceWithText ("not a circuit either");

    // Nor a subfolder: this listing is flat.
    const auto bank = presetFolder.getChildFile ("A bank");
    REQUIRE (bank.createDirectory());
    writePreset (bank.getChildFile ("Buried.celsch"));

    {
        PresetLibrary library (settings);
        library.setDirectory (presetFolder);

        const auto found = library.getPresets();
        juce::StringArray names;

        for (const auto& file : found)
            names.add (file.getFileNameWithoutExtension());

        INFO ("listed: " << names.joinIntoString (", "));

        // Natural order, case ignored.
        CHECK (names == juce::StringArray ({ "apple", "Bass 9", "Bass 10" }));
    }

    // --------------------------------------------------------------------------
    // The choice persists in the settings file.
    {
        PresetLibrary reopened (settings);
        CHECK (reopened.hasDirectory());
        CHECK (reopened.getDirectory() == presetFolder);
        CHECK (reopened.getPresets().size() == 3);
    }

    // --------------------------------------------------------------------------
    // A folder that has gone away reads as no folder (e.g. an unplugged drive).
    REQUIRE (presetFolder.deleteRecursively());

    {
        PresetLibrary missing (settings);
        CHECK (! missing.hasDirectory());
        CHECK (missing.getPresets().isEmpty());

        // The path is kept, so it comes back when the drive does.
        CHECK (missing.getDirectory() == presetFolder);
    }
}

TEST_CASE ("The first-run question is asked once, whatever the answer", "[presets]")
{
    TempFolder scratch;
    const auto settings = settingsIn (scratch.folder);

    {
        PresetLibrary library (settings);
        REQUIRE (! library.hasBeenOffered());

        // Marked before showing the dialog, so declining or closing is final.
        library.markOffered();
        CHECK (library.hasBeenOffered());

        // Declining creates no folder.
        CHECK (! library.hasDirectory());
    }

    PresetLibrary nextRun (settings);
    CHECK (nextRun.hasBeenOffered());
    CHECK (! nextRun.hasDirectory());

    // The suggested folder is just a path; checked by shape, not existence.
    const auto suggested = PresetLibrary::getSuggestedDirectory();
    CHECK (suggested.getParentDirectory()
           == juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    CHECK (suggested.getFileName().contains (PresetLibrary::getProductName()));
}

TEST_CASE ("A preset picked off the menu loads through the ordinary document path", "[presets]")
{
    TempFolder scratch;
    const auto settings = settingsIn (scratch.folder);

    auto presetFolder = scratch.folder.getChildFile ("Presets");
    REQUIRE (presetFolder.createDirectory());

    // Saved as Save does, with a knob moved.
    const auto file = presetFolder.getChildFile ("Lead.celsch");

    int parts = 0;

    {
        PluginProcessor plugin;
        SchematicModel::Examples::load (plugin.getSchematic(), 0);
        parts = static_cast<int> (plugin.getSchematic().getElements().size());
        REQUIRE (parts > 0);

        auto* output = plugin.apvts.getParameter ("output");
        REQUIRE (output != nullptr);
        output->setValueNotifyingHost (0.25f);

        const auto xml = plugin.createDocument().createXml();
        REQUIRE (xml != nullptr);
        REQUIRE (xml->writeTo (file));
    }

    PresetLibrary library (settings);
    library.setDirectory (presetFolder);

    const auto found = library.getPresets();
    REQUIRE (found.size() == 1);
    CHECK (found[0] == file);

    // What PluginEditor::loadPresetFile does, without the window.
    PluginProcessor loaded;
    const auto xml = juce::XmlDocument::parse (found[0]);
    REQUIRE (xml != nullptr);
    REQUIRE (loaded.restoreDocument (juce::ValueTree::fromXml (*xml)));

    CHECK (static_cast<int> (loaded.getSchematic().getElements().size()) == parts);
    CHECK (loaded.apvts.getParameter ("output")->getValue() == Catch::Approx (0.25f));

    // A foreign file with the right suffix is refused outright.
    const auto impostor = presetFolder.getChildFile ("Not really.celsch");
    impostor.replaceWithText ("<NOPE><SOMETHING/></NOPE>");

    PluginProcessor untouched;
    const auto parsed = juce::XmlDocument::parse (impostor);
    REQUIRE (parsed != nullptr);
    CHECK (! untouched.restoreDocument (juce::ValueTree::fromXml (*parsed)));
}

TEST_CASE ("Sheets from before the rename are refused", "[presets]")
{
    // Neither the old .ampsch extension nor the old AMPMODELLING type is read: sheets and
    // sessions from before the rename don't open. Deliberate, for one format.
    TempFolder scratch;
    const auto folder = scratch.folder.getChildFile ("Presets");
    REQUIRE (folder.createDirectory());

    PluginProcessor source;
    SchematicModel::Examples::load (source.getSchematic(), 0);
    REQUIRE (source.getSchematic().getElements().size() > 0);

    // Ours in every respect but its type.
    auto current = source.createDocument().createCopy();
    REQUIRE (current.hasType (PluginProcessor::documentType));

    juce::ValueTree old ("AMPMODELLING");

    for (int i = 0; i < current.getNumChildren(); ++i)
        old.appendChild (current.getChild (i).createCopy(), nullptr);

    // Refused as a host session.
    {
        PluginProcessor loaded;
        CHECK (! loaded.restoreDocument (old));
    }

    // The old extension isn't listed.
    const auto file = folder.getChildFile ("From an older build.ampsch");
    const auto xml = old.createXml();
    REQUIRE (xml != nullptr);
    REQUIRE (xml->writeTo (file));

    PresetLibrary library (settingsIn (scratch.folder));
    library.setDirectory (folder);
    CHECK (library.getPresets().isEmpty());

    // What is written carries the current type.
    CHECK (source.createDocument().hasType (PluginProcessor::documentType));
}

TEST_CASE ("The first-run prompt's Choose button is button 1, not button 0", "[presets]")
{
    // AlertWindow::exitAlert reports the value given to addButton, not the button index:
    // a two-button box numbers them 1, 0, and 0 is shared with escape and the close box.
    // Pins the convention so a JUCE change fails here.
    const auto options =
        juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::QuestionIcon)
            .withTitle ("Presets")
            .withMessage ("Pick a folder.")
            .withButton ("Choose folder...")
            .withButton ("Not now");

    REQUIRE (options.getNumButtons() == 2);

    // AlertWindow dereferences the primary display unchecked, so skip without one (e.g.
    // headless CI).
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
        SKIP ("No display to lay an AlertWindow out against");

    std::unique_ptr<juce::AlertWindow> alert (
        juce::LookAndFeel::getDefaultLookAndFeel().createAlertWindow (
            options.getTitle(), options.getMessage(),
            options.getButtonText (0), options.getButtonText (1), options.getButtonText (2),
            options.getIconType(), options.getNumButtons(), nullptr));

    REQUIRE (alert != nullptr);
    REQUIRE (alert->getNumButtons() == 2);

    const auto* choose = alert->getButton (0);
    const auto* notNow = alert->getButton (1);

    REQUIRE (choose != nullptr);
    REQUIRE (notNow != nullptr);

    CHECK (choose->getButtonText() == "Choose folder...");
    CHECK (notNow->getButtonText() == "Not now");

    // The two facts the editor's callback depends on.
    CHECK (choose->getCommandID() == 1);
    CHECK (notNow->getCommandID() == 0);
}
