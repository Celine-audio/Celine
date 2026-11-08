#include <Schematic/SchematicHistory.h>
#include <ui/SchematicCanvas.h>
#include <catch2/catch_test_macros.hpp>

using namespace SchematicModel;
using namespace Celine;

namespace
{
    /** Command on macOS, Control on Windows and Linux. */
    juce::ModifierKeys command()
    {
        return juce::ModifierKeys (juce::ModifierKeys::commandModifier);
    }

    juce::ModifierKeys commandShift()
    {
        return juce::ModifierKeys (juce::ModifierKeys::commandModifier
                                   | juce::ModifierKeys::shiftModifier);
    }
} // namespace

TEST_CASE ("The history steps back and forward over edits", "[undo]")
{
    SchematicHistory history;

    Schematic sheet;
    history.reset (sheet.toValueTree());

    CHECK (! history.canUndo());
    CHECK (! history.canRedo());

    sheet.addElement (ElementType::Resistor, 0, 0);
    history.record (sheet.toValueTree());

    sheet.addElement (ElementType::Capacitor, 8, 0);
    history.record (sheet.toValueTree());

    REQUIRE (history.canUndo());

    // Back to one part, then to none.
    Schematic replay;
    replay.restoreFromValueTree (history.undo());
    CHECK (replay.getElements().size() == 1);

    replay.restoreFromValueTree (history.undo());
    CHECK (replay.getElements().empty());
    CHECK (! history.canUndo());
    CHECK (history.canRedo());

    // And forward again.
    replay.restoreFromValueTree (history.redo());
    CHECK (replay.getElements().size() == 1);
    replay.restoreFromValueTree (history.redo());
    CHECK (replay.getElements().size() == 2);
    CHECK (! history.canRedo());
}

TEST_CASE ("A new edit after undoing abandons the redo branch", "[undo]")
{
    SchematicHistory history;
    Schematic sheet;
    history.reset (sheet.toValueTree());

    sheet.addElement (ElementType::Resistor, 0, 0);
    history.record (sheet.toValueTree());

    sheet.restoreFromValueTree (history.undo());
    REQUIRE (history.canRedo());

    // A new edit discards the redo future.
    sheet.addElement (ElementType::Diode, 4, 4);
    history.record (sheet.toValueTree());

    CHECK (! history.canRedo());
    CHECK (history.canUndo());
}

TEST_CASE ("Amending keeps a drag to one undo step", "[undo]")
{
    SchematicHistory history;
    Schematic sheet;
    const auto id = sheet.addElement (ElementType::Resistor, 0, 0);
    history.reset (sheet.toValueTree());

    // A drag: record once, then amend for each further grid square.
    sheet.findElement (id)->x = 1;
    history.record (sheet.toValueTree());

    for (int x = 2; x <= 10; ++x)
    {
        sheet.findElement (id)->x = x;
        history.amend (sheet.toValueTree());
    }

    // One undo lands where the drag began.
    Schematic replay;
    replay.restoreFromValueTree (history.undo());
    CHECK (replay.findElement (id)->x == 0);
    CHECK (! history.canUndo());
}

TEST_CASE ("The history is bounded", "[undo]")
{
    SchematicHistory history;
    Schematic sheet;
    history.reset (sheet.toValueTree());

    for (size_t i = 0; i < SchematicHistory::maxDepth + 20; ++i)
    {
        sheet.addElement (ElementType::Resistor, static_cast<int> (i) * 4, 0);
        history.record (sheet.toValueTree());
    }

    int steps = 0;

    while (history.canUndo())
    {
        history.undo();
        ++steps;
    }

    INFO ("undid " << steps << " steps");
    CHECK (steps <= static_cast<int> (SchematicHistory::maxDepth));
}

TEST_CASE ("Undo and redo run from the system's command key", "[undo][gui]")
{
    // Tested at the canvas, which forwards shortcuts to the editor's history through
    // these callbacks.
    Schematic sheet;
    SchematicCanvas canvas { sheet };
    canvas.setBounds (0, 0, 600, 400);

    int undos = 0, redos = 0;
    canvas.onUndoRequested = [&undos] { ++undos; };
    canvas.onRedoRequested = [&redos] { ++redos; };

    // commandModifier is Command on macOS and Control elsewhere.
    REQUIRE (canvas.keyPressed (juce::KeyPress ('z', command(), 'z')));
    CHECK (undos == 1);
    CHECK (redos == 0);

    REQUIRE (canvas.keyPressed (juce::KeyPress ('z', commandShift(), 'z')));
    CHECK (undos == 1);
    CHECK (redos == 1);

    // Ctrl-Y also redoes.
    REQUIRE (canvas.keyPressed (juce::KeyPress ('y', command(), 'y')));
    CHECK (redos == 2);

    // Upper case, as Caps Lock delivers it.
    REQUIRE (canvas.keyPressed (juce::KeyPress ('Z', command(), 'Z')));
    CHECK (undos == 2);

    // A bare Z is not undo: bare letters name parts.
    undos = 0;
    canvas.keyPressed (juce::KeyPress ('z', {}, 'z'));
    CHECK (undos == 0);
}

TEST_CASE ("Command shortcuts survive the control character X11 sends", "[undo][gui]")
{
    // X11 and Windows deliver a control code as the text character for Ctrl presses
    // (Ctrl-Z is 0x1A), so matching must fall back to the key code.
    Schematic sheet;
    SchematicCanvas canvas { sheet };
    canvas.setBounds (0, 0, 600, 400);

    int undos = 0, redos = 0;
    canvas.onUndoRequested = [&undos] { ++undos; };
    canvas.onRedoRequested = [&redos] { ++redos; };

    const auto controlCode = [] (juce::juce_wchar letter)
    { return static_cast<juce::juce_wchar> (letter - 'a' + 1); };

    REQUIRE (canvas.keyPressed (juce::KeyPress ('Z', command(), controlCode ('z'))));
    CHECK (undos == 1);

    REQUIRE (canvas.keyPressed (juce::KeyPress ('Z', commandShift(), controlCode ('z'))));
    CHECK (redos == 1);

    REQUIRE (canvas.keyPressed (juce::KeyPress ('Y', command(), controlCode ('y'))));
    CHECK (redos == 2);

    // The same for selection shortcuts.
    REQUIRE (canvas.keyPressed (juce::KeyPress ('R', command(), controlCode ('r'))));
    REQUIRE (canvas.keyPressed (juce::KeyPress ('D', command(), controlCode ('d'))));

    // A bare letter still names a part.
    REQUIRE (canvas.keyPressed (juce::KeyPress ('r', {}, 'r')));
    CHECK (canvas.getPendingType() == ElementType::Resistor);
}

TEST_CASE ("Part shortcuts follow the character the layout produced", "[gui]")
{
    Schematic sheet;
    SchematicCanvas canvas { sheet };
    canvas.setBounds (0, 0, 600, 400);

    // Lower case, as typed.
    REQUIRE (canvas.keyPressed (juce::KeyPress ('r', {}, 'r')));
    CHECK (canvas.getPendingType() == ElementType::Resistor);

    REQUIRE (canvas.keyPressed (juce::KeyPress ('c', {}, 'c')));
    CHECK (canvas.getPendingType() == ElementType::Capacitor);

    // Upper case (Shift or Caps Lock): same action.
    REQUIRE (canvas.keyPressed (juce::KeyPress ('R', {}, 'R')));
    CHECK (canvas.getPendingType() == ElementType::Resistor);

    REQUIRE (canvas.keyPressed (juce::KeyPress ('L', {}, 'L')));
    CHECK (canvas.getPendingType() == ElementType::Inductor);

    // No text character (a synthesised KeyPress): the key code carries it.
    REQUIRE (canvas.keyPressed (juce::KeyPress ('D', {}, 0)));
    CHECK (canvas.getPendingType() == ElementType::Diode);
}

TEST_CASE ("Mirror and flip are different reflections", "[gui]")
{
    Schematic sheet;
    const auto id = sheet.addElement (ElementType::Triode, 0, 0);

    SchematicCanvas canvas { sheet };
    canvas.setBounds (0, 0, 600, 400);
    canvas.selectElement (id);

    const auto* triode = sheet.findElement (id);
    REQUIRE (triode->orientation == 0);
    REQUIRE (! triode->mirrored);

    // Left to right: swaps the part's sides, stays upright.
    canvas.flip();
    CHECK (sheet.findElement (id)->mirrored);
    CHECK (sheet.findElement (id)->orientation == 0);

    canvas.flip();
    REQUIRE (! sheet.findElement (id)->mirrored);

    // Top to bottom: a mirror plus a half turn.
    canvas.flipVertical();
    CHECK (sheet.findElement (id)->mirrored);
    CHECK (sheet.findElement (id)->orientation == 2);

    // Twice is a round trip.
    canvas.flipVertical();
    CHECK (! sheet.findElement (id)->mirrored);
    CHECK (sheet.findElement (id)->orientation == 0);
}

TEST_CASE ("Duplicate copies the selection and selects the copies", "[gui]")
{
    Schematic sheet;
    const auto id = sheet.addElement (ElementType::Resistor, 4, 4);
    auto* source = sheet.findElement (id);
    source->value = 4700.0;
    source->label = "R9";

    SchematicCanvas canvas { sheet };
    canvas.setBounds (0, 0, 600, 400);
    canvas.selectElement (id);

    canvas.duplicateSelection();

    REQUIRE (sheet.getElements().size() == 2);

    // The original is untouched; the copy carries its settings.
    CHECK (sheet.findElement (id)->x == 4);

    const auto& selection = canvas.getSelection();
    REQUIRE (selection.size() == 1);
    CHECK (selection.front() != id);

    const auto* copy = sheet.findElement (selection.front());
    REQUIRE (copy != nullptr);
    CHECK (copy->value == 4700.0);
    CHECK (copy->label == "R9");

    // Offset from the original.
    CHECK (copy->x != 4);
}

TEST_CASE ("Duplicate copies the selected wires with the parts", "[gui]")
{
    // A stage copied with its wiring comes out wired.
    Schematic sheet;
    const auto id = sheet.addElement (ElementType::Resistor, 4, 4);
    sheet.addWire ({ 4, 6 }, { 4, 10 });
    sheet.addWire ({ 4, 10 }, { 9, 10 });
    const int wireA = sheet.getWires()[0].id;
    const int wireB = sheet.getWires()[1].id;

    SchematicCanvas canvas { sheet };
    canvas.setBounds (0, 0, 600, 400);
    canvas.setSelection ({ id }, { wireA, wireB });

    canvas.duplicateSelection();

    REQUIRE (sheet.getElements().size() == 2);
    REQUIRE (sheet.getWires().size() == 4);

    // The copies are selected, parts and wires together.
    REQUIRE (canvas.getSelection().size() == 1);
    REQUIRE (canvas.getSelectedWires().size() == 2);
    CHECK (canvas.getSelection().front() != id);

    // Each wire is copied at the parts' offset, so it still meets their pins.
    const auto* part = sheet.findElement (canvas.getSelection().front());
    const auto* first = sheet.findWire (canvas.getSelectedWires()[0]);
    const auto* second = sheet.findWire (canvas.getSelectedWires()[1]);
    REQUIRE (part != nullptr);
    REQUIRE (first != nullptr);
    REQUIRE (second != nullptr);

    const juce::Point<int> delta (part->x - 4, part->y - 4);
    CHECK (first->a == juce::Point<int> (4, 6) + delta);
    CHECK (first->b == juce::Point<int> (4, 10) + delta);
    CHECK (second->a == juce::Point<int> (4, 10) + delta);
    CHECK (second->b == juce::Point<int> (9, 10) + delta);

    // The originals are where they were.
    CHECK (sheet.findWire (wireA)->a == juce::Point<int> (4, 6));
    CHECK (sheet.findWire (wireB)->b == juce::Point<int> (9, 10));
}

TEST_CASE ("Duplicate works on wires alone", "[gui]")
{
    Schematic sheet;
    sheet.addWire ({ 2, 2 }, { 8, 2 });
    const int wire = sheet.getWires()[0].id;

    SchematicCanvas canvas { sheet };
    canvas.setBounds (0, 0, 600, 400);
    canvas.setSelection ({}, { wire });

    canvas.duplicateSelection();

    REQUIRE (sheet.getWires().size() == 2);
    REQUIRE (canvas.getSelectedWires().size() == 1);
    CHECK (canvas.getSelectedWires().front() != wire);
    CHECK (canvas.getSelection().empty());
}
