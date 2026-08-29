#include <Schematic/SchematicHistory.h>
#include <UI/SchematicCanvas.h>
#include <catch2/catch_test_macros.hpp>

using namespace SchematicModel;
using namespace SchematicUI;

namespace
{
    /** The modifier that means "the system's command key" -- Command on macOS,
        Control on Windows and Linux. JUCE maps one flag to both, which is the
        whole of respecting the platform convention. */
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

    // Something else instead. The redone future no longer exists, and offering
    // it would put back a change from a history that was abandoned.
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

    // What the editor does across a drag: record once, then amend for every
    // further grid square the mouse crosses.
    sheet.findElement (id)->x = 1;
    history.record (sheet.toValueTree());

    for (int x = 2; x <= 10; ++x)
    {
        sheet.findElement (id)->x = x;
        history.amend (sheet.toValueTree());
    }

    // One step back, and it lands where the drag began rather than partway
    // along it.
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
    // Tested at the canvas, which is where the keyboard lives: the canvas
    // forwards the shortcut and the editor owns the history, so this is the
    // seam. Wiring the callbacks here rather than adding a door into
    // PluginEditor keeps the test on the real interface.
    Schematic sheet;
    SchematicCanvas canvas { sheet };
    canvas.setBounds (0, 0, 600, 400);

    int undos = 0, redos = 0;
    canvas.onUndoRequested = [&undos] { ++undos; };
    canvas.onRedoRequested = [&redos] { ++redos; };

    // One flag covers both platforms: JUCE maps commandModifier to Command on
    // macOS and Control on Windows and Linux, which is the whole of respecting
    // the system binding.
    REQUIRE (canvas.keyPressed (juce::KeyPress ('z', command(), 'z')));
    CHECK (undos == 1);
    CHECK (redos == 0);

    REQUIRE (canvas.keyPressed (juce::KeyPress ('z', commandShift(), 'z')));
    CHECK (undos == 1);
    CHECK (redos == 1);

    // Ctrl-Y, the other redo Windows users reach for.
    REQUIRE (canvas.keyPressed (juce::KeyPress ('y', command(), 'y')));
    CHECK (redos == 2);

    // Upper case, as Caps Lock delivers it.
    REQUIRE (canvas.keyPressed (juce::KeyPress ('Z', command(), 'Z')));
    CHECK (undos == 2);

    // A bare Z is not undo -- the bare letters name parts, and stealing one for
    // an action would break that split.
    undos = 0;
    canvas.keyPressed (juce::KeyPress ('z', {}, 'z'));
    CHECK (undos == 0);
}

TEST_CASE ("Command shortcuts survive the control character X11 sends", "[undo][gui]")
{
    // Every case above hands the shortcut its plain letter as the text
    // character, which is what macOS delivers -- and is why they all passed
    // while Ctrl-Z did nothing at all on Linux.
    //
    // X11 and Windows deliver the ASCII *control code* for a Ctrl-modified
    // press: Ctrl-Z arrives as 0x1A, Ctrl-Y as 0x19. That is non-zero, so a
    // matcher that trusts the text character whenever it has one compares 0x1A
    // against 'z', says no, and never reaches the key code that would have
    // matched. This pins the platform the other cases could not see.
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

    // The same for the ones that act on the selection rather than the history.
    REQUIRE (canvas.keyPressed (juce::KeyPress ('R', command(), controlCode ('r'))));
    REQUIRE (canvas.keyPressed (juce::KeyPress ('D', command(), controlCode ('d'))));

    // And a bare letter still names a part, which the control-code path must
    // not have disturbed: it only applies when a command modifier is held.
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

    // Upper case, which is what Shift or Caps Lock delivers. Same action --
    // otherwise the shortcuts silently stop working with Caps Lock on.
    REQUIRE (canvas.keyPressed (juce::KeyPress ('R', {}, 'R')));
    CHECK (canvas.getPendingType() == ElementType::Resistor);

    REQUIRE (canvas.keyPressed (juce::KeyPress ('L', {}, 'L')));
    CHECK (canvas.getPendingType() == ElementType::Inductor);

    // A press carrying no text character at all, which is what a synthesised
    // KeyPress looks like: the key code has to carry it instead.
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

    // Left to right: swaps the part's own sides and leaves it upright.
    canvas.flip();
    CHECK (sheet.findElement (id)->mirrored);
    CHECK (sheet.findElement (id)->orientation == 0);

    canvas.flip();
    REQUIRE (! sheet.findElement (id)->mirrored);

    // Top to bottom: the other reflection, which is a mirror plus a half turn.
    canvas.flipVertical();
    CHECK (sheet.findElement (id)->mirrored);
    CHECK (sheet.findElement (id)->orientation == 2);

    // Twice is a round trip, or it isn't a reflection.
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

    // The original is untouched, and the copy carries its settings rather than
    // arriving at the table's defaults.
    CHECK (sheet.findElement (id)->x == 4);

    const auto& selection = canvas.getSelection();
    REQUIRE (selection.size() == 1);
    CHECK (selection.front() != id);

    const auto* copy = sheet.findElement (selection.front());
    REQUIRE (copy != nullptr);
    CHECK (copy->value == 4700.0);
    CHECK (copy->label == "R9");

    // Offset, so it is visible and immediately draggable rather than hidden
    // exactly behind what it came from.
    CHECK (copy->x != 4);
}
