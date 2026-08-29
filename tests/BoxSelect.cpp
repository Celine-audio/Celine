#include <Schematic/Schematic.h>
#include <UI/SchematicCanvas.h>
#include <catch2/catch_test_macros.hpp>

using namespace SchematicModel;
using namespace SchematicUI;

namespace
{
    /** A canvas with three resistors in a row, far enough apart that a box can
        take one without touching the next. */
    struct Bench
    {
        Schematic sheet;
        SchematicCanvas canvas { sheet };
        int a, b, c;

        Bench()
        {
            a = sheet.addElement (ElementType::Resistor, 10, 10);
            b = sheet.addElement (ElementType::Resistor, 20, 10);
            c = sheet.addElement (ElementType::Resistor, 30, 10);
            canvas.setBounds (0, 0, 900, 600);
        }

        /** The element's box on screen, which is what a selection box is
            compared against. */
        juce::Rectangle<float> pixelsOf (int id) const
        {
            const auto bounds = sheet.getElementBounds (*sheet.findElement (id));
            return { canvas.gridToPixel (bounds.getTopLeft()),
                     canvas.gridToPixel (bounds.getBottomRight()) };
        }

        juce::MouseEvent event (juce::Point<float> p, juce::ModifierKeys mods)
        {
            return { juce::Desktop::getInstance().getMainMouseSource(), p, mods,
                     1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &canvas, &canvas,
                     juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 1, false };
        }

        /** Drags a box from one screen point to another. Which way round they
            are is the whole point, so they are never reordered. */
        void dragBox (juce::Point<float> from, juce::Point<float> to,
                      juce::ModifierKeys mods = {})
        {
            canvas.mouseDown (event (from, mods));
            canvas.mouseDrag (event ({ (from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f }, mods));
            canvas.mouseDrag (event (to, mods));
            canvas.mouseUp (event (to, mods));
        }

        bool selected (int id) const { return canvas.isSelected (id); }
        size_t count() const { return canvas.getSelection().size(); }
    };
} // namespace

TEST_CASE ("Dragging left to right selects only what fits inside", "[canvas][gui]")
{
    Bench bench;

    // A box that swallows the first resistor whole and merely clips the second.
    const auto first = bench.pixelsOf (bench.a);
    const auto second = bench.pixelsOf (bench.b);

    const juce::Rectangle<float> box (first.getX() - 10.0f, first.getY() - 10.0f,
                                      (second.getCentreX() - first.getX()) + 10.0f,
                                      first.getHeight() + 20.0f);

    REQUIRE (box.contains (first));
    REQUIRE (box.intersects (second));
    REQUIRE (! box.contains (second));

    bench.dragBox (box.getTopLeft(), box.getBottomRight());

    // Enclosed only: the one it merely touched is left alone.
    CHECK (bench.selected (bench.a));
    CHECK (! bench.selected (bench.b));
    CHECK (! bench.selected (bench.c));
}

TEST_CASE ("Dragging right to left selects anything it touches", "[canvas][gui]")
{
    Bench bench;

    const auto first = bench.pixelsOf (bench.a);
    const auto second = bench.pixelsOf (bench.b);

    const juce::Rectangle<float> box (first.getX() - 10.0f, first.getY() - 10.0f,
                                      (second.getCentreX() - first.getX()) + 10.0f,
                                      first.getHeight() + 20.0f);

    // Exactly the same rectangle as the test above -- only the direction of the
    // drag differs, and that is what changes the rule.
    bench.dragBox (box.getBottomRight(), box.getTopLeft());

    CHECK (bench.selected (bench.a));
    CHECK (bench.selected (bench.b));
    CHECK (! bench.selected (bench.c));
}

TEST_CASE ("Shift adds to the selection instead of replacing it", "[canvas][gui]")
{
    Bench bench;

    auto boxAround = [&bench] (int id)
    {
        return bench.pixelsOf (id).expanded (8.0f);
    };

    bench.dragBox (boxAround (bench.a).getTopLeft(), boxAround (bench.a).getBottomRight());
    REQUIRE (bench.count() == 1);

    // A second plain drag replaces.
    bench.dragBox (boxAround (bench.b).getTopLeft(), boxAround (bench.b).getBottomRight());
    CHECK (bench.count() == 1);
    CHECK (bench.selected (bench.b));

    // With shift it accumulates.
    bench.dragBox (boxAround (bench.c).getTopLeft(), boxAround (bench.c).getBottomRight(),
                   juce::ModifierKeys (juce::ModifierKeys::shiftModifier));
    CHECK (bench.count() == 2);
    CHECK (bench.selected (bench.b));
    CHECK (bench.selected (bench.c));
}

TEST_CASE ("A selection moves, rotates and deletes as one", "[canvas][gui]")
{
    Bench bench;

    // Catch the first two with a crossing drag.
    const auto span = bench.pixelsOf (bench.a).getUnion (bench.pixelsOf (bench.b)).expanded (6.0f);
    bench.dragBox (span.getBottomRight(), span.getTopLeft());
    REQUIRE (bench.count() == 2);

    const auto before = std::pair { *bench.sheet.findElement (bench.a),
                                    *bench.sheet.findElement (bench.b) };

    SECTION ("dragging one member moves every member by the same amount")
    {
        const auto grab = bench.canvas.gridToPixel ({ before.first.x, before.first.y });
        const auto drop = bench.canvas.gridToPixel ({ before.first.x + 5, before.first.y + 3 });

        bench.canvas.mouseDown (bench.event (grab, {}));
        bench.canvas.mouseDrag (bench.event (drop, {}));
        bench.canvas.mouseUp (bench.event (drop, {}));

        const auto* movedA = bench.sheet.findElement (bench.a);
        const auto* movedB = bench.sheet.findElement (bench.b);

        CHECK (movedA->x == before.first.x + 5);
        CHECK (movedA->y == before.first.y + 3);

        // The spacing is the point: a group that moves by different amounts is
        // not a group.
        CHECK (movedB->x == before.second.x + 5);
        CHECK (movedB->y == before.second.y + 3);

        // The one left out stays put.
        CHECK (bench.sheet.findElement (bench.c)->x == 30);
    }

    SECTION ("rotate turns each about its own centre")
    {
        bench.canvas.rotate();

        CHECK (bench.sheet.findElement (bench.a)->orientation == 1);
        CHECK (bench.sheet.findElement (bench.b)->orientation == 1);
        CHECK (bench.sheet.findElement (bench.c)->orientation == 0);

        // Turning the symbols must not shift them off the pins they are wired
        // to, so the centres stay exactly where they were.
        CHECK (bench.sheet.findElement (bench.a)->x == before.first.x);
        CHECK (bench.sheet.findElement (bench.b)->x == before.second.x);
    }

    SECTION ("delete takes the whole selection")
    {
        bench.canvas.deleteSelection();

        CHECK (bench.sheet.findElement (bench.a) == nullptr);
        CHECK (bench.sheet.findElement (bench.b) == nullptr);
        CHECK (bench.sheet.findElement (bench.c) != nullptr);
        CHECK (bench.count() == 0);
    }
}

TEST_CASE ("Clicking empty sheet clears the selection", "[canvas][gui]")
{
    Bench bench;

    const auto span = bench.pixelsOf (bench.a).getUnion (bench.pixelsOf (bench.c)).expanded (6.0f);
    bench.dragBox (span.getBottomRight(), span.getTopLeft());
    REQUIRE (bench.count() == 3);

    // Well away from anything, and with no drag: the box never opens.
    const auto empty = bench.canvas.gridToPixel ({ 60, 40 });
    bench.canvas.mouseDown (bench.event (empty, {}));
    bench.canvas.mouseUp (bench.event (empty, {}));

    CHECK (bench.count() == 0);
}

TEST_CASE ("Clicking one member of a selection narrows to it", "[canvas][gui]")
{
    Bench bench;

    const auto span = bench.pixelsOf (bench.a).getUnion (bench.pixelsOf (bench.c)).expanded (6.0f);
    bench.dragBox (span.getBottomRight(), span.getTopLeft());
    REQUIRE (bench.count() == 3);

    // Press and release on one of them without moving. The press must keep the
    // group -- otherwise picking a group up by one of its members would drop
    // the rest before the drag even started -- and the release narrows it.
    const auto onB = bench.canvas.gridToPixel ({ 20, 10 });
    bench.canvas.mouseDown (bench.event (onB, {}));
    CHECK (bench.count() == 3);

    bench.canvas.mouseUp (bench.event (onB, {}));
    CHECK (bench.count() == 1);
    CHECK (bench.selected (bench.b));
}
