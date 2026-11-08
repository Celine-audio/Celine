#include "helpers/test_helpers.h"
#include <PluginEditor.h>
#include <PluginProcessor.h>
#include <PresetLibrary.h>
#include <Schematic/ExampleSchematics.h>
#include <ui/PresetLibraryPanel.h>
#include <catch2/catch_test_macros.hpp>

namespace
{
    /** A scratch folder that takes itself away again. Real files, because a preset
        folder is a real folder that files get dropped into. */
    struct TempFolder
    {
        TempFolder()
            : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("celine-library-test-" + juce::Uuid().toString()))
        {
            folder.createDirectory();
        }

        ~TempFolder() { folder.deleteRecursively(); }

        juce::File folder;
    };

    /** A real, loadable preset -- the document the processor saves. */
    void writePreset (const juce::File& file)
    {
        file.getParentDirectory().createDirectory();

        PluginProcessor plugin;
        SchematicModel::Examples::load (plugin.getSchematic(), 0);

        const auto xml = plugin.createDocument().createXml();
        REQUIRE (xml != nullptr);
        REQUIRE (xml->writeTo (file));
    }

    /**
        root/
            Bass 9.celsch, Bass 10.celsch     -- natural order, 9 before 10
            notes.txt, .hidden.celsch         -- not presets / hidden: never listed
            Amps/ Fender.celsch
                  Marshall/ JCM 800.celsch
            Empty/
    */
    void writeTree (const juce::File& root)
    {
        writePreset (root.getChildFile ("Bass 10.celsch"));
        writePreset (root.getChildFile ("Bass 9.celsch"));
        writePreset (root.getChildFile ("Amps/Fender.celsch"));
        writePreset (root.getChildFile ("Amps/Marshall/JCM 800.celsch"));
        writePreset (root.getChildFile (".hidden.celsch"));
        root.getChildFile ("notes.txt").replaceWithText ("not a preset");
        root.getChildFile ("Empty").createDirectory();
    }

    juce::StringArray labels (const Celine::PresetLibraryPanel& panel)
    {
        juce::StringArray result;

        // The gaps between sections are rows with nothing to read; the list of
        // names is what these tests are about.
        for (int row = 0; row < panel.getNumShownRows(); ++row)
            if (! panel.isRowSeparator (row))
                result.add (panel.getRowLabel (row));

        return result;
    }

    int rowNamed (const Celine::PresetLibraryPanel& panel, const juce::String& label)
    {
        for (int row = 0; row < panel.getNumShownRows(); ++row)
            if (panel.getRowLabel (row) == label)
                return row;

        return -1;
    }

    const juce::StringArray factory { "Diode clipper with volume", "Tone control", "12AX7 gain stage" };
} // namespace

TEST_CASE ("The library lists New, the factory presets, then the folder as a tree", "[presets][library]")
{
    const TempFolder temp;
    writeTree (temp.folder);

    Celine::PresetLibraryPanel panel;
    panel.setFactoryPresets (factory);
    panel.setFolder (temp.folder);

    // The order the menu had -- New, the factory presets, the user's -- with the
    // factory folder open, as it is on a fresh install, and the user's subfolders shut.
    // Folders before presets, presets in natural order, and neither the text file nor
    // the hidden one.
    CHECK (labels (panel) == juce::StringArray { "New empty sheet",
                                                 "Factory presets",
                                                 "Diode clipper with volume", "Tone control", "12AX7 gain stage",
                                                 "Amps", "Empty",
                                                 "Bass 9", "Bass 10" });

    CHECK (panel.isRowFolder (rowNamed (panel, "Factory presets")));
    CHECK (panel.isRowOpen (rowNamed (panel, "Factory presets")));
    CHECK (panel.isRowFolder (rowNamed (panel, "Amps")));
    CHECK_FALSE (panel.isRowOpen (rowNamed (panel, "Amps")));
    CHECK_FALSE (panel.isRowFolder (rowNamed (panel, "Bass 9")));

    // Opening a folder shows what is in it, one step in; the folder inside it stays
    // shut until it is opened in turn.
    panel.chooseRow (rowNamed (panel, "Amps"));
    CHECK (panel.isRowOpen (rowNamed (panel, "Amps")));
    REQUIRE (rowNamed (panel, "Fender") > rowNamed (panel, "Amps"));
    CHECK (panel.getRowDepth (rowNamed (panel, "Fender")) == 1);
    CHECK (panel.isRowFolder (rowNamed (panel, "Marshall")));
    CHECK (rowNamed (panel, "JCM 800") == -1);

    panel.chooseRow (rowNamed (panel, "Marshall"));
    CHECK (panel.getRowDepth (rowNamed (panel, "JCM 800")) == 2);

    // And shutting it takes its contents away again.
    panel.chooseRow (rowNamed (panel, "Amps"));
    CHECK (rowNamed (panel, "Fender") == -1);
    CHECK (rowNamed (panel, "JCM 800") == -1);

    // The factory folder shuts like any other.
    panel.chooseRow (rowNamed (panel, "Factory presets"));
    CHECK (rowNamed (panel, "Tone control") == -1);
}

TEST_CASE ("A row does what the menu item did", "[presets][library]")
{
    const TempFolder temp;
    writeTree (temp.folder);

    Celine::PresetLibraryPanel panel;
    panel.setFactoryPresets (factory);
    panel.setFolder (temp.folder);

    juce::File chosenFile;
    int chosenFactory = -1;
    int newSheets = 0;

    panel.onPresetChosen = [&] (const juce::File& file) { chosenFile = file; };
    panel.onFactoryPresetChosen = [&] (int index) { chosenFactory = index; };
    panel.onNewSheet = [&] { ++newSheets; };

    panel.chooseRow (rowNamed (panel, "Bass 10"));
    CHECK (chosenFile == temp.folder.getChildFile ("Bass 10.celsch"));

    // By the index Examples numbers them, which is what loadFactoryPreset takes.
    panel.chooseRow (rowNamed (panel, "12AX7 gain stage"));
    CHECK (chosenFactory == 2);

    panel.chooseRow (rowNamed (panel, "New empty sheet"));
    CHECK (newSheets == 1);
}

TEST_CASE ("The library says what to do when there is no folder, or nothing in it", "[presets][library]")
{
    Celine::PresetLibraryPanel panel;
    panel.setFactoryPresets (factory);

    int folderRequests = 0;
    panel.onChooseFolder = [&] { ++folderRequests; };

    SECTION ("no folder chosen: a row that chooses one, as the menu's did")
    {
        panel.setFolder ({});

        const auto row = rowNamed (panel, "Choose a folder for your own presets...");
        REQUIRE (row >= 0);
        panel.chooseRow (row);
        CHECK (folderRequests == 1);
    }

    SECTION ("an empty folder says so")
    {
        const TempFolder temp;
        panel.setFolder (temp.folder);

        CHECK (rowNamed (panel, "No presets in " + temp.folder.getFileName() + " yet") >= 0);
    }
}

TEST_CASE ("A search reaches into shut folders and says where a match lives", "[presets][library]")
{
    const TempFolder temp;
    writeTree (temp.folder);

    Celine::PresetLibraryPanel panel;
    panel.setFactoryPresets (factory);
    panel.setFolder (temp.folder);

    // JCM 800 is two shut folders down; the search finds it and opens the way to it.
    // New and the factory presets that do not match are gone.
    panel.setSearchText ("jcm");
    CHECK (labels (panel) == juce::StringArray { "Amps", "Marshall", "JCM 800" });
    CHECK (panel.isRowOpen (0));
    CHECK (panel.isRowOpen (1));

    // The shipped examples are searched too.
    panel.setSearchText ("clipper");
    CHECK (labels (panel) == juce::StringArray { "Factory presets", "Diode clipper with volume" });

    panel.setSearchText ("nothing called this");
    CHECK (panel.getNumShownRows() == 0);

    // Cleared, the tree is as it was: the search opened nothing for good.
    panel.setSearchText ({});
    CHECK_FALSE (panel.isRowOpen (rowNamed (panel, "Amps")));
}

TEST_CASE ("The preset in use is marked, and its folder opened", "[presets][library]")
{
    const TempFolder temp;
    writeTree (temp.folder);

    Celine::PresetLibraryPanel panel;
    panel.setFactoryPresets (factory);
    panel.setFolder (temp.folder);

    panel.setCurrentPreset (temp.folder.getChildFile ("Amps/Marshall/JCM 800.celsch"), {});

    const auto row = rowNamed (panel, "JCM 800");
    REQUIRE (row >= 0);
    CHECK (panel.isRowCurrent (row));
    CHECK_FALSE (panel.isRowCurrent (rowNamed (panel, "Fender")));

    // A factory example is marked by name.
    panel.setCurrentPreset ({}, "Tone control");
    CHECK (panel.isRowCurrent (rowNamed (panel, "Tone control")));
    CHECK_FALSE (panel.isRowCurrent (rowNamed (panel, "JCM 800")));
}

TEST_CASE ("A thin rule divides the root groups, and nothing else", "[presets][library]")
{
    const TempFolder temp;
    writeTree (temp.folder);

    Celine::PresetLibraryPanel panel;
    panel.setFactoryPresets (factory);
    panel.setFolder (temp.folder);

    auto below = [&panel] (const juce::String& label) { return panel.hasDividerBelow (rowNamed (panel, label)); };
    auto section = [&panel] (const juce::String& label) { return panel.hasSectionBreakBelow (rowNamed (panel, label)); };

    // The heavy bar falls only between the three sections: after New, after the
    // factory presets, and nowhere inside the personal library.
    CHECK (section ("New empty sheet"));
    CHECK (section ("12AX7 gain stage"));
    CHECK_FALSE (section ("Amps"));
    CHECK_FALSE (section ("Empty"));

    // New on its own; the factory folder with its examples; each top-level folder;
    // and the presets loose at the top of the folder as one group.
    CHECK (below ("New empty sheet"));
    CHECK_FALSE (below ("Factory presets"));
    CHECK_FALSE (below ("Diode clipper with volume"));
    CHECK (below ("12AX7 gain stage"));
    CHECK (below ("Amps"));
    CHECK (below ("Empty"));
    CHECK_FALSE (below ("Bass 9"));
    CHECK_FALSE (below ("Bass 10")); // the last row has nothing under it to divide from

    // An open folder's contents belong to it: the rule moves to the end of them, and
    // a subfolder inside does not get one of its own. Inside Amps the subfolder comes
    // first, so the order is Marshall, JCM 800, then Fender -- the group's last row.
    panel.chooseRow (rowNamed (panel, "Amps"));
    panel.chooseRow (rowNamed (panel, "Marshall"));

    CHECK_FALSE (below ("Amps"));
    CHECK_FALSE (below ("Marshall"));
    CHECK_FALSE (below ("JCM 800"));
    CHECK (below ("Fender"));

    // A shut factory folder is a group of one row.
    panel.chooseRow (rowNamed (panel, "Factory presets"));
    CHECK (below ("Factory presets"));
}

TEST_CASE ("The list is exactly as tall as its rows", "[presets][library]")
{
    const TempFolder temp;
    writeTree (temp.folder);

    Celine::PresetLibraryPanel panel;
    panel.setFactoryPresets (factory);
    panel.setFolder (temp.folder);

    panel.setSearchText ("Bass 10");
    REQUIRE (panel.getNumShownRows() == 1);
    const auto one = panel.getPreferredHeight();

    panel.setSearchText ("Bass");
    REQUIRE (panel.getNumShownRows() == 2);
    const auto two = panel.getPreferredHeight();

    // One row more is one row's height more -- no floor of three rows under a single
    // match, which left it sitting on an empty list.
    const auto rowHeight = two - one;
    CHECK (rowHeight > 0);

    // The padding is the same above and below the rows whatever their number: the
    // list's height is exactly its rows plus that padding.
    const auto padding = one - panel.getFieldArea().getBottom() - rowHeight;
    panel.setSearchText ({});
    int rows = 0;

    for (int row = 0; row < panel.getNumShownRows(); ++row)
        rows += panel.getRowHeight (row);

    CHECK (panel.getPreferredHeight() == panel.getFieldArea().getBottom() + padding + rows);

    // A section gap is shorter than a row: as much space either side of its line as
    // the list has above its first row, and no more.
    for (int row = 0; row < panel.getNumShownRows(); ++row)
        if (panel.isRowSeparator (row))
            CHECK (panel.getRowHeight (row) < rowHeight);

    // Nothing found keeps one row's room, to say so.
    panel.setSearchText ("nothing called this");
    CHECK (panel.getPreferredHeight() == one);
}

TEST_CASE ("The preset field drops the library down, and a preset picked in it loads", "[presets][library][gui]")
{
    const TempFolder temp;
    writeTree (temp.folder);

    runWithinPluginEditor ([&temp] (PluginProcessor& plugin) {
        auto* editor = dynamic_cast<PluginEditor*> (plugin.getActiveEditor());
        REQUIRE (editor != nullptr);
        editor->setSize (1331, 929);

        // The editor's library reads the test's redirected settings file; point it at
        // the scratch tree for this test and put it back after.
        PresetLibrary presets;
        const auto previous = presets.getDirectory();
        presets.setDirectory (temp.folder);

        Celine::PresetButton* field = nullptr;
        Celine::PresetLibraryPanel* library = nullptr;

        for (auto* child : editor->getChildren())
        {
            if (auto* f = dynamic_cast<Celine::PresetButton*> (child)) field = f;
            if (auto* l = dynamic_cast<Celine::PresetLibraryPanel*> (child)) library = l;
        }

        REQUIRE (field != nullptr);
        REQUIRE (library != nullptr);

        const auto fieldBefore = field->getBounds();
        CHECK_FALSE (library->isVisible());

        // Clicking the field opens it. There is one bar, not two: the library's top
        // lies exactly over the preset field and stands in for it -- search, folder and
        // save -- and the list hangs below. Nothing in the toolbar moves.
        field->onClick();
        CHECK (library->isVisible());
        CHECK (library->getFieldArea() + library->getPosition() == field->getBounds());

        // Exactly as wide as the field -- the search bar -- so they read as one control.
        CHECK (library->getX() == field->getX());
        CHECK (library->getWidth() == field->getWidth());
        CHECK (library->getListArea().getY() + library->getY() > field->getBottom());
        CHECK (editor->getLocalBounds().contains (library->getBounds()));
        CHECK (field->getBounds() == fieldBefore);

        // The folder icon is in the field's right-hand end whether the library is open
        // or not, and over the library while it is, so it is always there to click.
        juce::Component* folderIcon = nullptr;

        for (auto* child : editor->getChildren())
            if (child->getName() == "Choose the preset folder")
                folderIcon = child;

        REQUIRE (folderIcon != nullptr);
        CHECK (folderIcon->isVisible());
        CHECK (field->getBounds().contains (folderIcon->getBounds()));
        CHECK (folderIcon->getRight() > field->getBounds().getCentreX());
        CHECK (editor->getIndexOfChildComponent (folderIcon) > editor->getIndexOfChildComponent (library));

        // Opening it again puts it away, as the field did with the menu.
        field->onClick();
        CHECK_FALSE (library->isVisible());

        // Escape puts it away.
        field->onClick();
        REQUIRE (library->isVisible());
        library->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
        CHECK_FALSE (library->isVisible());

        // A preset picked in it loads through the ordinary route -- the toolbar names
        // it -- and the library is put away, as the menu was.
        field->onClick();
        library->chooseRow (rowNamed (*library, "Bass 9"));
        CHECK_FALSE (library->isVisible());
        CHECK (editor->getLoadedPresetName() == "Bass 9");
        CHECK_FALSE (editor->isFactoryPresetLoaded());

        // And next time it opens, the one in use is marked.
        field->onClick();
        CHECK (library->isRowCurrent (rowNamed (*library, "Bass 9")));
        field->onClick();

        presets.setDirectory (previous);
    });
}
