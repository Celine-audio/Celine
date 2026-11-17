#include "helpers/test_helpers.h"

#include <PluginEditor.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// The controls: a right or middle drag moves the sheet, a right click that does not move
// brings the floating actions -- for parts that turn, not for wires, boxes or notes.

using namespace SchematicModel;

namespace
{
    struct Sheet
    {
        Schematic sheet;
        Celine::SchematicCanvas canvas { sheet };
        int part, box, wire;

        Sheet()
        {
            part = sheet.addElement (ElementType::Resistor, 10, 10);
            sheet.findElement (part)->value = 1000.0;
            box = sheet.addElement (ElementType::Rectangle, 40, 20);
            sheet.addWire ({ 20, 30 }, { 30, 30 });
            wire = sheet.getWires().back().id;
            canvas.setBounds (0, 0, 900, 600);
        }

        juce::Point<float> at (juce::Point<int> grid) const { return canvas.gridToPixel (grid); }

        juce::MouseEvent event (juce::Point<float> p, juce::ModifierKeys mods)
        {
            return { juce::Desktop::getInstance().getMainMouseSource(), p, mods,
                     1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &canvas, &canvas,
                     juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 1, false };
        }

        void drag (juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys mods)
        {
            canvas.mouseDown (event (from, mods));
            canvas.mouseDrag (event (to, mods));
            canvas.mouseUp (event (to, mods));
        }

        juce::Point<float> onPart() const
        {
            const auto* e = sheet.findElement (part);
            return canvas.gridToPixel ({ e->x, e->y });
        }
    };

    const juce::ModifierKeys middle (juce::ModifierKeys::middleButtonModifier);
    const juce::ModifierKeys right (juce::ModifierKeys::rightButtonModifier);

    /** Save to Import, the toolbar's middle group: its centre's distance from the window's. */
    int presetGroupOffCentre (juce::Component& editor)
    {
        int start = -1, end = -1;
        for (auto* child : editor.getChildren())
            if (auto* b = dynamic_cast<Celine::IconButton*> (child))
            {
                if (b->getName() == "Save")
                    start = b->getX();
                if (b->getName() == "Import a circuit into this one")
                    end = b->getRight();
            }

        return start < 0 || end < 0 ? 9999 : std::abs ((start + end) / 2 - editor.getWidth() / 2);
    }

    template <typename Type>
    Type* find (juce::Component& root, const std::function<bool (Type&)>& matches = nullptr)
    {
        for (auto* child : root.getChildren())
        {
            if (auto* t = dynamic_cast<Type*> (child); t != nullptr && (matches == nullptr || matches (*t)))
                return t;
            if (auto* deeper = find<Type> (*child, matches))
                return deeper;
        }
        return nullptr;
    }
} // namespace

TEST_CASE ("A right or middle drag moves the sheet, a right click asks for the actions", "[controls]")
{
    Sheet s;
    int asked = 0;
    juce::Point<int> where;
    s.canvas.onActionsRequested = [&] (juce::Point<int> p) { ++asked; where = p; };

    // Both drags move the sheet under the pointer, the part with it, nothing edited.
    auto before = s.onPart();
    s.drag ({ 400, 300 }, { 450, 330 }, middle);
    CHECK (s.onPart() == before + juce::Point<float> (50, 30));

    before = s.onPart();
    s.drag ({ 400, 300 }, { 440, 320 }, right);
    CHECK (s.onPart() == before + juce::Point<float> (40, 20));
    CHECK (asked == 0);

    // A right press that hardly moves is a click, and moves nothing.
    before = s.onPart();
    s.drag (s.onPart(), s.onPart() + juce::Point<float> (2, 1), right);
    CHECK (s.onPart() == before);

    // That click selected the part and asked for its actions, where it was let go.
    CHECK (asked == 1);
    CHECK (s.canvas.isSelected (s.part));
    CHECK (where == (before + juce::Point<float> (2, 1)).toInt());

    // On empty sheet: the selection goes and nothing comes up, rather than the actions for
    // a part the click was nowhere near.
    s.drag ({ 880, 580 }, { 880, 580 }, right);
    CHECK (asked == 1);
    CHECK (s.canvas.getSelection().empty());
    CHECK (s.canvas.getSelectedWires().empty());

    CHECK (s.canvas.getTool() == Celine::SchematicCanvas::Tool::Select);
}

TEST_CASE ("Wires, boxes and notes get no floating actions", "[controls]")
{
    Sheet s;
    int asked = 0;
    s.canvas.onActionsRequested = [&] (juce::Point<int>) { ++asked; };

    // A wire is selected by a right click, but cannot be turned: no actions.
    const auto onWire = s.at ({ 25, 30 });
    s.drag (onWire, onWire, right);
    CHECK (s.canvas.isWireSelected (s.wire));
    CHECK_FALSE (s.canvas.selectionHasActions());
    CHECK (asked == 0);

    // Nor a group box, grabbed by its edge.
    const auto frame = s.sheet.findElement (s.box)->getRectangleBounds();
    const auto edge = s.at ({ frame.getX(), frame.getCentreY() });
    s.drag (edge, edge, right);
    CHECK (s.canvas.isSelected (s.box));
    CHECK (asked == 0);

    // A part among them brings the actions back: they act on what turns.
    s.canvas.setSelection ({ s.part, s.box }, { s.wire });
    CHECK (s.canvas.selectionHasActions());
}

TEST_CASE ("A right click while placing puts the tool down", "[controls]")
{
    Sheet s;
    s.canvas.setPendingType (ElementType::Capacitor);
    CHECK (s.canvas.getTool() == Celine::SchematicCanvas::Tool::Place);

    s.drag ({ 300, 300 }, { 300, 300 }, right);
    CHECK (s.canvas.getTool() == Celine::SchematicCanvas::Tool::Select);
    CHECK (s.sheet.getElements().size() == 2);
}

TEST_CASE ("The toolbar is clean, and the actions float by the part", "[controls][ui]")
{
    runWithinPluginEditor ([] (PluginProcessor& plugin) {
        auto* editor = dynamic_cast<PluginEditor*> (plugin.getActiveEditor());
        REQUIRE (editor != nullptr);
        editor->setSize (1400, 900);

        // No rotate, flip, copy or tool buttons; undo stays.
        const auto toolbarButton = [editor] (const juce::String& name)
        {
            for (auto* child : editor->getChildren())
                if (auto* b = dynamic_cast<Celine::IconButton*> (child); b != nullptr && b->getName() == name)
                    return b;
            return (Celine::IconButton*) nullptr;
        };

        CHECK (toolbarButton ("Rotate a quarter turn") == nullptr);
        CHECK (toolbarButton ("Delete (X)") == nullptr);
        REQUIRE (toolbarButton ("Undo") != nullptr);
        CHECK (toolbarButton ("Undo")->isVisible());

        // The preset group is centred on the window.
        CHECK (presetGroupOffCentre (*editor) <= 2);

        auto* canvas = find<Celine::SchematicCanvas> (*editor);
        auto* bar = find<Celine::ActionBar> (*editor);
        REQUIRE (canvas != nullptr);
        REQUIRE (bar != nullptr);
        CHECK_FALSE (bar->isVisible());

        // Ask for the actions on the first part, as a right click does.
        auto& part = plugin.getSchematic().getElements().front();
        const int turned = part.orientation;
        canvas->selectElement (part.id);
        canvas->onActionsRequested (canvas->gridToPixel ({ part.x, part.y }).toInt());
        CHECK (bar->isVisible());
        CHECK (canvas->getBounds().contains (bar->getBounds()));

        // Over the part, centred on it...
        const auto partAt = canvas->gridToPixel ({ part.x, part.y }) + canvas->getPosition().toFloat();
        CHECK (std::abs ((float) bar->getBounds().getCentreX() - partAt.x) < 40.0f);
        CHECK ((float) bar->getBottom() < partAt.y);

        // ...but clear of it, not on it.
        const auto selected = canvas->getSelectionArea().translated ((float) canvas->getX(), (float) canvas->getY());
        CHECK_FALSE (bar->getBounds().reduced (Celine::ActionBar::shadow).toFloat().intersects (selected));

        // Its rotate turns the part, and it stays up for another.
        auto* rotate = find<juce::Button> (*bar, [] (juce::Button& b) { return b.getName().startsWith ("Rotate"); });
        REQUIRE (rotate != nullptr);
        rotate->onClick();
        CHECK (plugin.getSchematic().findElement (part.id)->orientation == (turned + 1) % 4);
        CHECK (bar->isVisible());

        // Emptying the selection puts it away.
        canvas->setSelection ({}, {});
        CHECK_FALSE (bar->isVisible());
    });
}

TEST_CASE ("Fitting the drawing leaves room for the inspector only while it shows", "[controls][ui]")
{
    runWithinPluginEditor ([] (PluginProcessor& plugin) {
        auto* editor = plugin.getActiveEditor();
        editor->setSize (1400, 900);

        auto* canvas = find<Celine::SchematicCanvas> (*editor);
        auto* inspector = find<Celine::ElementInspector> (*editor);
        REQUIRE (canvas != nullptr);
        REQUIRE (inspector != nullptr);

        // Shortcut list off and nothing selected: the inspector hides, and F frames the
        // drawing across the room it left.
        inspector->setShortcutsShown (false);
        canvas->setSelection ({}, {});
        inspector->setElement (nullptr);
        CHECK_FALSE (inspector->isVisible());
        CHECK (canvas->getFramingInsets().getRight() == Celine::Theme::floatingMargin);

        // A part selected: it is back, and so is its room.
        inspector->setElement (&plugin.getSchematic().getElements().front());
        CHECK (inspector->isVisible());
        CHECK (canvas->getFramingInsets().getRight() >= Celine::Theme::inspectorWidth);

        inspector->setShortcutsShown (true);
    });
}

TEST_CASE ("A trackpad's two fingers move the sheet; Shift or a pinch zooms; a wheel zooms", "[controls]")
{
    Sheet s;
    const auto wheel = [] (float dx, float dy, bool smooth)
    {
        juce::MouseWheelDetails w;
        w.deltaX = dx;
        w.deltaY = dy;
        w.isReversed = false;
        w.isSmooth = smooth;
        w.isInertial = false;
        return w;
    };

    const juce::Point<float> pointer (300, 200);
    const juce::ModifierKeys none, shift (juce::ModifierKeys::shiftModifier);

    // Two fingers: the sheet moves with them, exactly, both ways, at the same zoom.
    const float zoom = s.canvas.getZoomProportion();
    auto before = s.onPart();
    s.canvas.mouseWheelMove (s.event (pointer, none), wheel (10.0f / 512.0f, -20.0f / 512.0f, true));
    CHECK (s.onPart().x == Catch::Approx (before.x + 10.0f));
    CHECK (s.onPart().y == Catch::Approx (before.y - 20.0f));
    CHECK (s.canvas.getZoomProportion() == Catch::Approx (zoom));

    // Shift and two fingers: a zoom about the pointer, which stays put.
    const auto pinned = s.canvas.gridToPixel ({ 0, 0 });
    s.canvas.mouseWheelMove (s.event (pointer, shift), wheel (0.0f, 50.0f / 512.0f, true));
    CHECK (s.canvas.getZoomProportion() > zoom);
    const auto after = s.canvas.gridToPixel ({ 0, 0 });
    CHECK ((after - pointer).getDistanceFromOrigin() > (pinned - pointer).getDistanceFromOrigin()); // things spread from the pointer

    // A mouse wheel still zooms, and does not pan.
    const float closer = s.canvas.getZoomProportion();
    s.canvas.mouseWheelMove (s.event (pointer, none), wheel (0.0f, -0.25f, false));
    CHECK (s.canvas.getZoomProportion() < closer);

    // A pinch zooms about the fingers.
    const float pinchFrom = s.canvas.getZoomProportion();
    s.canvas.mouseMagnify (s.event (pointer, none), 1.5f);
    CHECK (s.canvas.getZoomProportion() > pinchFrom);
}

TEST_CASE ("A middle click picks up a copy of the part; a middle drag still pans", "[controls]")
{
    Sheet s;
    s.sheet.findElement (s.part)->orientation = 1;

    // A drag: the sheet moves, nothing is picked up.
    s.drag ({ 400, 300 }, { 430, 300 }, middle);
    CHECK (s.canvas.getTool() == Celine::SchematicCanvas::Tool::Select);

    // A click on empty sheet: nothing.
    s.drag ({ 880, 580 }, { 880, 580 }, middle);
    CHECK (s.canvas.getTool() == Celine::SchematicCanvas::Tool::Select);

    // A click on the part: the Place tool armed with a copy of it, turned as it is.
    s.drag (s.onPart(), s.onPart() + juce::Point<float> (1, 1), middle);
    CHECK (s.canvas.getTool() == Celine::SchematicCanvas::Tool::Place);
    CHECK (s.canvas.getPendingType() == ElementType::Resistor);
    CHECK (s.sheet.getElements().size() == 2); // picked up, not yet placed
}

TEST_CASE ("One Switch and one Transformer in the palette; their kind is chosen in the inspector", "[controls][ui]")
{
    runWithinPluginEditor ([] (PluginProcessor& plugin) {
        auto* editor = plugin.getActiveEditor();
        editor->setSize (1400, 900);

        auto& sheet = plugin.getSchematic();
        const int switchId = sheet.addElement (ElementType::Switch, 40, 40);
        const int transformerId = sheet.addElement (ElementType::Transformer, 60, 40);

        auto* inspector = find<Celine::ElementInspector> (*editor);
        REQUIRE (inspector != nullptr);

        // The switch: its Action box offers ON-OFF and the three changeover actions.
        auto* part = sheet.findElement (switchId);
        part->closed = true;
        inspector->setElement (part);

        auto* action = find<juce::ComboBox> (*inspector, [] (juce::ComboBox& b) { return b.getItemText (0) == "ON-OFF"; });
        REQUIRE (action != nullptr);
        CHECK (action->isVisible());
        CHECK (action->getSelectedId() == 1);

        // ON-OFF-ON: it becomes the changeover, still made on the throw it was closed on.
        action->setSelectedId (3, juce::sendNotificationSync);
        part = sheet.findElement (switchId);
        CHECK (part->type == ElementType::Spdt);
        CHECK (part->switchAction == SwitchAction::OnOffOn);
        CHECK (part->getThrowAt (part->switchPosition) == 0);

        // And back to ON-OFF: the plain switch, still closed.
        action->setSelectedId (1, juce::sendNotificationSync);
        part = sheet.findElement (switchId);
        CHECK (part->type == ElementType::Switch);
        CHECK (part->closed);

        // The transformer: Centre tap turns it into the tapped one and back, model kept.
        part = sheet.findElement (transformerId);
        part->modelIndex = 1;
        inspector->setElement (part);

        auto* tap = find<juce::ToggleButton> (*inspector, [] (juce::ToggleButton& b) { return b.getButtonText() == "Centre tap"; });
        REQUIRE (tap != nullptr);
        CHECK (tap->isVisible());
        CHECK_FALSE (tap->getToggleState());

        tap->setToggleState (true, juce::dontSendNotification);
        tap->onClick();
        part = sheet.findElement (transformerId);
        CHECK (part->type == ElementType::CenterTapTransformer);
        CHECK (part->modelIndex == 1);
        CHECK (part->getPinCount() == 5);

        tap->setToggleState (false, juce::dontSendNotification);
        tap->onClick();
        CHECK (sheet.findElement (transformerId)->type == ElementType::Transformer);
    });
}

TEST_CASE ("Space is left to the host", "[controls]")
{
    // Most hosts start and stop playback on Space, and only see it when the plugin's window
    // does not take it.
    Sheet s;
    CHECK_FALSE (s.canvas.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));

    // And a left drag on empty sheet is a box selection, not a pan, whatever was pressed.
    const auto before = s.onPart();
    s.drag ({ 600, 400 }, { 650, 450 }, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier));
    CHECK (s.onPart() == before);
}

TEST_CASE ("Twelve probes and scopes at most: the palette greys them and the sheet refuses more", "[controls][probe]")
{
    Sheet s;
    for (int i = 0; i < SchematicModel::maxTraceParts - 1; ++i)
        s.sheet.addElement (i % 2 == 0 ? ElementType::Probe : ElementType::Scope, 4 * i, 60);

    // One short: a probe can still be armed and placed.
    CHECK (s.canvas.hasRoomFor (ElementType::Probe));
    s.canvas.setPendingType (ElementType::Probe);
    REQUIRE (s.canvas.getTool() == Celine::SchematicCanvas::Tool::Place);
    s.canvas.mouseDown (s.event (s.at ({ 70, 70 }), {}));
    s.canvas.mouseUp (s.event (s.at ({ 70, 70 }), {}));

    const auto traces = [&s]
    {
        return std::count_if (s.sheet.getElements().begin(), s.sheet.getElements().end(),
                              [] (const Element& e) { return SchematicModel::isTracePart (e.type); });
    };
    REQUIRE (traces() == SchematicModel::maxTraceParts);

    // At the limit: not armed, not copied, not duplicated; other parts still are.
    CHECK_FALSE (s.canvas.hasRoomFor (ElementType::Scope));
    CHECK (s.canvas.hasRoomFor (ElementType::Resistor));

    s.canvas.setPendingType (ElementType::Scope);
    CHECK (s.canvas.getTool() == Celine::SchematicCanvas::Tool::Select);

    const auto aProbe = s.sheet.getElements().back().id;
    s.canvas.cloneElement (aProbe);
    CHECK (s.canvas.getTool() == Celine::SchematicCanvas::Tool::Select);

    s.canvas.setSelection ({ aProbe, s.part }, {});
    const auto before = s.sheet.getElements().size();
    s.canvas.duplicateSelection();
    CHECK (s.sheet.getElements().size() == before + 1); // the resistor only
    CHECK (traces() == SchematicModel::maxTraceParts);

    // The palette greys both, and only those.
    Celine::ElementPalette palette;
    const auto greyed = [&palette]
    {
        int n = 0;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
        {
            for (auto* child : c.getChildren())
            {
                if (auto* b = dynamic_cast<juce::Button*> (child); b != nullptr && ! b->isEnabled())
                    ++n;
                walk (*child);
            }
        };
        walk (palette);
        return n;
    };

    palette.setTracePartsAvailable (false);
    CHECK (greyed() == 2);
    palette.setTracePartsAvailable (true);
    CHECK (greyed() == 0);

    // A sheet that arrives with more builds, and says which are not read: a divider, and
    // one probe more than can be shown.
    Schematic many;
    many.addElement (ElementType::Input, -6, 0);                    // pin (-4, 0)
    const auto r1 = many.addElement (ElementType::Resistor, -4, 2); // (-4, 0) - (-4, 4)
    const auto r2 = many.addElement (ElementType::Resistor, -4, 6); // (-4, 4) - (-4, 8)
    many.findElement (r1)->value = 1000.0;
    many.findElement (r2)->value = 1000.0;
    many.addElement (ElementType::Ground, -4, 10);                  // pin (-4, 8)
    many.addElement (ElementType::Output, -2, 4);                   // pin (-4, 4)
    for (int i = 0; i <= SchematicModel::maxTraceParts; ++i)
        many.addElement (ElementType::Probe, -4, -2);              // pin (-4, 0)

    const auto built = buildCircuits (many, 48000.0, 1);
    INFO (built.error);
    REQUIRE (built.isValid());
    CHECK (static_cast<int> (built.probes.size()) == SchematicModel::maxTraceParts);
    CHECK (std::any_of (built.diagnostics.begin(), built.diagnostics.end(),
                        [] (const Diagnostic& d) { return d.text.contains ("not read"); }));
}
