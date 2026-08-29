#include <PluginProcessor.h>
#include <PresetLibrary.h>
#include <Schematic/ExampleSchematics.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
    /** A scratch folder that takes itself away again. Every test here writes
        real files, because a preset folder is a real folder someone drops files
        into -- mocking the filesystem would test the mock. */
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

    /** A settings file in the scratch folder, never the user's own. */
    juce::File settingsIn (const juce::File& folder) { return folder.getChildFile ("settings.xml"); }

    /** Writes a real, loadable preset -- the document PluginProcessor saves,
        not a stub, so what comes back out has to survive the actual format. */
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

    //--------------------------------------------------------------------------
    // Nothing chosen yet: the menu has to be able to tell "no folder" from "a
    // folder with nothing in it", because they need different offers.
    {
        PresetLibrary library (settings);
        CHECK (! library.hasDirectory());
        CHECK (! library.hasBeenOffered());
        CHECK (library.getPresets().isEmpty());
    }

    //--------------------------------------------------------------------------
    // A folder with presets, a decoy, and names that sort like a person expects.
    writePreset (presetFolder.getChildFile ("Bass 10.celsch"));
    writePreset (presetFolder.getChildFile ("Bass 9.celsch"));
    writePreset (presetFolder.getChildFile ("apple.celsch"));

    // Not ours, and must not appear: the menu loads whatever it lists. The
    // .ampsch is a well-formed sheet from before the rename -- the extension
    // alone decides, and that one is no longer ours.
    writePreset (presetFolder.getChildFile ("Zebra.ampsch"));
    presetFolder.getChildFile ("notes.txt").replaceWithText ("not a circuit");
    presetFolder.getChildFile ("cover.png").replaceWithText ("not a circuit either");

    // Nor must a subfolder, even one full of presets -- the listing is flat.
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

        // Natural order: 9 before 10, and case ignored, so the menu reads the
        // way the folder does in a file browser.
        CHECK (names == juce::StringArray ({ "apple", "Bass 9", "Bass 10" }));
    }

    //--------------------------------------------------------------------------
    // The choice outlives the object that made it -- that is the whole point of
    // putting it in a settings file rather than in the plugin's state.
    {
        PresetLibrary reopened (settings);
        CHECK (reopened.hasDirectory());
        CHECK (reopened.getDirectory() == presetFolder);
        CHECK (reopened.getPresets().size() == 3);
    }

    //--------------------------------------------------------------------------
    // A folder that has gone away reads as no folder, not as an empty one. The
    // menu then offers to find a new one instead of implying the presets were
    // deleted -- an external drive being unplugged is the ordinary case.
    REQUIRE (presetFolder.deleteRecursively());

    {
        PresetLibrary missing (settings);
        CHECK (! missing.hasDirectory());
        CHECK (missing.getPresets().isEmpty());

        // The path is still on file, though, so nothing has been forgotten --
        // plug the drive back in and it comes straight back.
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

        // The editor marks this before showing the dialog, so declining -- or
        // closing the window on it -- is final.
        library.markOffered();
        CHECK (library.hasBeenOffered());

        // Declining leaves no folder behind. Nothing may appear on anyone's disk
        // because they were asked a question and said no.
        CHECK (! library.hasDirectory());
    }

    PresetLibrary nextRun (settings);
    CHECK (nextRun.hasBeenOffered());
    CHECK (! nextRun.hasDirectory());

    // The suggested folder is a path to put in front of the user, nothing more.
    // Checked by shape rather than by asking whether it exists -- it may well
    // exist on the machine running this, and that is not this test's business.
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

    // Saved the way the Save button saves, with a knob moved so there is
    // something to check beyond the drawing surviving.
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

    // What PluginEditor::loadPresetFile does, minus the window: parse, then hand
    // it to restoreDocument like every other route in.
    PluginProcessor loaded;
    const auto xml = juce::XmlDocument::parse (found[0]);
    REQUIRE (xml != nullptr);
    REQUIRE (loaded.restoreDocument (juce::ValueTree::fromXml (*xml)));

    CHECK (static_cast<int> (loaded.getSchematic().getElements().size()) == parts);
    CHECK (loaded.apvts.getParameter ("output")->getValue() == Catch::Approx (0.25f));

    // A file with the right suffix that isn't ours is refused outright rather
    // than half-loaded -- the menu lists a folder, and anything can be in it.
    const auto impostor = presetFolder.getChildFile ("Not really.celsch");
    impostor.replaceWithText ("<NOPE><SOMETHING/></NOPE>");

    PluginProcessor untouched;
    const auto parsed = juce::XmlDocument::parse (impostor);
    REQUIRE (parsed != nullptr);
    CHECK (! untouched.restoreDocument (juce::ValueTree::fromXml (*parsed)));
}

TEST_CASE ("Sheets from before the rename are refused", "[presets]")
{
    // The format changed twice at once: the extension became .celsch and the
    // document's ValueTree type became CELINESCHEMATIC. Neither old name is
    // read any more -- one format, one name, no second path to keep working.
    //
    // The consequence is deliberate and worth stating where it can be seen: a
    // sheet saved by an older build no longer opens, and a DAW session whose
    // state blob carries the old tag comes back as an empty circuit. That is
    // the trade for having one format.
    TempFolder scratch;
    const auto folder = scratch.folder.getChildFile ("Presets");
    REQUIRE (folder.createDirectory());

    PluginProcessor source;
    SchematicModel::Examples::load (source.getSchematic(), 0);
    REQUIRE (source.getSchematic().getElements().size() > 0);

    // A document that is ours in every respect but its type, which is what an
    // older build wrote.
    auto current = source.createDocument().createCopy();
    REQUIRE (current.hasType (PluginProcessor::documentType));

    juce::ValueTree old ("AMPMODELLING");

    for (int i = 0; i < current.getNumChildren(); ++i)
        old.appendChild (current.getChild (i).createCopy(), nullptr);

    // Refused on the way a host session arrives.
    {
        PluginProcessor loaded;
        CHECK (! loaded.restoreDocument (old));
    }

    // And the old extension is not listed, so the menu cannot offer one.
    const auto file = folder.getChildFile ("From an older build.ampsch");
    const auto xml = old.createXml();
    REQUIRE (xml != nullptr);
    REQUIRE (xml->writeTo (file));

    PresetLibrary library (settingsIn (scratch.folder));
    library.setDirectory (folder);
    CHECK (library.getPresets().isEmpty());

    // What is written carries the one name there is.
    CHECK (source.createDocument().hasType (PluginProcessor::documentType));
}

TEST_CASE ("The first-run prompt's Choose button is button 1, not button 0", "[presets]")
{
    // offerPresetFolderOnFirstRun asks with a two-button box and opens the
    // folder chooser on one particular reply. The number it has to compare
    // against is *not* the index of the button: AlertWindow::exitAlert reports
    // the value passed to addButton, and LookAndFeel_V2::createAlertWindow
    // numbers a two-button box 1, 0 -- so that 0 is shared with escape and the
    // close box, which are also "no".
    //
    // Reading it as an index inverts the whole dialog, and inverts it silently:
    // both buttons dismiss it, so the only symptom is that "Choose folder..."
    // does nothing and "Not now" opens a file chooser nobody asked for. That
    // shipped once. This pins the convention so a JUCE upgrade that renumbered
    // it would fail here rather than in front of someone on their first run.
    const auto options =
        juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::QuestionIcon)
            .withTitle ("Presets")
            .withMessage ("Pick a folder.")
            .withButton ("Choose folder...")
            .withButton ("Not now");

    REQUIRE (options.getNumButtons() == 2);

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
