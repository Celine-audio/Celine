#include <PluginProcessor.h>
#include <Schematic/Schematic.h>
#include <Schematic/SchematicBuilder.h>

#include <catch2/catch_test_macros.hpp>
#include <random>

using namespace SchematicModel;

namespace
{
    /** Every invariant a schematic is supposed to hold, whatever was done to it.

        Checked after each edit rather than at the end, so a failure names the
        operation that broke it instead of the hundred that came after. */
    void checkInvariants (const Schematic& sheet)
    {
        std::set<int> elementIds, wireIds;

        for (const auto& e : sheet.getElements())
        {
            REQUIRE (e.id > 0);
            REQUIRE (elementIds.insert (e.id).second);          // ids are unique
            REQUIRE (static_cast<int> (e.type) >= 0);
            REQUIRE (static_cast<int> (e.type) < numElementTypes);
            REQUIRE (e.orientation >= 0);
            REQUIRE (e.orientation < 4);

            // A part with models must be sitting on one that exists.
            if (e.hasModelChoice())
            {
                REQUIRE (e.modelIndex >= 0);
                REQUIRE (e.modelIndex < getModelChoices (e.type).size());
            }
        }

        for (const auto& w : sheet.getWires())
        {
            REQUIRE (w.id > 0);
            REQUIRE (wireIds.insert (w.id).second);

            // addWire splits diagonals, so nothing here may be one: a diagonal
            // conducts nothing, since Wire::contains cannot test it.
            REQUIRE ((w.isVertical() || w.isHorizontal()));
        }

        // Net extraction has to describe exactly the sheet it was given.
        const auto nets = sheet.extractNets();
        REQUIRE (nets.netOfPin.size() == sheet.getElements().size());

        for (size_t e = 0; e < sheet.getElements().size(); ++e)
        {
            const int pins = sheet.getElements()[e].getPinCount();

            for (int pin = 0; pin < pins; ++pin)
            {
                const int net = nets.netOfPin[e][static_cast<size_t> (pin)];
                REQUIRE (net >= 0);
                REQUIRE (net < static_cast<int> (nets.netNames.size()));
            }
        }
    }

    /** Save, load, save again: the second document must equal the first. */
    void checkRoundTrip (const Schematic& sheet)
    {
        Schematic reloaded;
        reloaded.restoreFromValueTree (sheet.toValueTree());

        REQUIRE (reloaded.getElements().size() == sheet.getElements().size());
        REQUIRE (reloaded.getWires().size() == sheet.getWires().size());
        REQUIRE (reloaded.toValueTree().isEquivalentTo (sheet.toValueTree()));
    }
}

TEST_CASE ("A schematic survives arbitrary editing", "[schematic][fuzz]")
{
    // Deterministic, so a failure is reproducible from the seed printed above.
    std::mt19937 rng { 0xC0FFEEu };

    Schematic sheet;
    std::vector<int> ids;

    const auto pick = [&rng] (int lo, int hi)
    {
        return std::uniform_int_distribution<int> (lo, hi) (rng);
    };

    for (int step = 0; step < 1500; ++step)
    {
        switch (pick (0, 9))
        {
            case 0: case 1: case 2:
            {
                const auto type = static_cast<ElementType> (pick (0, numElementTypes - 1));
                ids.push_back (sheet.addElement (type, pick (-20, 20), pick (-20, 20)));
                break;
            }

            case 3:
                if (! ids.empty())
                {
                    const int at = pick (0, static_cast<int> (ids.size()) - 1);
                    sheet.removeElement (ids[static_cast<size_t> (at)]);
                    ids.erase (ids.begin() + at);
                }
                break;

            case 4:
                if (auto* e = ids.empty() ? nullptr
                                          : sheet.findElement (ids[static_cast<size_t> (pick (0, (int) ids.size() - 1))]))
                {
                    e->x += pick (-3, 3);
                    e->y += pick (-3, 3);
                    e->orientation = pick (0, 3);
                    e->mirrored = pick (0, 1) != 0;
                }
                break;

            case 5: case 6:
                sheet.addWire ({ pick (-20, 20), pick (-20, 20) },
                               { pick (-20, 20), pick (-20, 20) });
                break;

            case 7:
                if (! sheet.getWires().empty())
                    sheet.removeWire (sheet.getWires()[static_cast<size_t> (
                        pick (0, (int) sheet.getWires().size() - 1))].id);
                break;

            case 8:
                if (! sheet.getWires().empty())
                {
                    const auto& w = sheet.getWires()[static_cast<size_t> (
                        pick (0, (int) sheet.getWires().size() - 1))];
                    sheet.resizeWireEnd (w.id, pick (0, 1), { pick (-20, 20), pick (-20, 20) });
                }
                break;

            case 9:
                sheet.mergeCollinearWires();
                break;
        }

        // Deleted elements must not linger in the id list.
        ids.erase (std::remove_if (ids.begin(), ids.end(),
                                   [&sheet] (int id) { return sheet.findElement (id) == nullptr; }),
                   ids.end());

        INFO ("step " << step);
        checkInvariants (sheet);
    }

    checkRoundTrip (sheet);
}

TEST_CASE ("Any sheet either builds or says why", "[schematic][fuzz]")
{
    // Whatever is thrown at it, buildCircuits must come back with a circuit or
    // a reason -- never a crash, and never a valid result holding nothing.
    std::mt19937 rng { 0x5EEDu };

    const auto pick = [&rng] (int lo, int hi)
    {
        return std::uniform_int_distribution<int> (lo, hi) (rng);
    };

    for (int trial = 0; trial < 40; ++trial)
    {
        Schematic sheet;

        for (int i = 0; i < pick (1, 25); ++i)
        {
            const auto type = static_cast<ElementType> (pick (0, numElementTypes - 1));
            const int id = sheet.addElement (type, pick (-8, 8), pick (-8, 8));

            if (auto* e = sheet.findElement (id))
            {
                e->orientation = pick (0, 3);
                e->value = pick (0, 4) == 0 ? 0.0 : std::pow (10.0, pick (-9, 6));
                e->valueB = std::pow (10.0, pick (0, 3));
            }
        }

        for (int i = 0; i < pick (0, 25); ++i)
            sheet.addWire ({ pick (-8, 8), pick (-8, 8) }, { pick (-8, 8), pick (-8, 8) });

        INFO ("trial " << trial << ", " << sheet.getElements().size() << " elements");

        auto result = buildCircuits (sheet, 48000.0, 1, {});

        if (result.isValid())
        {
            REQUIRE (! result.circuits.empty());
            REQUIRE (result.circuits[0] != nullptr);

            // A circuit that built has to be safe to run.
            for (int i = 0; i < 64; ++i)
                REQUIRE (std::isfinite (result.circuits[0]->process (0.01f)));
        }
        else
        {
            REQUIRE (result.error.isNotEmpty());
        }
    }
}
