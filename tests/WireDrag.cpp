#include <Schematic/Schematic.h>
#include <UI/SchematicCanvas.h>

#include <catch2/catch_test_macros.hpp>

using namespace SchematicModel;
using namespace SchematicUI;

namespace
{
    /** Two resistors with a wire running between them, all three far enough
        apart to be clicked without catching the others. */
    struct Bench
    {
        Schematic sheet;
        SchematicCanvas canvas { sheet };
        int a, b, wire;

        Bench()
        {
            a = sheet.addElement (ElementType::Resistor, 10, 10);
            b = sheet.addElement (ElementType::Resistor, 30, 10);
            sheet.addWire ({ 10, 20 }, { 30, 20 });
            wire = sheet.getWires().back().id;
            canvas.setBounds (0, 0, 900, 600);
        }

        juce::MouseEvent event (juce::Point<float> p, juce::ModifierKeys mods = {})
        {
            return { juce::Desktop::getInstance().getMainMouseSource(), p, mods,
                     1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &canvas, &canvas,
                     juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 1, false };
        }

        juce::Point<float> pixelOf (juce::Point<int> grid) const { return canvas.gridToPixel (grid); }

        /** The middle of the wire, which is the part of it that is only ever
            the wire -- its ends sit under the resistors' pins. */
        juce::Point<float> onWire() const { return pixelOf ({ 20, 20 }); }

        juce::Point<float> onElement (int id) const
        {
            const auto* e = sheet.findElement (id);
            return pixelOf ({ e->x, e->y });
        }

        void drag (juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys mods = {})
        {
            canvas.mouseDown (event (from, mods));
            canvas.mouseDrag (event (to, mods));
            canvas.mouseUp (event (to, mods));
        }

        void click (juce::Point<float> at, juce::ModifierKeys mods = {})
        {
            canvas.mouseDown (event (at, mods));
            canvas.mouseUp (event (at, mods));
        }
    };

    const auto shift = juce::ModifierKeys (juce::ModifierKeys::shiftModifier);
} // namespace

TEST_CASE ("Dragging a wire that is part of a selection moves the whole selection", "[canvas][gui][wire]")
{
    Bench bench;

    // Both parts and the wire, built up the way a user would.
    bench.click (bench.onElement (bench.a));
    bench.click (bench.onElement (bench.b), shift);
    bench.canvas.mouseDown (bench.event (bench.onWire(), shift));
    bench.canvas.mouseUp (bench.event (bench.onWire(), shift));

    REQUIRE (bench.canvas.getSelection().size() == 2);
    REQUIRE (bench.canvas.getSelectedWires().size() == 1);

    const auto beforeA = juce::Point<int> { bench.sheet.findElement (bench.a)->x,
                                            bench.sheet.findElement (bench.a)->y };
    const auto beforeWire = bench.sheet.findWire (bench.wire)->a;

    // Now pick the group up *by the wire*. Everything should travel together.
    bench.drag (bench.onWire(), bench.pixelOf ({ 20, 25 }));

    CHECK (bench.canvas.getSelection().size() == 2);
    CHECK (bench.canvas.getSelectedWires().size() == 1);

    const auto* movedA = bench.sheet.findElement (bench.a);
    CHECK (movedA->y == beforeA.y + 5);
    CHECK (movedA->x == beforeA.x);
    CHECK (bench.sheet.findWire (bench.wire)->a.y == beforeWire.y + 5);
}

TEST_CASE ("Clicking a wire in a selection without dragging narrows to it", "[canvas][gui][wire]")
{
    Bench bench;

    bench.click (bench.onElement (bench.a));
    bench.canvas.mouseDown (bench.event (bench.onWire(), shift));
    bench.canvas.mouseUp (bench.event (bench.onWire(), shift));

    REQUIRE (bench.canvas.getSelection().size() == 1);
    REQUIRE (bench.canvas.getSelectedWires().size() == 1);

    // The way out of a group, and the same gesture the parts answer to.
    bench.click (bench.onWire());

    CHECK (bench.canvas.getSelection().empty());
    REQUIRE (bench.canvas.getSelectedWires().size() == 1);
    CHECK (bench.canvas.getSelectedWires().front() == bench.wire);
}

TEST_CASE ("Clicking a part while a wire is selected drops the wire", "[canvas][gui][wire]")
{
    Bench bench;

    bench.click (bench.onWire());
    REQUIRE (bench.canvas.getSelectedWires().size() == 1);

    bench.click (bench.onElement (bench.a));

    CHECK (bench.canvas.isSelected (bench.a));
    CHECK (bench.canvas.getSelectedWires().empty());
}

TEST_CASE ("Dragging an unselected wire still takes only that wire", "[canvas][gui][wire]")
{
    Bench bench;

    // The other half of the rule: grabbing something that was *not* in the
    // selection replaces it, so a wire is still picked up on its own.
    bench.click (bench.onElement (bench.a));
    REQUIRE (bench.canvas.isSelected (bench.a));

    const auto beforeA = bench.sheet.findElement (bench.a)->y;

    bench.drag (bench.onWire(), bench.pixelOf ({ 20, 25 }));

    CHECK (bench.canvas.getSelection().empty());
    CHECK (bench.canvas.getSelectedWires().size() == 1);
    CHECK (bench.sheet.findElement (bench.a)->y == beforeA);
}
