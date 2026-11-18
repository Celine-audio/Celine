#include "helpers/test_helpers.h"

#include <PluginEditor.h>
#include <Schematic/SchematicBuilder.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// The two parts that read and load a circuit: the probe, one pin against ground on a
// shared screen, and the speaker, a real driver's impedance.

using namespace SchematicModel;

namespace
{
    using Complex = std::complex<double>;

    /** Input, 1k, then the speaker to ground, the output across it: the divider reads the
        speaker's impedance, H = Z / (Z + 1k). */
    Schematic divider (int speakerModel)
    {
        Schematic sheet;
        sheet.addElement (ElementType::Input, -6, 0);                   // pin (-4, 0)
        const auto r = sheet.addElement (ElementType::Resistor, -4, 2); // (-4, 0) - (-4, 4)
        const auto s = sheet.addElement (ElementType::Speaker, -4, 6);  // (-4, 4) - (-4, 8)
        sheet.addElement (ElementType::Ground, -4, 10);                 // pin (-4, 8)
        sheet.addElement (ElementType::Output, -2, 4);                  // pin (-4, 4)
        sheet.findElement (r)->value = 1000.0;
        sheet.findElement (s)->modelIndex = speakerModel;
        return sheet;
    }

    struct Published
    {
        double re, le, fs, qms, qts;
    };

    /** The 8 ohm columns of Jensen's pages, as printed (Qes is derived; see the builder). */
    Published published (int model)
    {
        switch (model)
        {
            case 1: return { 6.05, 0.5e-3, 80.0, 13.07, 2.1 };   // P12R
            case 2: return { 6.05, 0.9e-3, 113.0, 7.52, 1.02 };  // C12N
            default: return { 5.6, 0.67e-3, 90.5, 11.57, 2.03 }; // P12Q
        }
    }

    constexpr double rate = 48000.0;

    /** The Thiele-Small impedance, at the frequency the engine's trapezoidal rule puts a
        reactance at: it warps every inductor and capacitor alike. */
    Complex impedance (const Published& p, double hz)
    {
        const double w = 2.0 * rate * std::tan (juce::MathConstants<double>::pi * hz / rate);
        const double qes = p.qms * p.qts / (p.qms - p.qts);
        const double ws = juce::MathConstants<double>::twoPi * p.fs;
        const double cmes = qes / (ws * p.re), lces = 1.0 / (ws * ws * cmes), res = p.re * p.qms / qes;

        const Complex j (0.0, 1.0);
        const Complex motional = 1.0 / (1.0 / res + 1.0 / (j * w * lces) + j * w * cmes);
        return p.re + j * w * p.le + motional;
    }
    /** The divider's impedance at about `hz`, driven with a steady sine: a whole number of
        cycles to the block, so one DFT bin reads it exactly. `hz` comes back as the
        frequency actually run. */
    Complex measuredImpedance (Circuit& circuit, double& hz)
    {
        const int cycles = std::max (4, static_cast<int> (std::ceil (0.05 * hz)));
        const auto length = static_cast<int> (std::round (cycles * rate / hz));
        hz = cycles * rate / length;

        circuit.reset();
        const double step = juce::MathConstants<double>::twoPi * hz / rate;

        // Half a second to settle (the cone rings for a few tens of milliseconds), then a
        // block read.
        const int settle = static_cast<int> (0.5 * rate);
        Complex in, out;

        for (int n = 0; n < settle + length; ++n)
        {
            const double x = 0.1 * std::sin (step * n);
            const double y = circuit.process (static_cast<float> (x));

            if (n >= settle)
            {
                const Complex turn = std::polar (1.0, -step * (n - settle));
                in += x * turn;
                out += y * turn;
            }
        }

        const auto h = out / in;
        return 1000.0 * h / (1.0 - h);
    }
} // namespace

TEST_CASE ("A speaker loads the circuit with its maker's impedance", "[speaker]")
{
    for (int model = 0; model < 3; ++model)
    {
        INFO ("speaker model " << model);
        const auto sheet = divider (model);

        auto built = buildCircuits (sheet, rate, 1);
        INFO (built.error);
        REQUIRE (built.isValid());
        auto& circuit = *built.circuits.front();

        const auto p = published (model);

        for (const double target : { 30.0, p.fs, 300.0, 1000.0, 5000.0 })
        {
            double hz = target;
            const auto measured = measuredImpedance (circuit, hz);
            INFO (hz << " Hz");
            CHECK (std::abs (measured) == Catch::Approx (std::abs (impedance (p, hz))).epsilon (0.01));
        }

        // The resonance: the impedance peaks at Fs, higher there than a sixth either side.
        // (Above a kilohertz the coil's inductance takes it higher still: 84 ohms at 20 kHz
        // for the P12Q.)
        double below = p.fs * 0.85, at = p.fs, above = p.fs * 1.15;
        const double peak = std::abs (measuredImpedance (circuit, at));
        CHECK (peak > std::abs (measuredImpedance (circuit, below)));
        CHECK (peak > std::abs (measuredImpedance (circuit, above)));
    }
}

TEST_CASE ("A probe is read against ground and shares the probes' screen", "[probe]")
{
    Schematic sheet = divider (0);
    const auto probe = sheet.addElement (ElementType::Probe, -6, 2); // pin (-6, 4)
    sheet.addWire ({ -6, 4 }, { -4, 4 });

    const auto built = buildCircuits (sheet, 48000.0, 1);
    REQUIRE (built.isValid());

    const auto found = std::find_if (built.probes.begin(), built.probes.end(),
                                     [probe] (const ScopeProbe& p) { return p.elementId == probe; });
    REQUIRE (found != built.probes.end());
    CHECK (found->referenceNode == "gnd");
    CHECK (found->positiveIndex >= 0);

    // In the editor: the display comes with the first probe and goes with the last.
    runWithinPluginEditor ([] (PluginProcessor& plugin) {
        auto* editor = dynamic_cast<PluginEditor*> (plugin.getActiveEditor());
        REQUIRE (editor != nullptr);
        editor->setSize (1400, 900);

        Celine::TraceWindow* display = nullptr;
        Celine::TraceWindowButton* button = nullptr;
        for (auto* child : editor->getChildren())
        {
            if (auto* d = dynamic_cast<Celine::TraceWindow*> (child))
                display = d;
            if (auto* b = dynamic_cast<Celine::TraceWindowButton*> (child))
                button = b;
        }
        REQUIRE (display != nullptr);
        REQUIRE (button != nullptr);
        CHECK_FALSE (display->isVisible());
        CHECK_FALSE (button->isVisible());

        auto& schematic = plugin.getSchematic();
        const int id = schematic.addElement (ElementType::Probe, 0, 0);
        editor->schematicChangedExternally();
        CHECK (display->isVisible());
        CHECK_FALSE (button->isVisible());
        CHECK (display->has (Celine::TraceWindow::Kind::probes));
        CHECK_FALSE (display->has (Celine::TraceWindow::Kind::scopes));
        CHECK (editor->getLocalBounds().contains (display->getBounds()));

        // The card is centred on the sheet; its tabs hang off its left edge.
        Celine::SchematicCanvas* canvas = nullptr;
        for (auto* child : editor->getChildren())
            if (auto* c = dynamic_cast<Celine::SchematicCanvas*> (child))
                canvas = c;
        REQUIRE (canvas != nullptr);
        const int cardCentre = display->getX() + Celine::TraceWindow::tabWidth + (display->getWidth() - Celine::TraceWindow::tabWidth) / 2;
        CHECK (std::abs (cardCentre - canvas->getBounds().getCentreX()) <= 1);

        // A legend entry switches its trace off and on.
        CHECK (display->isShown (id));
        display->setShown (id, false);
        CHECK_FALSE (display->isShown (id));
        display->setShown (id, true);
        CHECK (display->isShown (id));

        // A scope joins it, on a tab of its own.
        const int scope = schematic.addElement (ElementType::Scope, 10, 0);
        editor->schematicChangedExternally();
        CHECK (display->has (Celine::TraceWindow::Kind::scopes));

        // Hidden, it folds to the button above the band; the button brings it back.
        display->onHideRequested();
        CHECK_FALSE (display->isVisible());
        CHECK (button->isVisible());
        CHECK (button->getBounds().getBottom() < editor->getHeight());
        button->onClick();
        CHECK (display->isVisible());
        CHECK_FALSE (button->isVisible());

        schematic.removeElement (id);
        schematic.removeElement (scope);
        editor->schematicChangedExternally();
        CHECK_FALSE (display->isVisible());
        CHECK_FALSE (button->isVisible());
    });
}

TEST_CASE ("The traces' time span steps from 5 ms to 4 s and is kept", "[probe]")
{
    Celine::TraceWindow window;
    window.setTraces ({ { 1, "Probe 1" } }, {});
    window.setWindowSeconds (Celine::TraceWindow::Kind::probes, 0.04);
    CHECK (window.getSpanText() == "40 ms");

    double told = 0.0;
    window.onWindowChanged = [&told] (Celine::TraceWindow::Kind, double seconds) { told = seconds; };

    juce::Button* shorter = nullptr;
    juce::Button* longer = nullptr;
    for (auto* child : window.getChildren())
        if (auto* b = dynamic_cast<juce::Button*> (child))
        {
            if (b->getName().startsWith ("Longer"))
                longer = b;
            else if (b->getName().startsWith ("Shorter"))
                shorter = b;
        }
    REQUIRE (shorter != nullptr);
    REQUIRE (longer != nullptr);

    // Up to the ceiling, where the plus fades and stops.
    for (int i = 0; i < 20 && longer->isEnabled(); ++i)
        longer->onClick();
    CHECK (window.getSpanText() == "4 s");
    CHECK (told == Catch::Approx (4.0));
    CHECK_FALSE (longer->isEnabled());

    for (int i = 0; i < 20 && shorter->isEnabled(); ++i)
        shorter->onClick();
    CHECK (window.getSpanText() == "5 ms");
    CHECK_FALSE (shorter->isEnabled());

    // The part keeps 4 s through a save; anything longer comes back at 4 s.
    Schematic sheet;
    const int keeps = sheet.addElement (ElementType::Probe, 0, 0);
    const int clamps = sheet.addElement (ElementType::Probe, 10, 0);
    sheet.findElement (keeps)->scopeSeconds = 4.0;
    sheet.findElement (clamps)->scopeSeconds = 10.0;

    Schematic back;
    back.restoreFromValueTree (sheet.toValueTree());
    CHECK (back.findElement (keeps)->scopeSeconds == Catch::Approx (4.0));
    CHECK (back.findElement (clamps)->scopeSeconds == Catch::Approx (Element::maxScopeSeconds));
}

TEST_CASE ("Every probe gets a legend entry and a colour of its own", "[probe]")
{
    // Twelve probes, the most a sheet shows: the legend wraps to hold them all and the window grows to fit it.
    std::vector<Celine::TraceWindow::Trace> many;
    for (int i = 1; i <= SchematicModel::maxTraceParts; ++i)
        many.push_back ({ i, "Probe " + juce::String (i) });

    Celine::TraceWindow window;
    window.setTraces ({ { 1, "Probe 1" } }, {});
    const int oneRow = window.getPreferredHeight (Celine::TraceWindow::preferredWidth);
    CHECK (oneRow == Celine::TraceWindow::preferredHeight);

    window.setTraces (many, {});
    CHECK (window.getPreferredHeight (Celine::TraceWindow::preferredWidth) > oneRow);

    // Twelve colours, no two alike.
    for (int a = 0; a < SchematicModel::maxTraceParts; ++a)
        for (int b = a + 1; b < SchematicModel::maxTraceParts; ++b)
        {
            INFO (a << " and " << b);
            CHECK (Celine::TraceWindow::colourFor (a) != Celine::TraceWindow::colourFor (b));
        }
}

TEST_CASE ("The traces' range holds still while a wave's peaks vary", "[probe]")
{
    using Celine::SteadyRange;

    CHECK (SteadyRange::roundUp (1.0) == Catch::Approx (1.0));
    CHECK (SteadyRange::roundUp (0.0042) == Catch::Approx (0.005));
    CHECK (SteadyRange::roundUp (7.0) == Catch::Approx (8.0));
    CHECK (SteadyRange::roundUp (1.26) == Catch::Approx (1.5));

    // An asymmetric wave about zero: symmetric, zero in the middle.
    SteadyRange range;
    range.update (-0.3, 0.9, 0.0);
    CHECK (range.low == Catch::Approx (-1.0));
    CHECK (range.high == Catch::Approx (1.0));

    // Frame to frame its peaks vary; the range does not.
    range.update (-0.28, 0.85, 16.0);
    range.update (-0.31, 0.95, 32.0);
    CHECK (range.high == Catch::Approx (1.0));

    // Past the top it grows at once.
    range.update (-0.3, 1.2, 48.0);
    CHECK (range.low == Catch::Approx (-1.5));
    CHECK (range.high == Catch::Approx (1.5));

    // Quieter, it waits before coming back in.
    range.update (-0.3, 0.7, 100.0);
    range.update (-0.3, 0.7, 1000.0);
    CHECK (range.high == Catch::Approx (1.5));
    range.update (-0.3, 0.7, 100.0 + SteadyRange::shrinkDelayMs);
    CHECK (range.high == Catch::Approx (0.8));

    // A loud frame in between starts the wait again.
    range.update (-0.2, 0.3, 2000.0);
    range.update (-0.2, 0.79, 2500.0);
    range.update (-0.2, 0.3, 3000.0);
    range.update (-0.2, 0.3, 2000.0 + SteadyRange::shrinkDelayMs);
    CHECK (range.high == Catch::Approx (0.8));
    range.update (-0.2, 0.3, 3000.0 + SteadyRange::shrinkDelayMs);
    CHECK (range.high == Catch::Approx (0.4));

    // A small swing far from zero is shown as itself, on round ends.
    range.reset();
    range.update (199.0, 201.0, 0.0);
    CHECK (range.low <= 199.0);
    CHECK (range.high >= 201.0);
    CHECK (range.high - range.low < 5.0);
    CHECK (range.low > 195.0);
}

TEST_CASE ("The trigger holds a repeating wave still", "[probe]")
{
    // A ring of twice the span shown, filled with a sine about 2 V (an offset, as a plate
    // would sit), its newest column at any phase.
    constexpr int shown = 400, count = 2 * shown;
    constexpr double period = 57.3; // columns: not a divisor of anything

    const auto ring = [] (double phase)
    {
        Celine::ScopeReading r;
        r.count = count;
        r.writeColumn = 123; // the oldest column; age 0
        r.live = true;

        for (int age = 0; age < count; ++age)
        {
            const auto v = static_cast<float> (2.0 + std::sin (juce::MathConstants<double>::twoPi * (age + phase) / period));
            const int c = (r.writeColumn + age) % count;
            r.minimum[c] = r.maximum[c] = v;
        }
        return r;
    };

    const auto shape = [&] (const Celine::ScopeReading& r, int from)
    {
        std::vector<float> v;
        for (int k = 0; k < shown; ++k)
            v.push_back (r.maximum[(r.writeColumn + from + k) % count]);
        return v;
    };

    // However far the wave has moved on, the span shown starts at the same point of it: a
    // tenth of the way in, rising through its average.
    std::vector<float> first;
    for (const double phase : { 0.0, 7.0, 19.5, 33.0, 50.0 })
    {
        INFO ("phase " << phase);
        const auto r = ring (phase);
        const auto from = Celine::TraceWindow::triggeredStart (r, shown, shown);
        REQUIRE (from.has_value());
        CHECK (*from + shown <= count);

        const auto at = shape (r, *from);
        CHECK (at[shown / 10 - 1] < 2.0f);
        CHECK (at[shown / 10] >= 2.0f);

        if (first.empty())
            first = at;
        else
            for (size_t k = 0; k < first.size(); k += 37)
                CHECK (at[k] == Catch::Approx (first[k]).margin (0.12)); // within a column's step
    }

    // Silence, or a level that never moves: nothing to trigger on, so it runs free.
    Celine::ScopeReading flat;
    flat.count = count;
    flat.live = true;
    CHECK_FALSE (Celine::TraceWindow::triggeredStart (flat, shown, shown).has_value());
}

TEST_CASE ("The trigger lets go of a wave that has stopped", "[probe]")
{
    // A 4 s span: a sine, then the level it swung about, as a note dying away leaves. The
    // trigger held its last crossing until that left the ring, so the screen showed the same
    // old picture for up to a whole span (4 s) while new readings came in.
    constexpr int shown = 1024;
    constexpr double span = 4.0, period = 37.0; // columns
    const int allowed = Celine::TraceWindow::staleColumnsAllowed (span, shown);

    // A quarter of a second, or a tenth of the span when longer, never past the span.
    CHECK (allowed == shown / 10);
    CHECK (Celine::TraceWindow::staleColumnsAllowed (1.0, shown) == shown / 4);
    CHECK (Celine::TraceWindow::staleColumnsAllowed (0.04, shown) == shown);

    std::vector<float> stream;
    constexpr int stops = 6000;
    for (int i = 0; i < 9000; ++i)
        stream.push_back (i < stops ? static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * i / period)) : 0.0f);

    const auto startAt = [&stream] (int now, int stale)
    {
        Celine::ScopeReading r;
        r.count = 2 * shown;
        r.writeColumn = 0;
        r.filled = r.count;
        r.live = true;
        for (int age = 0; age < r.count; ++age)
            r.minimum[age] = r.maximum[age] = stream[static_cast<size_t> (now - r.count + age)];
        return Celine::TraceWindow::triggeredStart (r, shown, stale);
    };

    // Running, it triggers on every read, however little it is allowed to hold.
    for (int now = 3000; now <= stops; now += 97)
    {
        INFO ("now " << now);
        CHECK (startAt (now, allowed).has_value());
    }

    // Stopped, the picture slides on for most of a span, the level coming in from the right,
    // until the last crossing is a span (less the lead) old. From there the old trigger held
    // that picture until the crossing left the ring, about a span more; now it runs free
    // within what it is allowed (a period's slack for where the last crossing fell).
    const int lead = shown / 10, held = stops + shown - lead;
    CHECK (startAt (held + shown / 2, shown).has_value()); // allowed the whole span: still held

    for (int now = held + allowed + static_cast<int> (period) + 1; now < held + shown - period; now += 31)
    {
        INFO ("now " << now);
        CHECK_FALSE (startAt (now, allowed).has_value());
    }
}

TEST_CASE ("A probe placed later takes the span its kind is set to", "[probe]")
{
    // A new probe starts at 40 ms; the window shows its kind at the first part's span. Left
    // at 40 ms, its ring was drawn across the others' 1 s axis.
    runWithinPluginEditor ([] (PluginProcessor& plugin) {
        auto* editor = dynamic_cast<PluginEditor*> (plugin.getActiveEditor());
        REQUIRE (editor != nullptr);
        auto& sheet = plugin.getSchematic();

        const int first = sheet.addElement (ElementType::Probe, 0, 0);
        sheet.findElement (first)->scopeSeconds = 1.0;
        editor->schematicChangedExternally();

        const int later = sheet.addElement (ElementType::Probe, 10, 0);
        const int scope = sheet.addElement (ElementType::Scope, 20, 0);
        REQUIRE (sheet.findElement (later)->scopeSeconds == Catch::Approx (0.04));
        editor->schematicChangedExternally();

        CHECK (sheet.findElement (later)->scopeSeconds == Catch::Approx (1.0));
        CHECK (sheet.findElement (scope)->scopeSeconds == Catch::Approx (0.04)); // scopes have their own

        sheet.removeElement (first);
        sheet.removeElement (later);
        sheet.removeElement (scope);
        editor->schematicChangedExternally();
    });
}

TEST_CASE ("The traces move at the display's pace, not at every repaint", "[probe]")
{
    // A repaint the sheet beneath asks for must show the same frame; only the display's tick
    // (refresh) reads the traces again, and not while paused.
    int reads = 0;
    Celine::TraceWindow window;
    window.setSize (Celine::TraceWindow::preferredWidth, Celine::TraceWindow::preferredHeight);
    window.reader = [&reads] (int, Celine::ScopeReading& r)
    {
        ++reads;
        r.live = true;
        r.count = 64;
        return true;
    };

    window.setTraces ({ { 1, "Probe 1" } }, {});
    const int afterSetup = reads;
    CHECK (afterSetup >= 1); // a new list is read once, to have something to draw

    for (int i = 0; i < 3; ++i)
        window.createComponentSnapshot (window.getLocalBounds());
    CHECK (reads == afterSetup);

    window.refresh();
    CHECK (reads == afterSetup + 1);

    window.setPaused (true);
    window.refresh();
    CHECK (reads == afterSetup + 1);

    window.setPaused (false);
    window.refresh();
    CHECK (reads == afterSetup + 2);
}

TEST_CASE ("A new time span starts empty, not at 0 V", "[probe]")
{
    // A probe on a divider fed a steady 0.5 V: it reads 0.25 V. Changing the span clears the
    // capture, and until it fills again the cleared columns must count as nothing -- read as
    // 0 V, they dragged the average down and put the trace off centre.
    PluginProcessor plugin;
    auto& sheet = plugin.getSchematic();
    {
        Schematic divider;
        divider.addElement (ElementType::Input, -6, 0);                    // pin (-4, 0)
        const auto r1 = divider.addElement (ElementType::Resistor, -4, 2); // (-4, 0) - (-4, 4)
        const auto r2 = divider.addElement (ElementType::Resistor, -4, 6); // (-4, 4) - (-4, 8)
        divider.findElement (r1)->value = 1000.0;
        divider.findElement (r2)->value = 1000.0;
        divider.addElement (ElementType::Ground, -4, 10);                  // pin (-4, 8)
        divider.addElement (ElementType::Output, -2, 4);                   // pin (-4, 4)
        divider.addElement (ElementType::Probe, -6, 2);                    // pin (-6, 4)
        divider.addWire ({ -6, 4 }, { -4, 4 });
        sheet.restoreFromValueTree (divider.toValueTree());
    }

    int probe = 0;
    for (const auto& e : sheet.getElements())
        if (e.type == ElementType::Probe)
            probe = e.id;

    const auto result = plugin.rebuild();
    INFO (result.error);
    REQUIRE (result.isValid());
    plugin.prepareToPlay (48000.0, 512);

    const auto pump = [&plugin] (int blocks)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        for (int b = 0; b < blocks; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 0.5f, 512);
            plugin.processBlock (buffer, midi);
        }
    };

    pump (20);
    const auto* trace = plugin.getScopeTrace (probe);
    REQUIRE (trace != nullptr);
    CHECK (trace->dcAverage.load() == Catch::Approx (0.25).epsilon (0.01));

    // A one-second span: two blocks fill a sliver of it.
    sheet.findElement (probe)->scopeSeconds = 1.0;
    plugin.refreshScopeTiming();
    pump (2);

    CHECK (trace->filled.load() > 0);
    CHECK (trace->filled.load() < trace->columnCount.load() / 2);
    CHECK (trace->dcAverage.load() == Catch::Approx (0.25).epsilon (0.01));

    // The window, reading the same: its level is the node's, and the trigger waits for a
    // whole span rather than firing on the cleared part.
    Celine::ScopeReading reading;
    reading.writeColumn = trace->writeColumn.load();
    reading.count = trace->columnCount.load();
    reading.filled = trace->filled.load();
    reading.live = true;
    for (int c = 0; c < reading.count; ++c)
    {
        reading.minimum[c] = trace->minimum[c].load();
        reading.maximum[c] = trace->maximum[c].load();
    }

    CHECK (reading.firstFilled() == reading.count - reading.filled);
    CHECK_FALSE (Celine::TraceWindow::triggeredStart (reading, reading.count / 2, reading.count / 2).has_value());
}
