/*
    One test, and it is the one that keeps the theming honest.

    A colour handed to a widget with `setColour`, or painted into a drawable with
    `Assets::tint`, is a *snapshot*: the widget keeps it and never asks again. Nothing
    warns about that. The window simply comes out half-themed, and the half that did not
    move is whichever control was written before somebody remembered the rule -- which
    is not a thing code review reliably catches, because the omission looks like nothing
    at all.

    So this renders the whole editor, moves every role somewhere it could not already
    have been, renders it again, and fails if any colour the design *ships* is still on
    screen. That turns "did I remember applyColours()" into an answer rather than a
    habit.
*/
#include "helpers/test_helpers.h"

#include <PluginEditor.h>
#include <ui/Theme.h>
#include <ui/ThemePalette.h>

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
    /** A colour no role ships and no two roles collide on: the channels rotated and one
        of them inverted. Rotating alone would map a grey onto itself, and a grey that
        cannot move is a straggler this test would never see. */
    juce::Colour movedFrom (juce::Colour c)
    {
        return juce::Colour (c.getGreen(), c.getBlue(), (juce::uint8) (255 - c.getRed()));
    }
}

TEST_CASE ("every colour on screen follows the theme", "[theme]")
{
    using namespace Celine::Theme;

    PluginProcessor plugin;
    auto* editor = plugin.createEditorAndMakeActive();

    // A failing REQUIRE below throws, so the teardown cannot be left at the end of the
    // function: an editor leaked on failure buries the message under leak reports.
    const struct Closer
    {
        PluginProcessor& p;
        juce::AudioProcessorEditor* e;
        ~Closer() { Celine::Theme::palette().reset(); p.editorBeingDeleted (e); delete e; }
    } closer { plugin, editor };

    // Big enough that every panel is laid out rather than collapsed to nothing.
    editor->setSize (1500, 900);

    for (size_t i = 0; i < numRoles; ++i)
        palette().set ((Role) i, movedFrom (juce::Colour (info()[i].shipped)));

    // ChangeBroadcaster posts, and there is no message loop in here to deliver it.
    palette().sendSynchronousChangeMessage();

    const auto shot = editor->createComponentSnapshot (editor->getLocalBounds(), false, 1.0f);
    REQUIRE (shot.isValid());

    std::map<juce::uint32, int> counts;
    {
        const juce::Image::BitmapData data (shot, juce::Image::BitmapData::readOnly);

        for (int y = 0; y < shot.getHeight(); ++y)
            for (int x = 0; x < shot.getWidth(); ++x)
                ++counts[data.getPixelColour (x, y).getARGB()];
    }

    for (size_t i = 0; i < numRoles; ++i)
    {
        const auto it = counts.find (juce::Colour (info()[i].shipped).getARGB());

        // A handful of pixels can survive as an antialiasing accident where two themed
        // colours blend; a control that did not follow leaves far more than that.
        const auto stuck = it == counts.end() ? 0 : it->second;

        // Where they are, so the straggler can be named rather than hunted.
        juce::Rectangle<int> box;

        if (stuck >= 40)
        {
            const juce::Image::BitmapData data (shot, juce::Image::BitmapData::readOnly);
            const auto wanted = juce::Colour (info()[i].shipped).getARGB();

            for (int y = 0; y < shot.getHeight(); ++y)
                for (int x = 0; x < shot.getWidth(); ++x)
                    if (data.getPixelColour (x, y).getARGB() == wanted)
                        box = box.isEmpty() ? juce::Rectangle<int> (x, y, 1, 1)
                                            : box.getUnion (juce::Rectangle<int> (x, y, 1, 1));
        }

        INFO ("role '" << info()[i].key << "' is still showing its shipped colour in "
                       << stuck << " pixels, within " << box.toString().toStdString()
                       << " -- something took it once and kept it. Gather that control's "
                          "colours into an applyColours() and call it from "
                          "lookAndFeelChanged() too.");
        REQUIRE (stuck < 40);
    }

}
