#include "helpers/test_helpers.h"

#include <PluginEditor.h>
#include <PresetLibrary.h>
#include <Schematic/ExampleSchematics.h>
#include <CelineUI/EmbeddedAssets.h>
#include <ui/ToolbarWidgets.h>
#include <catch2/catch_test_macros.hpp>

namespace
{
    /** Runs everything queued with callAsync. onSchematicReplaced arrives after
        loadPresetFile has returned, so tests must pump to see it. */
    void pump()
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }
} // namespace

TEST_CASE ("The toolbar says which preset is loaded", "[presets][gui]")
{
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("celine-toolbar-" + juce::Uuid().toString());
    REQUIRE (folder.createDirectory());

    const auto file = folder.getChildFile ("Classic tone stack.celsch");

    {
        PluginProcessor source;
        SchematicModel::Examples::load (source.getSchematic(), 0);
        const auto xml = source.createDocument().createXml();
        REQUIRE (xml != nullptr);
        REQUIRE (xml->writeTo (file));
    }

    PluginProcessor plugin;
    auto* base = plugin.createEditorAndMakeActive();
    auto* editor = dynamic_cast<PluginEditor*> (base);
    REQUIRE (editor != nullptr);

    // Nothing loaded yet: the button shows no name.
    CHECK (editor->getLoadedPresetName().isEmpty());

    REQUIRE (editor->loadPresetFile (file));
    CHECK (editor->getLoadedPresetName() == "Classic tone stack");

    // The name must survive the async callback, which clears it for host restores.
    pump();
    CHECK (editor->getLoadedPresetName() == "Classic tone stack");

    // A host restore has no file, so the name goes.
    const auto document = plugin.createDocument();
    REQUIRE (plugin.restoreDocument (document));
    pump();
    CHECK (editor->getLoadedPresetName().isEmpty());

    // A foreign file leaves the loaded name alone.
    REQUIRE (editor->loadPresetFile (file));
    pump();
    REQUIRE (editor->getLoadedPresetName() == "Classic tone stack");

    const auto impostor = folder.getChildFile ("Not really.celsch");
    impostor.replaceWithText ("<NOPE><SOMETHING/></NOPE>");

    CHECK (! editor->loadPresetFile (impostor));
    pump();
    CHECK (editor->getLoadedPresetName() == "Classic tone stack");

    plugin.editorBeingDeleted (base);
    delete base;
    folder.deleteRecursively();
}

TEST_CASE ("Factory and user presets are told apart", "[presets][gui]")
{
    const auto names = SchematicModel::Examples::getNames();
    REQUIRE (! names.isEmpty());

    PluginProcessor plugin;
    auto* base = plugin.createEditorAndMakeActive();
    auto* editor = dynamic_cast<PluginEditor*> (base);
    REQUIRE (editor != nullptr);

    // Factory presets are labelled as such: Save can't overwrite them.
    editor->loadFactoryPreset (0);
    pump();
    CHECK (editor->getLoadedPresetName() == names[0]);
    CHECK (editor->isFactoryPresetLoaded());

    // Out of range is ignored.
    editor->loadFactoryPreset (names.size() + 5);
    pump();
    CHECK (editor->getLoadedPresetName() == names[0]);

    // A user preset takes over and stops being factory.
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("celine-factory-" + juce::Uuid().toString());
    REQUIRE (folder.createDirectory());
    const auto file = folder.getChildFile ("Mine.celsch");

    {
        const auto xml = plugin.createDocument().createXml();
        REQUIRE (xml != nullptr);
        REQUIRE (xml->writeTo (file));
    }

    REQUIRE (editor->loadPresetFile (file));
    pump();
    CHECK (editor->getLoadedPresetName() == "Mine");
    CHECK (! editor->isFactoryPresetLoaded());

    plugin.editorBeingDeleted (base);
    delete base;
    folder.deleteRecursively();
}

TEST_CASE ("The preset library opens on the whole list, not the last search", "[presets][gui]")
{
    PluginProcessor plugin;
    auto* base = plugin.createEditorAndMakeActive();
    auto* editor = dynamic_cast<PluginEditor*> (base);
    REQUIRE (editor != nullptr);

    // The field and the library, found as the user would: among the editor's children.
    Celine::PresetButton* field = nullptr;
    Celine::PresetLibraryPanel* library = nullptr;

    for (auto* child : editor->getChildren())
    {
        if (auto* f = dynamic_cast<Celine::PresetButton*> (child))
            field = f;
        if (auto* l = dynamic_cast<Celine::PresetLibraryPanel*> (child))
            library = l;
    }

    REQUIRE (field != nullptr);
    REQUIRE (library != nullptr);
    REQUIRE (field->onClick != nullptr);

    field->onClick(); // open
    REQUIRE (library->isVisible());
    const int everything = library->getNumShownRows();
    REQUIRE (everything > 1);

    // A search that matches nothing but one factory preset...
    library->setSearchText (SchematicModel::Examples::getNames()[0]);
    REQUIRE (library->getNumShownRows() < everything);

    field->onClick(); // the field toggles it shut
    REQUIRE (! library->isVisible());

    // ...is gone when it opens again.
    field->onClick();
    REQUIRE (library->isVisible());
    CHECK (library->getNumShownRows() == everything);
    CHECK (library->getRowLabel (0) == "New empty sheet");

    plugin.editorBeingDeleted (base);
    delete base;
}

TEST_CASE ("Engaging bypass recolours the button without reshaping it", "[toolbar][gui]")
{
    // Bypass paints itself (it goes red), so check it keeps the same shape as IconButton:
    // the two states must cover the same pixels. Compared as coverage masks, so only the
    // colour may change.
    Celine::PowerButton button ("Bypass",
                                     Celine::Assets::drawable ("power-off-solid-full.svg"));
    button.setSize (Celine::Theme::buttonSize, Celine::Theme::buttonSize);

    auto mask = [&button] (bool bypassed)
    {
        button.setToggleState (bypassed, juce::dontSendNotification);

        juce::Image image (juce::Image::ARGB, button.getWidth(), button.getHeight(), true);
        { juce::Graphics g (image); button.paintEntireComponent (g, false); }

        std::vector<bool> covered;
        covered.reserve ((size_t) (image.getWidth() * image.getHeight()));

        const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);

        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                covered.push_back (pixels.getPixelColour (x, y).getAlpha() > 24);

        return covered;
    };

    const auto running = mask (false);
    const auto bypassed = mask (true);

    REQUIRE (running.size() == bypassed.size());

    int differing = 0;

    for (size_t i = 0; i < running.size(); ++i)
        differing += running[i] != bypassed[i] ? 1 : 0;

    // Antialiasing at the glyph's edges allows a few pixels; a changed corner radius
    // costs dozens.
    INFO (differing << " pixels differ between the two states");
    CHECK (differing <= 12);
}
