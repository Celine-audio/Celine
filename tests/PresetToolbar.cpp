#include "helpers/test_helpers.h"

#include <PluginEditor.h>
#include <PresetLibrary.h>
#include <Schematic/ExampleSchematics.h>
#include <UI/EmbeddedAssets.h>
#include <UI/ToolbarWidgets.h>
#include <catch2/catch_test_macros.hpp>

namespace
{
    /** Lets everything queued with callAsync actually run.

        The point of the test: `onSchematicReplaced` is bounced through the
        message thread, so the interesting part happens after loadPresetFile has
        already returned. Without pumping, the bug this covers is invisible. */
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

    // Nothing loaded yet, so the button must not claim anything.
    CHECK (editor->getLoadedPresetName().isEmpty());

    REQUIRE (editor->loadPresetFile (file));
    CHECK (editor->getLoadedPresetName() == "Classic tone stack");

    // The name has to survive the callback that lands a beat later. It used not
    // to: schematicChangedExternally() clears the name because it cannot tell a
    // preset load from a host restoring a session, and being async it ran after
    // the load had set it -- so the button went blank on its own.
    pump();
    CHECK (editor->getLoadedPresetName() == "Classic tone stack");

    // A host restoring a session has no file behind it, so the name goes.
    const auto document = plugin.createDocument();
    REQUIRE (plugin.restoreDocument (document));
    pump();
    CHECK (editor->getLoadedPresetName().isEmpty());

    // A file that isn't ours leaves the loaded name alone rather than half
    // replacing it -- it never got as far as replacing the circuit either.
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

    // The examples are in the same menu as the user's own files now, so the
    // button has to say which kind you are looking at -- only one of them can be
    // overwritten by Save.
    editor->loadFactoryPreset (0);
    pump();
    CHECK (editor->getLoadedPresetName() == names[0]);
    CHECK (editor->isFactoryPresetLoaded());

    // Out of range is ignored rather than loading something arbitrary: the ids
    // come off a menu built from a list that could change under it.
    editor->loadFactoryPreset (names.size() + 5);
    pump();
    CHECK (editor->getLoadedPresetName() == names[0]);

    // A user preset takes over, and stops claiming to be factory.
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

TEST_CASE ("Engaging bypass recolours the button without reshaping it", "[toolbar][gui]")
{
    // Bypass is the one toolbar button that paints itself rather than deferring
    // to IconButton, because it goes red when engaged. That made it easy for it
    // to drift: it grew its own inset, its own corner radius and its own border
    // weight, so clicking it visibly changed the button's shape. A control that
    // changes shape under the pointer reads as a rendering fault, and it breaks
    // the row's single rhythm at the one moment you are looking straight at it.
    //
    // Only the *colour* may differ, so the two states must cover exactly the same
    // pixels. Compared as a coverage mask rather than as an image, which is what
    // lets the colour change and nothing else.
    SchematicUI::PowerButton button ("Bypass",
                                     SchematicUI::Assets::drawable ("power-off-solid-full.svg"));
    button.setSize (SchematicUI::Theme::buttonSize, SchematicUI::Theme::buttonSize);

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

    // Not zero: the glyph is a solid fill in one state and a red one in the
    // other, and antialiasing at its edges lands either side of the threshold.
    // The *shape* is what matters, so allow a few pixels of that and no more --
    // a changed corner radius costs dozens.
    INFO (differing << " pixels differ between the two states");
    CHECK (differing <= 12);
}
