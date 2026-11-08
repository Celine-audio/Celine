#include "helpers/test_helpers.h"

#include <PluginEditor.h>
#include <Schematic/Schematic.h>
#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace SchematicModel;

namespace
{
    /** A two-part sheet with a wire between them, at a known place. */
    Schematic makeBlock (int originX, int originY)
    {
        Schematic sheet;
        const int r = sheet.addElement (ElementType::Resistor, originX, originY);
        const int c = sheet.addElement (ElementType::Capacitor, originX + 6, originY);
        sheet.addWire ({ originX, originY + 2 }, { originX + 6, originY + 2 });

        if (auto* element = sheet.findElement (r)) element->value = 4700.0;
        if (auto* element = sheet.findElement (c)) element->value = 1.0e-7;

        return sheet;
    }
}

TEST_CASE ("Importing renumbers what it brings", "[import]")
{
    // Both sheets number from one, so a merge keeping ids would duplicate them.
    auto host = makeBlock (0, 0);
    const auto guest = makeBlock (0, 0);

    const auto before = host.getElements().size();
    const auto merged = host.merge (guest, { 20, 0 });

    REQUIRE (merged.elementIds.size() == guest.getElements().size());
    REQUIRE (host.getElements().size() == before + guest.getElements().size());

    std::set<int> ids;

    for (const auto& element : host.getElements())
        CHECK (ids.insert (element.id).second);

    std::set<int> wireIds;

    for (const auto& wire : host.getWires())
        CHECK (wireIds.insert (wire.id).second);

    // Every returned id must find something.
    for (const int id : merged.elementIds)
        CHECK (host.findElement (id) != nullptr);

    for (const int id : merged.wireIds)
        CHECK (host.findWire (id) != nullptr);
}

TEST_CASE ("Importing moves the whole block by one offset", "[import]")
{
    auto host = makeBlock (0, 0);
    const auto guest = makeBlock (0, 0);

    const juce::Point<int> delta { 25, -7 };
    const auto merged = host.merge (guest, delta);

    // Layout kept and shifted as one.
    for (size_t i = 0; i < merged.elementIds.size(); ++i)
    {
        const auto& source = guest.getElements()[i];
        const auto* arrived = host.findElement (merged.elementIds[i]);

        REQUIRE (arrived != nullptr);
        CHECK (arrived->x == source.x + delta.x);
        CHECK (arrived->y == source.y + delta.y);

        // Everything else about the part comes with it.
        CHECK (arrived->type == source.type);
        CHECK (juce::exactlyEqual (arrived->value, source.value));
    }

    for (size_t i = 0; i < merged.wireIds.size(); ++i)
    {
        const auto& source = guest.getWires()[i];
        const auto* arrived = host.findWire (merged.wireIds[i]);

        REQUIRE (arrived != nullptr);
        CHECK (arrived->a == source.a + delta);
        CHECK (arrived->b == source.b + delta);
    }
}

TEST_CASE ("An imported block lands clear of what is already drawn", "[import]")
{
    // Two sheets drawn at the same coordinates, the common case.
    auto host = makeBlock (0, 0);
    const auto guest = makeBlock (0, 0);

    const auto existing = host.getContentBounds();
    const auto arriving = guest.getContentBounds();

    REQUIRE (! existing.isEmpty());
    REQUIRE (! arriving.isEmpty());

    constexpr int gap = 4;
    const juce::Point<int> delta { existing.getRight() + gap - arriving.getX(),
                                   existing.getY() - arriving.getY() };

    const auto merged = host.merge (guest, delta);

    juce::Rectangle<int> landed;
    bool first = true;

    for (const int id : merged.elementIds)
    {
        const auto* element = host.findElement (id);
        REQUIRE (element != nullptr);
        const auto box = host.getElementBounds (*element);
        landed = first ? box : landed.getUnion (box);
        first = false;
    }

    INFO ("existing " << existing.toString() << "   landed " << landed.toString());
    CHECK (landed.getX() >= existing.getRight());
    CHECK (! landed.intersects (existing));
}

TEST_CASE ("Content bounds survive a sheet that is one straight wire", "[import]")
{
    // A lone vertical wire: the shape that collapses a naive Rectangle::getUnion.
    Schematic sheet;
    sheet.addWire ({ 5, 2 }, { 5, 9 });

    const auto bounds = sheet.getContentBounds();

    INFO (bounds.toString());
    CHECK (! bounds.isEmpty());
    CHECK (bounds.getWidth() > 0);
    CHECK (bounds.getHeight() >= 7);
}

TEST_CASE ("An empty sheet has empty content bounds", "[import]")
{
    const Schematic sheet;
    CHECK (sheet.getContentBounds().isEmpty());
}

TEST_CASE ("Importing carries a part's settings, cabinet included", "[import]")
{
    Schematic host;
    Schematic guest;

    const int outputId = guest.addElement (ElementType::Output, 3, 3);
    auto* output = guest.findElement (outputId);
    REQUIRE (output != nullptr);
    output->cabEnabled = true;
    output->cabFile = "/some/where/greenback.wav";
    output->label = "Speaker";

    const int potId = guest.addElement (ElementType::Potentiometer, 8, 3);
    auto* pot = guest.findElement (potId);
    REQUIRE (pot != nullptr);
    pot->controlPosition = 0.8;
    pot->taper = Taper::Logarithmic;
    pot->label = "Volume";

    const auto merged = host.merge (guest, { 0, 0 });
    REQUIRE (merged.elementIds.size() == 2);

    const auto* arrivedOutput = host.findElement (merged.elementIds[0]);
    REQUIRE (arrivedOutput != nullptr);
    CHECK (arrivedOutput->cabEnabled);
    CHECK (arrivedOutput->cabFile == "/some/where/greenback.wav");
    CHECK (arrivedOutput->label == "Speaker");

    const auto* arrivedPot = host.findElement (merged.elementIds[1]);
    REQUIRE (arrivedPot != nullptr);
    CHECK (juce::exactlyEqual (arrivedPot->controlPosition, 0.8));
    CHECK (arrivedPot->taper == Taper::Logarithmic);
}

TEST_CASE ("A sheet imported into an empty one keeps its own coordinates", "[import]")
{
    Schematic host;
    const auto guest = makeBlock (11, 4);

    const auto existing = host.getContentBounds();
    REQUIRE (existing.isEmpty());

    // Nothing to avoid, so no offset.
    const auto merged = host.merge (guest, {});

    const auto* first = host.findElement (merged.elementIds.front());
    REQUIRE (first != nullptr);
    CHECK (first->x == 11);
    CHECK (first->y == 4);
}

TEST_CASE ("Importing leaves the host's own parts untouched", "[import]")
{
    auto host = makeBlock (0, 0);

    std::vector<Element> before = host.getElements();
    const auto guest = makeBlock (0, 0);

    host.merge (guest, { 30, 0 });

    REQUIRE (host.getElements().size() > before.size());

    for (size_t i = 0; i < before.size(); ++i)
    {
        CHECK (host.getElements()[i].id == before[i].id);
        CHECK (host.getElements()[i].x == before[i].x);
        CHECK (host.getElements()[i].y == before[i].y);
        CHECK (juce::exactlyEqual (host.getElements()[i].value, before[i].value));
    }
}

// ==============================================================================
// The whole route through the editor: a real file, validated like any load, merged
// and selected.
// ==============================================================================

TEST_CASE ("Importing a file merges it and selects what arrived", "[import][gui]")
{
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("celine-import-" + juce::Uuid().toString());
    REQUIRE (folder.createDirectory());

    // A real .celsch, written as Save writes one.
    const auto file = folder.getChildFile ("stage.celsch");

    {
        PluginProcessor source;
        source.getSchematic().clear();
        source.getSchematic().addElement (ElementType::Resistor, 0, 0);
        source.getSchematic().addElement (ElementType::Capacitor, 6, 0);
        source.getSchematic().addWire ({ 0, 2 }, { 6, 2 });

        const auto xml = source.createDocument().createXml();
        REQUIRE (xml != nullptr);
        REQUIRE (xml->writeTo (file));
    }

    runWithinPluginEditor ([&] (PluginProcessor& plugin)
    {
        auto* editor = dynamic_cast<PluginEditor*> (plugin.getActiveEditor());
        REQUIRE (editor != nullptr);

        const auto before = plugin.getSchematic().getElements().size();
        const auto existing = plugin.getSchematic().getContentBounds();

        REQUIRE (editor->importSchematicFile (file));

        const auto& sheet = plugin.getSchematic();
        CHECK (sheet.getElements().size() == before + 2);

        // Landed clear of the existing drawing.
        juce::Rectangle<int> landed;
        bool first = true;

        for (size_t i = before; i < sheet.getElements().size(); ++i)
        {
            const auto box = sheet.getElementBounds (sheet.getElements()[i]);
            landed = first ? box : landed.getUnion (box);
            first = false;
        }

        INFO ("existing " << existing.toString() << "  landed " << landed.toString());
        CHECK (landed.getX() >= existing.getRight());

        // The whole block is selected, wires included.
        CHECK (editor->getCanvas().getSelection().size() == 2);
        CHECK (editor->getCanvas().getSelectedWires().size() == 1);
    });

    folder.deleteRecursively();
}

TEST_CASE ("A file that isn't ours is refused rather than half-merged", "[import][gui]")
{
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("celine-import-" + juce::Uuid().toString());
    REQUIRE (folder.createDirectory());

    // Right suffix, wrong contents.
    const auto impostor = folder.getChildFile ("nope.celsch");
    impostor.replaceWithText ("<AMPMODELLING><SCHEMATIC/></AMPMODELLING>");

    const auto notXml = folder.getChildFile ("rubbish.celsch");
    notXml.replaceWithText ("this is not xml at all");

    runWithinPluginEditor ([&] (PluginProcessor& plugin)
    {
        auto* editor = dynamic_cast<PluginEditor*> (plugin.getActiveEditor());
        REQUIRE (editor != nullptr);

        const auto before = plugin.getSchematic().getElements().size();

        CHECK (! editor->importSchematicFile (impostor));
        CHECK (! editor->importSchematicFile (notXml));
        CHECK (! editor->importSchematicFile (folder.getChildFile ("not-there.celsch")));

        // The drawing is untouched.
        CHECK (plugin.getSchematic().getElements().size() == before);
    });

    folder.deleteRecursively();
}
