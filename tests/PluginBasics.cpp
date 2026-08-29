#include "helpers/test_helpers.h"
#include <PluginProcessor.h>
#include <CelineEngine/Components/FastMath.h>
#include <Schematic/ExampleSchematics.h>
#include <UI/ControlStrip.h>
#include <UI/SchematicCanvas.h>
#include <UI/Theme.h>
#include <juce_dsp/juce_dsp.h>
#include <set>
#include <thread>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

TEST_CASE ("Plugin instance", "[instance]")
{
    PluginProcessor testPlugin;

    SECTION ("name")
    {
        // PRODUCT_NAME carries the version, which the build bumps, so match the
        // stable part rather than the whole string.
        CHECK_THAT (testPlugin.getName().toStdString(),
                    Catch::Matchers::ContainsSubstring (std::string ("Celine")));

        // And every character ASCII, deliberately. This name becomes both the
        // VST3 bundle's directory on disk and its class-info string, and JUCE
        // mangles the latter through a byte-at-a-time widening -- so an accent
        // here shows up as garbage in Windows hosts. Asserted rather than left
        // to a comment, because it is an easy and very confusing thing to
        // reintroduce.
        for (const auto character : testPlugin.getName())
            CHECK (character < 128);
    }
}


#ifdef PAMPLEJUCE_IPP
    #include <ipp.h>

TEST_CASE ("IPP version", "[ipp]")
{
    #if defined(__APPLE__)
        // macOS uses 2021.9.1 from pip wheel (only x86_64 version available)
        CHECK_THAT (ippsGetLibVersion()->Version, Catch::Matchers::Equals ("2021.9.1 (r0x7e208212)"));
    #else
        CHECK_THAT (ippsGetLibVersion()->Version, Catch::Matchers::Equals ("2026.0.0 (r0xa7ad6ebc)"));
    #endif
}
#endif

//==============================================================================
// The plugin end to end: parameters through the amp and back out as audio.
//==============================================================================

TEST_CASE ("Plugin exposes a fixed parameter set", "[plugin]")
{
    PluginProcessor plugin;

    juce::StringArray ids;
    for (auto* p : plugin.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            ids.add (withId->paramID);

    // Input, Output and Bypass are ours and always there. The knobs are a fixed
    // pool: parameters have to be declared once at construction, but a
    // schematic's controls come and go as it is drawn, so the drawn pots are
    // mapped onto these in order.
    for (auto* expected : { "input", "output", "bypass", "channels" })
        CHECK (ids.contains (expected));

    for (int i = 0; i < PluginProcessor::maxLiveControls; ++i)
        CHECK (ids.contains (PluginProcessor::getControlParameterId (i)));

    CHECK (ids.size() == 4 + PluginProcessor::maxLiveControls);

    // The host gets a real bypass rather than being left to fade around us.
    CHECK (plugin.getBypassParameter() != nullptr);
}

namespace
{
    /** Every message a build produced, as one string, for tests that only care
        that something was said. */
    juce::String joinDiagnostics (const SchematicModel::BuildResult& result)
    {
        juce::StringArray all;

        for (const auto& d : result.diagnostics)
            all.add (d.subject + " " + d.text);

        return all.joinIntoString (" | ");
    }

    /** Fills a stereo buffer with a sine, advancing `phase`. */
    void fillSine (juce::AudioBuffer<float>& buffer, double& phase, double frequency, float amplitude)
    {
        const double step = 2.0 * juce::MathConstants<double>::pi * frequency / 48000.0;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            buffer.setSample (0, i, static_cast<float> (amplitude * std::sin (phase)));
            buffer.setSample (1, i, buffer.getSample (0, i));
            phase += step;
        }
    }

    void setParameter (PluginProcessor& plugin, const juce::String& id, float value)
    {
        auto* p = plugin.apvts.getParameter (id);
        REQUIRE (p != nullptr);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    /** Runs the plugin and returns the peak of the settled output. */
    float measurePeak (PluginProcessor& plugin, float amplitude, double frequency = 1000.0)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        double phase = 0.0;
        float peak = 0.0f;

        for (int block = 0; block < 40; ++block)
        {
            fillSine (buffer, phase, frequency, amplitude);
            plugin.processBlock (buffer, midi);

            if (block >= 20) // let the coupling capacitors settle
                for (int i = 0; i < 512; ++i)
                {
                    const float out = buffer.getSample (0, i);
                    REQUIRE (std::isfinite (out));
                    peak = std::max (peak, std::abs (out));
                }
        }

        return peak;
    }
}

TEST_CASE ("Bypass passes the input through untouched", "[plugin]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (48000.0, 512);

    setParameter (plugin, "input", 0.0f);
    setParameter (plugin, "output", 0.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    double phase = 0.0;

    // Warm up engaged, then bypass. The circuit keeps running while bypassed --
    // its capacitors would otherwise hold stale charge and thump on re-engaging
    // -- so what this checks is that the *output* is the untouched input.
    setParameter (plugin, "bypass", 0.0f);
    for (int block = 0; block < 20; ++block) { fillSine (buffer, phase, 440.0, 0.5f); plugin.processBlock (buffer, midi); }

    setParameter (plugin, "bypass", 1.0f);

    juce::AudioBuffer<float> expected (2, 512);
    for (int block = 0; block < 5; ++block)
    {
        fillSine (buffer, phase, 440.0, 0.5f);
        expected.makeCopyOf (buffer);
        plugin.processBlock (buffer, midi);

        // Bit-exact is the requirement -- bypass must not touch the signal at
        // all. Written as a magnitude to keep -Wfloat-equal quiet.
        for (int i = 0; i < 512; ++i)
            REQUIRE (std::abs (buffer.getSample (0, i) - expected.getSample (0, i)) == 0.0f);
    }
}

TEST_CASE ("Plugin runs the drawn circuit", "[plugin]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (48000.0, 512);

    setParameter (plugin, "input", 0.0f);
    setParameter (plugin, "output", 0.0f);
    setParameter (plugin, "bypass", 0.0f);

    // The default sheet is the diode clipper, whose one pot is labelled Volume.
    const auto controls = plugin.getLiveControls();
    REQUIRE (controls.size() == 1);
    CHECK (controls[0].name == "Volume");

    // The knob is live: it moves the output with no rebuild in between, which is
    // the whole point of separating controls from topology.
    setParameter (plugin, PluginProcessor::getControlParameterId (0), 1.0f);
    const float wideOpen = measurePeak (plugin, 0.5f);

    setParameter (plugin, PluginProcessor::getControlParameterId (0), 0.1f);
    const float turnedDown = measurePeak (plugin, 0.5f);

    INFO ("volume wide open " << wideOpen << ", turned down " << turnedDown);
    CHECK (wideOpen > turnedDown * 2.0f);
    CHECK (turnedDown > 0.0f);

    // And it clips: the diodes hold the level down as the input goes up, so
    // doubling the input must not double the output.
    setParameter (plugin, PluginProcessor::getControlParameterId (0), 1.0f);
    const float quiet = measurePeak (plugin, 0.25f);
    const float loud  = measurePeak (plugin, 1.0f);

    INFO ("0.25 in gives " << quiet << ", 1.0 in gives " << loud);
    CHECK (loud > quiet);
    CHECK (loud < quiet * 4.0f);
}

TEST_CASE ("Editing the schematic and rebuilding changes what is heard", "[plugin]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (48000.0, 512);

    setParameter (plugin, "input", 0.0f);
    setParameter (plugin, "output", 0.0f);
    setParameter (plugin, "bypass", 0.0f);
    setParameter (plugin, PluginProcessor::getControlParameterId (0), 1.0f);

    // Driven well past the silicon knee, so the diodes are genuinely holding the
    // level down -- at a level where they barely conduct, swapping them proves
    // very little.
    const float before = measurePeak (plugin, 3.0f);

    // Swap the clipping diodes for blue LEDs, which clip far later, and rebuild.
    int changed = 0;
    for (auto& element : plugin.getSchematic().getElements())
        if (element.type == SchematicModel::ElementType::Diode)
        {
            element.modelIndex = 5; // blue LED
            ++changed;
        }

    REQUIRE (changed == 2);
    REQUIRE (plugin.rebuild().isValid());

    const float after = measurePeak (plugin, 3.0f);

    // Blue LEDs need about three volts before they conduct, so at this level
    // they barely clip at all and far more signal gets through.
    INFO ("silicon " << before << ", blue LED " << after);
    CHECK (after > before * 1.5f);
}

TEST_CASE ("Channel mode collapses the circuit to one instance", "[plugin]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (48000.0, 512);

    setParameter (plugin, "input", 0.0f);
    setParameter (plugin, "output", 0.0f);
    setParameter (plugin, "bypass", 0.0f);
    setParameter (plugin, PluginProcessor::getControlParameterId (0), 1.0f);

    // Different tones per side, so which one came out is unambiguous.
    auto run = [&plugin] (int mode)
    {
        setParameter (plugin, "channels", static_cast<float> (mode));

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        double phase = 0.0;

        for (int block = 0; block < 40; ++block)
        {
            for (int i = 0; i < 512; ++i)
            {
                buffer.setSample (0, i, static_cast<float> (0.5 * std::sin (phase)));
                buffer.setSample (1, i, static_cast<float> (0.5 * std::sin (phase * 3.0)));
                phase += 2.0 * juce::MathConstants<double>::pi * 220.0 / 48000.0;
            }

            plugin.processBlock (buffer, midi);
        }

        return std::pair { buffer.getMagnitude (0, 0, 512), buffer.getMagnitude (1, 0, 512) };
    };

    // Stereo runs a circuit per side, so two different inputs give two different
    // outputs.
    const auto [stereoLeft, stereoRight] = run (0);
    CHECK (std::abs (stereoLeft - stereoRight) > 1.0e-6f);

    // Every mono mode runs one circuit and copies its output to both sides --
    // that copy is the saving, and identical channels are what proves it happened.
    for (int mode = 1; mode <= 3; ++mode)
    {
        const auto [left, right] = run (mode);
        INFO ("mode " << mode << ": left " << left << ", right " << right);
        CHECK (std::abs (left - right) == 0.0f);
        CHECK (left > 0.0f);
    }
}

TEST_CASE ("Editor size rides with the session, not with a preset", "[plugin]")
{
    PluginProcessor plugin;
    plugin.editorWidth = 1480;
    plugin.editorHeight = 900;

    // A .celsch is a circuit, not a window. Carrying a size would mean loading
    // someone else's pedal resized your plugin.
    const auto preset = plugin.createDocument();
    CHECK (! preset.hasProperty ("editorWidth"));

    juce::MemoryBlock state;
    plugin.getStateInformation (state);

    PluginProcessor reopened;
    reopened.setStateInformation (state.getData(), static_cast<int> (state.getSize()));

    CHECK (reopened.editorWidth.load() == 1480);
    CHECK (reopened.editorHeight.load() == 900);
}


TEST_CASE ("Resizing the editor gives every extra pixel to the canvas", "[plugin]")
{
    runWithinPluginEditor ([] (PluginProcessor& plugin) {
        auto* editor = plugin.getActiveEditor();
        REQUIRE (editor->isResizable());

        // The panels are lists of fixed-width things and the strip is a row of
        // knobs, so the sheet is the only part of the window worth growing.
        auto canvasBounds = [] (int width, int height)
        {
            // Read from the theme rather than restated. These were four literals
            // copied out of the layout code, which is a test that stops testing
            // the moment the design moves: every one of them went stale in the
            // Figma pass and the failure said "no child has these bounds" rather
            // than "the palette is a different width now".
            //
            // What is actually under test is the *rule* -- the four fixed edges
            // keep their sizes and the sheet takes the rest -- so the numbers
            // should come from wherever the rule reads them.
            constexpr int palette = SchematicUI::Theme::paletteWidth;
            constexpr int inspector = SchematicUI::Theme::inspectorWidth;
            constexpr int toolbar = SchematicUI::Theme::toolbarHeight;
            constexpr int strip = SchematicUI::ControlStrip::preferredHeight;

            return juce::Rectangle<int> { palette, toolbar,
                                          width - palette - inspector,
                                          height - toolbar - strip };
        };

        auto hasChildAt = [editor] (juce::Rectangle<int> bounds)
        {
            for (auto* child : editor->getChildren())
                if (child->getBounds() == bounds)
                    return true;

            return false;
        };

        for (auto size : { juce::Point<int> { 1000, 700 }, juce::Point<int> { 1400, 900 } })
        {
            editor->setSize (size.x, size.y);
            INFO ("at " << size.x << "x" << size.y);
            CHECK (hasChildAt (canvasBounds (size.x, size.y)));
        }

        // And the size survives the editor being thrown away and remade, which
        // is what happens every time the window is closed and reopened.
        CHECK (plugin.editorWidth.load() == 1400);
        CHECK (plugin.editorHeight.load() == 900);
    });
}

//==============================================================================
// Every model offered in the palette has to reach the engine. The builder maps
// a model index to a device model with a `default:` fallback, so an index the
// builder doesn't handle doesn't fail -- it silently behaves as index 0. That is
// how the measured 12AX7s and the KT66/KT77 sat implemented but unreachable:
// the dropdown was never extended to offer them.
//==============================================================================

namespace
{
    /** Builds a minimal common-cathode stage around one valve and returns a
        settled output sample, which differs whenever the device model does.

        Laid out by hand rather than by convenience: a wire here is routed as an
        L, and a leg that happens to pass over another part's pin joins that net
        silently. An earlier version of this had the screen's wire crossing the
        ground pin, which grounded the supply and made every pentode model
        produce the same answer -- the exact failure this test exists to catch,
        arriving as a false pass. Where a pin can simply be placed on top of
        another, it is: coincident pins connect with no wire at all. */
    float probeValveModel (SchematicModel::ElementType type, int modelIndex)
    {
        using namespace SchematicModel;

        Schematic schematic;

        // A triode's pins are plate/grid/cathode; a pentode's are
        // plate/screen/grid/cathode, so the same stage needs different indices.
        const bool isPentode = type == ElementType::Pentode;
        const int gridPin = isPentode ? 2 : 1;
        const int cathodePin = isPentode ? 3 : 2;

        const auto valve = schematic.addElement (type, 20, 0);
        schematic.findElement (valve)->modelIndex = modelIndex;

        auto pin = [&schematic] (int id, int index) { return schematic.findElement (id)->getPinPosition (index); };

        const auto plate = pin (valve, 0);
        const auto grid = pin (valve, gridPin);
        const auto cathode = pin (valve, cathodePin);

        // Plate load and cathode resistor sit directly in line with the pins
        // they serve, so those three joins need no wire.
        const auto plateLoad = schematic.addElement (ElementType::Resistor, plate.x, plate.y - 2);
        const auto cathodeR = schematic.addElement (ElementType::Resistor, cathode.x, cathode.y + 2);
        const auto gridLeak = schematic.addElement (ElementType::Resistor, 8, grid.y + 2);
        schematic.addElement (ElementType::Output, plate.x + 2, plate.y);
        schematic.addElement (ElementType::Input, 6, grid.y);

        schematic.findElement (plateLoad)->value = 100000.0;
        schematic.findElement (cathodeR)->value = 1500.0;
        schematic.findElement (gridLeak)->value = 1.0e6;

        // The supply lies flat well above the stage, so its two terminals are
        // side by side and no wire to one can run over the other.
        const auto supply = schematic.addElement (ElementType::VoltageSource, 0, -20);
        schematic.findElement (supply)->value = 300.0;
        schematic.findElement (supply)->orientation = 1;

        const auto rail = pin (supply, 0);

        // Two ground symbols, one at each end. They are one node because a
        // terminal names its net, which is what saves a wire down the sheet.
        schematic.addElement (ElementType::Ground, pin (supply, 1).x, pin (supply, 1).y + 2);
        const auto bottomGround = schematic.addElement (ElementType::Ground, -10, grid.y + 6);
        const auto gnd = pin (bottomGround, 0);

        schematic.addWire (pin (plateLoad, 0), rail);
        schematic.addWire (grid, pin (gridLeak, 0));
        schematic.addWire (pin (gridLeak, 1), gnd);
        schematic.addWire (pin (cathodeR, 1), gnd);

        // The screen goes to the rail, which is the whole point of having one.
        if (isPentode)
            schematic.addWire (pin (valve, 1), rail);

        auto result = buildCircuits (schematic, 48000.0, 1);
        INFO (result.error);
        REQUIRE (result.isValid());

        auto& circuit = *result.circuits[0];
        REQUIRE (circuit.foundOperatingPoint());

        float out = 0.0f;

        for (int i = 0; i < 2000; ++i)
            out = circuit.process (0.05f);

        REQUIRE (std::isfinite (out));
        REQUIRE (std::abs (out) > 1.0e-6f);   // a dead stage would compare equal for every model
        return out;
    }
}

namespace
{
    /** A shunt clipper carrying one diode model, probed in both directions.

        Both polarities matter and neither alone is enough. Forward, every
        silicon part in the list behaves the same, so a Zener would look like a
        1N4148; reverse, the plain diodes all behave the same and only the
        Zeners differ. Summing the two is what tells every entry apart.

        Laid out by coincident pins wherever possible, for the reason the valve
        probe above records: an L-routed wire has to get where it is going, and
        it can pass through something else on the way. */
    float probeDiodeModel (int modelIndex, float drive)
    {
        using namespace SchematicModel;

        Schematic schematic;

        const auto diode = schematic.addElement (ElementType::Diode, 0, 4);
        schematic.findElement (diode)->modelIndex = modelIndex;

        auto pin = [&schematic] (int id, int index)
        { return schematic.findElement (id)->getPinPosition (index); };

        const auto anode = pin (diode, 0);
        const auto cathode = pin (diode, 1);

        // Series resistor straight above the anode, ground straight below the
        // cathode: both joins are pin-on-pin.
        const auto series = schematic.addElement (ElementType::Resistor, anode.x, anode.y - 2);
        schematic.findElement (series)->value = 10000.0;

        // Terminals placed by their own pin offsets rather than by eye. Those
        // offsets differ per terminal -- ground's pin is two *above* its centre,
        // the input's two to the right, the output's two to the left -- so
        // guessing puts a pin on empty sheet, where it silently drives nothing
        // and every model reads back the same dead zero.
        const auto place = [&] (ElementType type, juce::Point<int> onto)
        {
            const auto id = schematic.addElement (type, 0, 0);
            const auto offset = schematic.findElement (id)->getPinPosition (0);
            schematic.findElement (id)->x = onto.x - offset.x;
            schematic.findElement (id)->y = onto.y - offset.y;
            return id;
        };

        place (ElementType::Ground, cathode);
        place (ElementType::Input, pin (series, 0));
        place (ElementType::Output, anode);

        auto result = buildCircuits (schematic, 48000.0, 1);
        INFO (result.error);
        REQUIRE (result.isValid());

        auto& circuit = *result.circuits[0];
        float out = 0.0f;

        for (int i = 0; i < 500; ++i)
            out = circuit.process (drive);

        REQUIRE (std::isfinite (out));
        return out;
    }
}

TEST_CASE ("Every diode model in the palette reaches the engine", "[plugin][schematic]")
{
    using namespace SchematicModel;

    const auto choices = getModelChoices (ElementType::Diode);
    REQUIRE (choices.size() > 1);

    // Driven well past every clamp in the list, both ways: +/-15 V clears the
    // 9 V Zener's knee, and the forward pass separates germanium from silicon
    // from the LEDs.
    const auto signature = [] (int index)
    { return probeDiodeModel (index, 15.0f) + probeDiodeModel (index, -15.0f); };

    const float first = signature (0);

    for (int i = 1; i < choices.size(); ++i)
    {
        // The 1N914 is the one entry that *should* match the 1N4148: it is the
        // same die under a second part number, listed twice because that is the
        // number half the schematics in the world print. Named rather than
        // skipped by index, so appending anything after it cannot quietly
        // inherit the exemption.
        if (choices[i] == "1N914")
        {
            INFO ("the 1N914 is listed as an alias of model 0 and must match it");
            CHECK (std::abs (signature (i) - first) < 1.0e-6f);
            continue;
        }

        // Identical to model 0 means the builder fell through to `default:` --
        // the palette is offering something that isn't wired up -- or that two
        // entries name the same part, which is a dropdown row that does nothing.
        INFO ("model " << i << " (" << choices[i] << ") behaves as model 0");
        CHECK (std::abs (signature (i) - first) > 1.0e-6f);
    }
}

TEST_CASE ("Model ids are unique and well formed", "[plugin][schematic]")
{
    using namespace SchematicModel;

    // The id is the file format now, and it is the one part of a model record
    // that nothing at load can check. An unknown id is caught and reported; a
    // *duplicated* one is not, because the lookup simply finds the first and
    // every sheet naming the second silently gets the wrong model. That is the
    // exact failure naming models was meant to remove, so it is checked here.
    for (int i = 0; i < numElementTypes; ++i)
    {
        const auto type = static_cast<ElementType> (i);
        const auto choices = getModelChoices (type);

        if (choices.isEmpty())
            continue;

        juce::StringArray seen;

        for (int model = 0; model < choices.size(); ++model)
        {
            const auto id = getModelId (type, model);
            INFO (getElementInfo (type).name << " model " << model << " (" << choices[model]
                  << ") has id \"" << id << "\"");

            CHECK (id.isNotEmpty());
            CHECK (! id.containsChar ('|'));   // would split the record
            CHECK (! id.containsChar (';'));   // would split the list
            CHECK (! id.containsChar (' '));   // ids go in files, keep them terse
            CHECK (id == id.toLowerCase());    // one spelling, so a typo is a miss

            // Every model Celine ships lives under a reserved prefix, so a user
            // model can never shadow one of ours and one we add later can never
            // shadow theirs. This is the whole reason to namespace before any
            // sheets exist carrying these ids.
            CHECK (id.startsWith ("celine:"));
            CHECK (getModelAuthor (type, model) == juce::String::fromUTF8 ("Céline"));

            CHECK (! seen.contains (id));
            seen.add (id);

            // And the round trip that everything else depends on.
            CHECK (findModelById (type, id) == model);
        }

        // A name that isn't there is -1, not 0. Callers branch on this.
        CHECK (findModelById (type, "definitely-not-a-model") == -1);
        CHECK (findModelById (type, "") == -1);
    }
}

TEST_CASE ("A saved sheet names its models rather than numbering them", "[plugin][schematic]")
{
    using namespace SchematicModel;

    Schematic sheet;
    const auto triode = sheet.addElement (ElementType::Triode, 0, 0);
    sheet.findElement (triode)->modelIndex = 6;          // 12AX7 EHX
    const auto diode = sheet.addElement (ElementType::Diode, 10, 0);
    sheet.findElement (diode)->modelIndex = 1;           // Germanium

    const auto tree = sheet.toValueTree();

    // The index is gone from the file entirely -- there is no second way to say
    // which model a part is, so there is no second path to keep in step.
    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        const auto node = tree.getChild (i);

        if (node.hasType ("ELEMENT"))
            CHECK (! node.hasProperty ("model"));
    }

    Schematic reloaded;
    reloaded.restoreFromValueTree (tree);

    REQUIRE (reloaded.getElements().size() == 2);
    CHECK (reloaded.getLoadWarnings().isEmpty());

    for (const auto& e : reloaded.getElements())
    {
        if (e.type == ElementType::Triode)
            CHECK (getModelId (e.type, e.modelIndex) == "celine:triode-12ax7-ehx");
        if (e.type == ElementType::Diode)
            CHECK (getModelId (e.type, e.modelIndex) == "celine:diode-germanium");
    }
}

TEST_CASE ("An unknown model id falls back and says which one", "[plugin][schematic]")
{
    using namespace SchematicModel;

    Schematic sheet;
    const auto triode = sheet.addElement (ElementType::Triode, 0, 0);
    sheet.findElement (triode)->modelIndex = 3;

    auto tree = sheet.toValueTree();
    tree.getChild (0).setProperty ("modelId", "triode-12ax7-from-the-future", nullptr);

    Schematic reloaded;
    reloaded.restoreFromValueTree (tree);

    REQUIRE (reloaded.getElements().size() == 1);

    // Model 0, not a guess and not a refusal: the rest of the sheet is fine.
    CHECK (reloaded.getElements()[0].modelIndex == 0);

    // And loudly. A silent fallback is the failure this scheme exists to
    // remove -- the sheet still builds and still makes sound, with the wrong
    // valve in it.
    const auto warnings = reloaded.getLoadWarnings();
    REQUIRE (warnings.size() == 1);
    INFO (warnings[0]);
    // Names the model that is missing and nothing else. What it fell back to is
    // visible in the inspector and on the sheet, so repeating it here would be
    // saying the same thing in the one place with the least room for it.
    CHECK (warnings[0] == "\"triode-12ax7-from-the-future\" model not found in this version.");

    // Drained, not repeated: the substitution describes the load, and after the
    // first report it has either been corrected or accepted.
    CHECK (reloaded.takeLoadWarnings().size() == 1);
    CHECK (reloaded.getLoadWarnings().isEmpty());
}

TEST_CASE ("An unresolved model id survives a save", "[plugin][schematic]")
{
    using namespace SchematicModel;

    // The reason this matters is that the save is often not a decision anybody
    // makes: a host writes its session state on its own, so a sheet living in a
    // DAW project would lose the reference simply by being opened and closed on
    // a machine without the model. Losing it makes the model unrecoverable even
    // once you obtain it, which turns a repairable situation into a permanent
    // one, silently.
    Schematic sheet;
    sheet.addElement (ElementType::Triode, 0, 0);

    auto tree = sheet.toValueTree();
    tree.getChild (0).setProperty ("modelId", "lucas:mullard-clone:7f3a", nullptr);

    Schematic opened;
    opened.restoreFromValueTree (tree);

    REQUIRE (opened.getElements().size() == 1);
    CHECK (opened.getElements()[0].modelIndex == 0);                            // builds as something real
    CHECK (opened.getElements()[0].unresolvedModelId == "lucas:mullard-clone:7f3a");

    // Saved again -- as a host would, unprompted -- the id is still what the
    // sheet asked for, not what it was given.
    const auto resaved = opened.toValueTree();
    CHECK (resaved.getChild (0).getProperty ("modelId").toString() == "lucas:mullard-clone:7f3a");

    // So a second trip round is lossless, and obtaining the model later repairs
    // the sheet rather than finding nothing left to repair.
    Schematic again;
    again.restoreFromValueTree (resaved);
    CHECK (again.getElements()[0].unresolvedModelId == "lucas:mullard-clone:7f3a");
    CHECK (again.getLoadWarnings().size() == 1);

    // Choosing a model settles it, which is what the inspector does on change.
    auto* element = again.findElement (again.getElements()[0].id);
    REQUIRE (element != nullptr);
    element->modelIndex = 2;
    element->unresolvedModelId.clear();

    const auto settled = again.toValueTree();
    CHECK (settled.getChild (0).getProperty ("modelId").toString()
             == getModelId (ElementType::Triode, 2));
}

TEST_CASE ("Cloning a part carries an unresolved model with it", "[plugin][schematic]")
{
    using namespace SchematicModel;

    // copyPropertiesTo is the single place that says what "the same part" means
    // -- middle-click cloning goes through it -- so a member added to Element
    // has to be listed there or it is silently dropped.
    Element source;
    source.type = ElementType::Triode;
    source.modelIndex = 0;
    source.unresolvedModelId = "lucas:mullard-clone:7f3a";

    Element clone;
    source.copyPropertiesTo (clone);

    CHECK (clone.unresolvedModelId == "lucas:mullard-clone:7f3a");
}

TEST_CASE ("A sheet from before model ids says so once", "[plugin][schematic]")
{
    using namespace SchematicModel;

    // Backwards compatibility was dropped deliberately, so such a sheet loads
    // with every model reset. That is a decision, not an accident -- but it has
    // to be *visible*, because the alternative is a sheet that opens, builds and
    // plays with the wrong parts throughout, which is far harder to notice than
    // a file that refuses.
    Schematic sheet;
    sheet.addElement (ElementType::Triode, 0, 0);
    sheet.addElement (ElementType::Diode, 10, 0);
    sheet.addElement (ElementType::Resistor, 20, 0);   // no models, so no id, so no complaint

    auto tree = sheet.toValueTree();

    for (int i = 0; i < tree.getNumChildren(); ++i)
        tree.getChild (i).removeProperty ("modelId", nullptr);

    Schematic reloaded;
    reloaded.restoreFromValueTree (tree);

    const auto warnings = reloaded.getLoadWarnings();
    REQUIRE (warnings.size() == 1);       // one line, not one per part
    INFO (warnings[0]);
    CHECK (warnings[0].contains ("2 parts"));   // the resistor is not counted
}

TEST_CASE ("Grouping a model list never renumbers it", "[plugin][schematic]")
{
    using namespace SchematicModel;

    // Saved sheets name their models now, so reordering this table no longer
    // breaks files -- but the dropdown's headings are still a view over the list
    // rather than an ordering of it, because the parts of one polarity are not
    // contiguous and cannot be made so while one model is one row.
    //
    // The head of each list is still worth pinning: SchematicBuilder's switch
    // ends in `default:`, so model 0 is what an unrecognised index falls to and
    // what an unknown id is deliberately substituted with. It is the one row in
    // each list that means something beyond itself.
    //
    // Pinned by the two facts that break together: the head of each list, and
    // that every model belongs to exactly one heading.
    struct Expected { ElementType type; int index; const char* name; };

    const Expected heads[] = {
        { ElementType::Diode,       0, "1N4148" },
        { ElementType::Diode,       1, "Germanium" },
        { ElementType::Transistor,  0, "2N3904" },
        { ElementType::Transistor,  1, "2N3906" },
        { ElementType::Jfet,        0, "J201" },
        { ElementType::Triode,      0, "12AX7 JJ" },
        { ElementType::Pentode,     0, "EL34 JJ" },
        { ElementType::VacuumDiode, 0, "GZ34 JJ" },
    };

    for (const auto& e : heads)
    {
        const auto choices = getModelChoices (e.type);
        REQUIRE (e.index < choices.size());
        INFO (getElementInfo (e.type).name << " model " << e.index);
        CHECK (choices[e.index] == juce::String (e.name));
    }

    for (int i = 0; i < numElementTypes; ++i)
    {
        const auto type = static_cast<ElementType> (i);
        const auto groups = getModelGroups (type);

        if (groups.isEmpty())
            continue;

        // Every model in a grouped list needs a heading, or it would be dropped
        // from the dropdown entirely -- the menu is built group by group.
        for (int model = 0; model < getModelChoices (type).size(); ++model)
        {
            INFO (getElementInfo (type).name << " model " << model
                  << " (" << getModelChoices (type)[model] << ") has no group");
            CHECK (groups.contains (getModelGroup (type, model)));
        }
    }
}

TEST_CASE ("Every valve model in the palette reaches the engine", "[plugin][schematic]")
{
    using namespace SchematicModel;

    for (const auto type : { ElementType::Triode, ElementType::Pentode })
    {
        const auto choices = getModelChoices (type);
        INFO (getElementInfo (type).name << " offers " << choices.size() << " models");
        REQUIRE (choices.size() > 1);

        const float first = probeValveModel (type, 0);

        for (int i = 1; i < choices.size(); ++i)
        {
            // Identical to model 0 means the builder fell through to `default:`,
            // i.e. the palette is offering something that isn't wired up.
            INFO ("model " << i << " (" << choices[i] << ") behaves as model 0");
            CHECK (std::abs (probeValveModel (type, i) - first) > 1.0e-6f);
        }
    }
}

namespace
{
    /** A common-emitter transistor stage driven through 100k, so the
        Miller-multiplied base-collector capacitance lands its pole inside the
        audio band. The same layout discipline as the valve probe: coincident
        pins wherever they reach, and wires routed clear of other parts' pins
        (a wire's corner goes across first, then down). */
    float probeTransistorStage (const SchematicModel::BuildOptions& options, double frequency)
    {
        using namespace SchematicModel;

        Schematic schematic;

        const auto transistor = schematic.addElement (ElementType::Transistor, 30, 0);
        schematic.findElement (transistor)->modelIndex = 0; // 2N3904

        auto pin = [&schematic] (int id, int index)
        { return schematic.findElement (id)->getPinPosition (index); };

        const auto base = pin (transistor, 0);
        const auto collector = pin (transistor, 1);
        const auto emitter = pin (transistor, 2);

        // Emitter straight onto a ground pin; the collector load and the
        // output terminal both sit on the collector pin.
        schematic.addElement (ElementType::Ground, emitter.x, emitter.y + 2);
        const auto collectorLoad = schematic.addElement (ElementType::Resistor, collector.x, collector.y - 2);
        schematic.addElement (ElementType::Output, collector.x + 2, collector.y);

        // The supply lies flat at the top, a second ground symbol on its
        // negative pin -- one node with the first, since a terminal names its net.
        const auto supply = schematic.addElement (ElementType::VoltageSource, 24, -10);
        schematic.findElement (supply)->value = 9.0;
        schematic.findElement (supply)->orientation = 1;
        schematic.addElement (ElementType::Ground, pin (supply, 1).x, pin (supply, 1).y + 2);

        const auto bias = schematic.addElement (ElementType::Resistor, 26, -4);
        const auto input = schematic.addElement (ElementType::Input, 8, 0);
        const auto coupling = schematic.addElement (ElementType::Capacitor, 13, 0);
        schematic.findElement (coupling)->orientation = 1;
        const auto source = schematic.addElement (ElementType::Resistor, 18, 0);
        schematic.findElement (source)->orientation = 1;

        schematic.findElement (collectorLoad)->value = 22000.0;
        schematic.findElement (bias)->value = 27.0e6;
        schematic.findElement (coupling)->value = 100.0e-6;
        schematic.findElement (source)->value = 100000.0;

        // Rotated parts put pin 0 on the right and pin 1 on the left, so the
        // short runs are pin-1-to-pin-1 and pin-0-to-pin-0. A wire straight
        // from the input to the source resistor would pass over the
        // capacitor's far pin and short it out -- the failure the valve
        // probe's layout note is about.
        schematic.addWire (pin (collectorLoad, 0), pin (supply, 0));
        schematic.addWire (pin (bias, 0), pin (supply, 0));
        schematic.addWire (pin (bias, 1), base);
        schematic.addWire (pin (input, 0), pin (coupling, 1));
        schematic.addWire (pin (coupling, 0), pin (source, 1));
        schematic.addWire (pin (source, 0), base);

        auto result = buildCircuits (schematic, 48000.0, 1, options);
        INFO (result.error);
        REQUIRE (result.isValid());

        auto& circuit = *result.circuits[0];
        REQUIRE (circuit.foundOperatingPoint());

        // Peak over the second half, so the answer is the settled amplitude.
        float peak = 0.0f;
        double phase = 0.0;

        for (int i = 0; i < 9600; ++i)
        {
            const float sample = circuit.process (0.001f * (float) std::sin (phase));
            phase += 2.0 * juce::MathConstants<double>::pi * frequency / 48000.0;

            if (i > 4800)
                peak = std::max (peak, std::abs (sample));
        }

        REQUIRE (std::isfinite (peak));
        return peak;
    }
}

TEST_CASE ("Transistor modelling options change the built circuit", "[plugin][schematic]")
{
    using namespace SchematicModel;

    // Both default to on, with the rest of BuildOptions: accurate unless the
    // CPU is wanted back.
    const BuildOptions defaults;
    CHECK (defaults.transistorJunctionCapacitance);
    CHECK (defaults.transistorEarlyEffect);

    // Each comparison sets *both* flags explicitly rather than leaning on the
    // defaults for one side. Leaning on them is how this test came to pass
    // vacuously when they flipped: "off" and "on" were the same build, and a
    // difference of exactly zero satisfied an approximate equality.
    BuildOptions bare;
    bare.transistorJunctionCapacitance = false;
    bare.transistorEarlyEffect = false;

    // Junction capacitance: at 1 kHz the pole hasn't bitten yet, at 20 kHz it
    // is well past it -- so the toggle should leave the low end alone and take
    // the top end away.
    BuildOptions caps = bare;
    caps.transistorJunctionCapacitance = true;

    const float flat1k = probeTransistorStage (bare, 1000.0);
    const float capped1k = probeTransistorStage (caps, 1000.0);
    const float flat20k = probeTransistorStage (bare, 20000.0);
    const float capped20k = probeTransistorStage (caps, 20000.0);

    INFO ("1 kHz: " << flat1k << " off, " << capped1k << " on;  20 kHz: "
          << flat20k << " off, " << capped20k << " on");

    CHECK (capped1k == Catch::Approx (flat1k).epsilon (0.05));
    CHECK (flat20k == Catch::Approx (flat1k).epsilon (0.05));  // no caps, no pole
    CHECK (capped20k < capped1k * 0.7f);                       // past the ~9 kHz pole

    // Early effect: a few percent of gain, present at 1 kHz already. The sign
    // is the topology's business (see the engine-level test), so what is pinned
    // here is that it is small, and that it is there.
    BuildOptions early = bare;
    early.transistorEarlyEffect = true;

    const float withEarly = probeTransistorStage (early, 1000.0);
    const float change = withEarly / flat1k - 1.0f;

    INFO ("Early effect gain change: " << change * 100.0f << "%");
    CHECK (std::abs (change) > 0.005f);
    CHECK (std::abs (change) < 0.10f);
}

TEST_CASE ("Interelectrode capacitance can be traded away for CPU", "[plugin][schematic]")
{
    using namespace SchematicModel;

    // A valve stage is the case the option exists for -- three capacitors per
    // valve, and in DK every capacitor is a state variable. Any example with a
    // valve in it will do, so take the first one that has one.
    Schematic schematic;
    const auto names = Examples::getNames();
    bool found = false;

    for (int i = 0; i < names.size() && ! found; ++i)
    {
        Examples::load (schematic, i);

        for (const auto& e : schematic.getElements())
            found = found || e.type == ElementType::Triode || e.type == ElementType::Pentode;
    }

    REQUIRE (found);

    auto settle = [&schematic] (bool interelectrode)
    {
        auto result = buildCircuits (schematic, 48000.0, 1, BuildOptions { interelectrode });
        REQUIRE (result.isValid());

        auto& circuit = *result.circuits[0];
        float out = 0.0f;
        double phase = 0.0;

        for (int i = 0; i < 4000; ++i)
        {
            out = circuit.process (0.2f * (float) std::sin (phase));
            phase += 2.0 * juce::MathConstants<double>::pi * 4000.0 / 48000.0;
        }

        REQUIRE (std::isfinite (out));
        return out;
    };

    // Turning it off has to actually change the circuit -- at 4 kHz, which is
    // where the rolloff it models lives.
    CHECK (std::abs (settle (true) - settle (false)) > 1.0e-6f);
}

TEST_CASE ("Build options travel with the document", "[plugin]")
{
    PluginProcessor plugin;

    // Accurate by default: nobody opts into the full simulation.
    CHECK (plugin.buildOptions.interelectrodeCapacitance);

    // An untouched sheet carries no option at all, so a file saved today still
    // matches one saved before the setting existed.
    CHECK (! plugin.createDocument().hasProperty ("interelectrodeCapacitance"));

    plugin.buildOptions.interelectrodeCapacitance = false;
    const auto preset = plugin.createDocument();
    CHECK (preset.hasProperty ("interelectrodeCapacitance"));

    // Unlike the window size, this one belongs to the circuit: it changes what
    // the sheet sounds like, so a shared .celsch has to bring it along.
    PluginProcessor loaded;
    REQUIRE (loaded.restoreDocument (preset));
    CHECK (! loaded.buildOptions.interelectrodeCapacitance);

    // And a document from before the option existed reads as accurate.
    auto old = plugin.createDocument();
    old.removeProperty ("interelectrodeCapacitance", nullptr);

    PluginProcessor upgraded;
    upgraded.buildOptions.interelectrodeCapacitance = false;
    REQUIRE (upgraded.restoreDocument (old));
    CHECK (upgraded.buildOptions.interelectrodeCapacitance);
}

TEST_CASE ("The transistor options travel with the document too", "[plugin]")
{
    // Same rule as the valve capacitance above -- accurate by default, written
    // only when switched off, absent means on. Worth its own test because the
    // pair briefly shipped the other way up, and both halves of the inversion
    // fail quietly: a wrong default writes a property nobody expects, and a
    // wrong fallback silently opens old sheets on the older, simpler model.
    PluginProcessor plugin;

    CHECK (plugin.buildOptions.transistorJunctionCapacitance);
    CHECK (plugin.buildOptions.transistorEarlyEffect);

    CHECK (! plugin.createDocument().hasProperty ("transistorJunctionCapacitance"));
    CHECK (! plugin.createDocument().hasProperty ("transistorEarlyEffect"));

    plugin.buildOptions.transistorJunctionCapacitance = false;
    plugin.buildOptions.transistorEarlyEffect = false;
    const auto preset = plugin.createDocument();
    CHECK (preset.hasProperty ("transistorJunctionCapacitance"));
    CHECK (preset.hasProperty ("transistorEarlyEffect"));

    PluginProcessor loaded;
    REQUIRE (loaded.restoreDocument (preset));
    CHECK (! loaded.buildOptions.transistorJunctionCapacitance);
    CHECK (! loaded.buildOptions.transistorEarlyEffect);

    // A sheet from before these existed carries neither property, and must come
    // back on the full model rather than frozen at the simpler one.
    auto old = preset.createCopy();
    old.removeProperty ("transistorJunctionCapacitance", nullptr);
    old.removeProperty ("transistorEarlyEffect", nullptr);

    PluginProcessor upgraded;
    upgraded.buildOptions.transistorJunctionCapacitance = false;
    upgraded.buildOptions.transistorEarlyEffect = false;
    REQUIRE (upgraded.restoreDocument (old));
    CHECK (upgraded.buildOptions.transistorJunctionCapacitance);
    CHECK (upgraded.buildOptions.transistorEarlyEffect);
}

TEST_CASE ("A text note annotates the sheet without joining the circuit", "[schematic]")
{
    using namespace SchematicModel;

    Schematic schematic;
    Examples::load (schematic, 0);

    const auto before = buildCircuits (schematic, 48000.0, 1);
    REQUIRE (before.isValid());

    const auto note = schematic.addElement (ElementType::Text, 4, 12);
    schematic.findElement (note)->label = "input stage";

    // No pins means net extraction never sees it, so it cannot short anything
    // or leave a net dangling however it is placed -- here, deliberately on top
    // of the existing drawing.
    REQUIRE (schematic.findElement (note)->getPinCount() == 0);

    const auto nets = schematic.extractNets();
    CHECK (nets.danglingNets.empty());

    auto after = buildCircuits (schematic, 48000.0, 1);
    REQUIRE (after.isValid());

    // Same circuit, sample for sample.
    auto run = [] (const BuildResult& result)
    {
        auto& circuit = *result.circuits[0];
        juce::Array<float> out;
        double phase = 0.0;

        for (int i = 0; i < 500; ++i)
        {
            out.add (circuit.process (0.3f * (float) std::sin (phase)));
            phase += 2.0 * juce::MathConstants<double>::pi * 500.0 / 48000.0;
        }

        return out;
    };

    const auto plain = run (before);
    const auto annotated = run (after);

    REQUIRE (plain.size() == annotated.size());

    for (int i = 0; i < plain.size(); ++i)
        REQUIRE (std::abs (plain[i] - annotated[i]) == 0.0f);

    // It still has to be selectable, which for a pinless element means the hit
    // box has to come from the text rather than from pins it doesn't have.
    const auto bounds = schematic.getElementBounds (*schematic.findElement (note));
    CHECK (bounds.getWidth() > 3);
    CHECK (schematic.findElementAt ({ 4, 12 }) == note);

    // And it survives a save/load round trip, text and all.
    Schematic reloaded;
    reloaded.restoreFromValueTree (schematic.toValueTree());

    const auto* restored = reloaded.findElement (note);
    REQUIRE (restored != nullptr);
    CHECK (restored->type == ElementType::Text);
    CHECK (restored->label == "input stage");
}

TEST_CASE ("A group box rings parts without joining or hiding them", "[schematic]")
{
    using namespace SchematicModel;

    Schematic schematic;
    Examples::load (schematic, 0);

    const auto before = buildCircuits (schematic, 48000.0, 1);
    REQUIRE (before.isValid());

    // Deliberately drawn around the existing circuit, which is what a group box
    // is for and also the arrangement most likely to break something.
    juce::Rectangle<int> drawing;
    bool first = true;

    for (const auto& e : schematic.getElements())
    {
        const auto bounds = schematic.getElementBounds (e);
        drawing = first ? bounds : drawing.getUnion (bounds);
        first = false;
    }

    REQUIRE (! drawing.isEmpty());

    const auto box = schematic.addElement (ElementType::Rectangle,
                                           drawing.getCentreX(), drawing.getCentreY());
    auto* group = schematic.findElement (box);
    group->label = "input stage";
    group->width = drawing.getWidth() + 4;
    group->height = drawing.getHeight() + 4;

    // No pins, so net extraction never sees it: it cannot short anything or
    // leave a net dangling however it is placed.
    REQUIRE (group->getPinCount() == 0);
    CHECK (schematic.extractNets().danglingNets.empty());

    auto after = buildCircuits (schematic, 48000.0, 1);
    REQUIRE (after.isValid());

    auto run = [] (const BuildResult& result)
    {
        auto& circuit = *result.circuits[0];
        juce::Array<float> out;
        double phase = 0.0;

        for (int i = 0; i < 500; ++i)
        {
            out.add (circuit.process (0.3f * (float) std::sin (phase)));
            phase += 2.0 * juce::MathConstants<double>::pi * 500.0 / 48000.0;
        }

        return out;
    };

    // Same circuit, sample for sample.
    const auto plain = run (before);
    const auto grouped = run (after);

    REQUIRE (plain.size() == grouped.size());

    for (int i = 0; i < plain.size(); ++i)
        REQUIRE (std::abs (plain[i] - grouped[i]) == 0.0f);

    //--------------------------------------------------------------------------
    // It is grabbed by its edge, and everything inside stays clickable.
    const auto bounds = group->getRectangleBounds();

    CHECK (schematic.findElementAt (bounds.getTopLeft()) == box);
    CHECK (schematic.findElementAt (bounds.getBottomRight()) == box);

    // The middle does not select it: a box that swallowed the clicks aimed at
    // the stage it rings would make that stage unselectable and undeletable.
    CHECK (schematic.findElementAt (bounds.getCentre()) != box);

    // Nor does any point on a part it was drawn around -- even though the box
    // was placed last, and hit testing otherwise searches newest first.
    for (const auto& e : schematic.getElements())
    {
        if (e.type == ElementType::Rectangle)
            continue;

        INFO (getElementInfo (e.type).name << " at " << e.x << "," << e.y);
        CHECK (schematic.findElementAt ({ e.x, e.y }) != box);
    }

    //--------------------------------------------------------------------------
    // Size, colour and title survive a preset round trip -- through the document
    // that .celsch files and the host's own state both use, rather than through
    // Schematic alone, since that is the path a saved preset actually takes.
    group->modelIndex = 3;

    {
        PluginProcessor plugin;
        plugin.getSchematic().restoreFromValueTree (schematic.toValueTree());

        PluginProcessor loaded;
        REQUIRE (loaded.restoreDocument (plugin.createDocument()));

        const auto* restored = loaded.getSchematic().findElement (box);
        REQUIRE (restored != nullptr);
        CHECK (restored->type == ElementType::Rectangle);
        CHECK (restored->label == "input stage");
        CHECK (restored->modelIndex == 3);
        CHECK (restored->width == group->width);
        CHECK (restored->height == group->height);
        CHECK (restored->getRectangleBounds() == bounds);
    }

    // And nothing else grows a size it would never read back.
    {
        const auto tree = schematic.toValueTree();

        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            const auto node = tree.getChild (i);

            if (! node.hasType ("ELEMENT"))
                continue;

            const bool isBox = static_cast<int> (node.getProperty ("type"))
                            == static_cast<int> (ElementType::Rectangle);

            INFO ("element type " << node.getProperty ("type").toString());
            CHECK (node.hasProperty ("width") == isBox);
            CHECK (node.hasProperty ("height") == isBox);
        }
    }

    //--------------------------------------------------------------------------
    // Dragging one corner leaves the other three where they were. This is what
    // SchematicCanvas::resizeTo does, and it relies on withCentre and getCentreX
    // being exact inverses at odd sizes too. Walked a square at a time, because
    // a drag is a run of these and any drift would accumulate over one.
    const auto anchor = bounds.getTopLeft();

    for (int step = 0; step < 25; ++step)
    {
        const auto current = group->getRectangleBounds();
        const auto target = current.getBottomRight() + juce::Point<int> (1, 1);

        group->width = juce::jmax (minRectangleSize, target.x - current.getX());
        group->height = juce::jmax (minRectangleSize, target.y - current.getY());
        group->x = current.getX() + group->width / 2;
        group->y = current.getY() + group->height / 2;

        const auto moved = group->getRectangleBounds();
        INFO ("step " << step << ": top-left is " << moved.getX() << "," << moved.getY());
        REQUIRE (moved.getTopLeft() == anchor);
        REQUIRE (moved.getBottomRight() == target);
    }

    //--------------------------------------------------------------------------
    // A size too small to grab is clamped rather than stored, so a stray drag or
    // a typed 0 can't leave a box on the sheet with no corner left to catch.
    group->width = 0;
    group->height = -5;

    const auto clamped = group->getRectangleBounds();
    CHECK (clamped.getWidth() == minRectangleSize);
    CHECK (clamped.getHeight() == minRectangleSize);
    CHECK (clamped.getCentre() == juce::Point<int> (group->x, group->y));
}

namespace
{
    /** A transformer driven from a real source impedance into an 8 ohm load,
        laid out so that most joins are coincident pins rather than wires. */
    SchematicModel::Schematic makeTransformerBench (SchematicModel::ElementType type, int modelIndex,
                                                   double primaryTurns, double secondaryTurns,
                                                   double sourceResistance)
    {
        using namespace SchematicModel;

        Schematic sheet;
        const auto xf = sheet.addElement (type, 0, 0);
        auto* transformer = sheet.findElement (xf);
        transformer->modelIndex = modelIndex;
        transformer->value = primaryTurns;
        transformer->valueB = secondaryTurns;

        auto pin = [&sheet] (int id, int index) { return sheet.findElement (id)->getPinPosition (index); };

        const auto primaryA = pin (xf, 0);
        const auto primaryB = pin (xf, 1);
        const auto secondaryA = pin (xf, 2);
        const auto secondaryB = pin (xf, type == ElementType::CenterTapTransformer ? 4 : 3);

        // Source: input -- Rs -- primary A. Laid flat, so its two pins land on
        // the input terminal and on the run into the transformer.
        const auto rs = sheet.addElement (ElementType::Resistor, primaryA.x - 4, primaryA.y);
        sheet.findElement (rs)->orientation = 1;
        sheet.findElement (rs)->value = sourceResistance;
        sheet.addElement (ElementType::Input, primaryA.x - 8, primaryA.y);
        sheet.addWire (pin (rs, 0), primaryA);

        // Primary B and secondary B to ground, each by a symbol sitting on the pin.
        sheet.addElement (ElementType::Ground, primaryB.x, primaryB.y + 2);
        sheet.addElement (ElementType::Ground, secondaryB.x, secondaryB.y + 2);

        // The 8 ohm load hangs off secondary A, and the output reads across it.
        const auto load = sheet.addElement (ElementType::Resistor, secondaryA.x, secondaryA.y + 2);
        sheet.findElement (load)->value = 8.0;
        sheet.addWire (pin (load, 1), secondaryB);
        sheet.addElement (ElementType::Output, secondaryA.x + 2, secondaryA.y);

        return sheet;
    }

    /** Output amplitude for a unit sine at `frequency`, after the circuit settles. */
    float gainAt (Circuit& circuit, double frequency)
    {
        const int settle = static_cast<int> (48000.0 / juce::jmin (frequency, 200.0)) * 8 + 4000;
        double phase = 0.0;

        for (int i = 0; i < settle; ++i)
        {
            circuit.process ((float) std::sin (phase));
            phase += 2.0 * juce::MathConstants<double>::pi * frequency / 48000.0;
        }

        float peak = 0.0f;
        const int cycle = juce::jmax (64, static_cast<int> (48000.0 / frequency) * 2);

        for (int i = 0; i < cycle; ++i)
        {
            peak = juce::jmax (peak, std::abs (circuit.process ((float) std::sin (phase))));
            phase += 2.0 * juce::MathConstants<double>::pi * frequency / 48000.0;
        }

        return peak;
    }
}

TEST_CASE ("A real transformer has a bandwidth and blocks DC", "[schematic]")
{
    using namespace SchematicModel;

    constexpr int idealModel = 0, realModel = 1;
    constexpr double ratio = 10.0;
    constexpr double sourceResistance = 800.0;   // matches the reflected primary impedance

    auto build = [] (const Schematic& sheet)
    {
        auto result = buildCircuits (sheet, 48000.0, 1);
        INFO (result.error);
        REQUIRE (result.isValid());
        return result;
    };

    SECTION ("ideal passes everything, real is a band")
    {
        auto ideal = build (makeTransformerBench (ElementType::Transformer, idealModel, ratio, 1.0, sourceResistance));
        auto real = build (makeTransformerBench (ElementType::Transformer, realModel, ratio, 1.0, sourceResistance));

        const float idealLow  = gainAt (*ideal.circuits[0], 30.0);
        const float idealMid  = gainAt (*ideal.circuits[0], 1000.0);
        const float idealHigh = gainAt (*ideal.circuits[0], 12000.0);

        const float realLow  = gainAt (*real.circuits[0], 30.0);
        const float realMid  = gainAt (*real.circuits[0], 1000.0);
        const float realHigh = gainAt (*real.circuits[0], 12000.0);

        INFO ("ideal " << idealLow << " / " << idealMid << " / " << idealHigh);
        INFO ("real  " << realLow << " / " << realMid << " / " << realHigh);

        // Ideal has no bandwidth at all: same answer everywhere.
        CHECK (std::abs (idealLow - idealMid) < idealMid * 0.02f);
        CHECK (std::abs (idealHigh - idealMid) < idealMid * 0.02f);

        // Real rolls off at both ends -- the magnetising inductance below, the
        // leakage above.
        CHECK (realLow < realMid * 0.9f);
        CHECK (realHigh < realMid * 0.9f);

        // And it still passes the midband at roughly what the ideal one does;
        // the copper costs a little, it must not cost most of it.
        CHECK (realMid > idealMid * 0.5f);
    }

    SECTION ("real blocks DC where ideal passes it")
    {
        auto ideal = build (makeTransformerBench (ElementType::Transformer, idealModel, ratio, 1.0, sourceResistance));
        auto real = build (makeTransformerBench (ElementType::Transformer, realModel, ratio, 1.0, sourceResistance));

        auto settleDC = [] (Circuit& circuit)
        {
            float out = 0.0f;

            for (int i = 0; i < 400000; ++i)
                out = circuit.process (1.0f);

            return out;
        };

        const float idealDC = settleDC (*ideal.circuits[0]);
        const float realDC = settleDC (*real.circuits[0]);

        INFO ("ideal DC " << idealDC << ", real DC " << realDC);

        // The magnetising inductance is a short at DC, so the primary voltage --
        // and with it the secondary -- collapses. An ideal transformer has
        // nothing to stop it.
        CHECK (std::abs (idealDC) > 0.001f);
        CHECK (std::abs (realDC) < std::abs (idealDC) * 0.01f);
    }

    SECTION ("1:8 is expressible, which a single ratio box could not manage")
    {
        // The bug this replaced: "1:8" was rejected outright and 0.125 came back
        // out of the box reading "125m:1".
        auto stepUp = build (makeTransformerBench (ElementType::Transformer, idealModel, 1.0, 8.0, 1.0));
        auto stepDown = build (makeTransformerBench (ElementType::Transformer, idealModel, 8.0, 1.0, 1.0));

        const float up = gainAt (*stepUp.circuits[0], 1000.0);
        const float down = gainAt (*stepDown.circuits[0], 1000.0);

        INFO ("1:8 gives " << up << ", 8:1 gives " << down);
        CHECK (up > down * 4.0f);
    }

    SECTION ("a real centre-tapped transformer builds and needs no magnetising warning")
    {
        auto ideal = build (makeTransformerBench (ElementType::CenterTapTransformer, idealModel, ratio, 1.0, sourceResistance));
        auto real = build (makeTransformerBench (ElementType::CenterTapTransformer, realModel, ratio, 1.0, sourceResistance));

        CHECK (! joinDiagnostics (real).contains ("across the primary"));
        CHECK (joinDiagnostics (ideal).contains ("across the primary"));
    }
}

TEST_CASE ("Every model record has a name and a description", "[schematic]")
{
    using namespace SchematicModel;

    // The model list is one string of `Name|What it is` records separated by
    // semicolons, so a semicolon inside a description splits that model in two
    // and the tail arrives in the dropdown as a nameless entry. A transformer
    // shipped with exactly that: "...depends on what drives it; the treble end
    // does not" became a third option reading " the treble end does not".
    for (int i = 0; i < numElementTypes; ++i)
    {
        const auto type = static_cast<ElementType> (i);
        const auto& info = getElementInfo (type);
        const auto choices = getModelChoices (type);

        if (choices.isEmpty())
            continue;

        INFO (info.name << " offers " << choices.size() << " models");

        for (int model = 0; model < choices.size(); ++model)
        {
            const auto name = choices[model];
            const auto description = getModelDescription (type, model);

            INFO ("model " << model << " is \"" << name << "\" / \"" << description << "\"");

            // A record with no '|' puts the whole thing in the name, which is
            // the signature of an accidental split.
            CHECK (description.isNotEmpty());

            // Names go under the part on the drawing, so a runaway one is also
            // a sentence that got parsed as a name.
            CHECK (name.isNotEmpty());
            CHECK (name.length() <= 24);

            // Nothing should arrive pre-trimmed with stray punctuation either.
            CHECK (name == name.trim());
        }
    }
}

TEST_CASE ("Every knob in the pool reaches a drawn control", "[plugin]")
{
    using namespace SchematicModel;

    // One pot per slot, plus two more than the pool holds so the ceiling gets
    // exercised in the same pass.
    const int drawn = PluginProcessor::maxLiveControls + 2;

    PluginProcessor plugin;
    plugin.prepareToPlay (48000.0, 512);

    auto& sheet = plugin.getSchematic();
    sheet.clear();

    // Above the pots' line, so the wire's L-corner misses the strap corner at
    // the first pot's centre -- coincident points connect, corner or not.
    const auto input = sheet.addElement (ElementType::Input, -4, -6);
    std::vector<int> pots;
    int x = 0;

    for (int i = 0; i < drawn; ++i)
    {
        // Pot pins are (top, wiper, bottom). Wiper strapped to the bottom makes
        // each a variable resistor, chained into a series string so every one is
        // genuinely in the signal path.
        const auto pot = sheet.addElement (ElementType::Potentiometer, x, 0);
        auto* p = sheet.findElement (pot);
        p->value = 100000.0;
        p->label = "Knob" + juce::String (i + 1);

        sheet.addWire (p->getPinPosition (1), p->getPinPosition (2));

        if (! pots.empty())
            sheet.addWire (sheet.findElement (pots.back())->getPinPosition (2), p->getPinPosition (0));

        pots.push_back (pot);
        x += 8;
    }

    const auto output = sheet.addElement (ElementType::Output, x, 0);
    const auto load = sheet.addElement (ElementType::Resistor, x, 6);
    sheet.findElement (load)->value = 1.0e6;
    const auto ground = sheet.addElement (ElementType::Ground, x, 12);

    // The output gets its own node with a load on it. Wiring it straight to
    // ground is silence, and the build now says so.
    const auto tail = sheet.findElement (pots.back())->getPinPosition (2);
    sheet.addWire (sheet.findElement (input)->getPinPosition (0),
                   sheet.findElement (pots.front())->getPinPosition (0));
    sheet.addWire (tail, sheet.findElement (output)->getPinPosition (0));
    sheet.addWire (tail, sheet.findElement (load)->getPinPosition (0));
    sheet.addWire (sheet.findElement (load)->getPinPosition (1),
                   sheet.findElement (ground)->getPinPosition (0));

    const auto result = plugin.rebuild();
    INFO (result.error);
    REQUIRE (result.isValid());

    const auto controls = plugin.getLiveControls();
    REQUIRE (static_cast<int> (controls.size()) == drawn);

    // Every slot in the pool has a parameter, and the ids are distinct.
    juce::StringArray ids;

    for (int i = 0; i < PluginProcessor::maxLiveControls; ++i)
    {
        const auto id = PluginProcessor::getControlParameterId (i);
        INFO ("slot " << i << " is " << id);
        REQUIRE (plugin.apvts.getParameter (id) != nullptr);
        ids.addIfNotAlreadyThere (id);
    }

    CHECK (ids.size() == PluginProcessor::maxLiveControls);

    // Drawing past the ceiling is reported rather than silently ignored, and it
    // names what went -- a knob that just never appears reads as a bug.
    const auto warnings = joinDiagnostics (result);
    INFO (warnings);
    CHECK (warnings.contains ("no knob"));
    CHECK (warnings.contains (controls[static_cast<size_t> (PluginProcessor::maxLiveControls)].name));
}

TEST_CASE ("A freshly placed part has no value and refuses to build", "[schematic]")
{
    using namespace SchematicModel;

    // Every part that carries a number starts at zero rather than at a
    // plausible-looking default, because a default that already looks like an
    // answer is one you forget to change.
    Schematic sheet;

    for (int i = 0; i < numElementTypes; ++i)
    {
        const auto type = static_cast<ElementType> (i);
        const auto& info = getElementInfo (type);

        const auto id = sheet.addElement (type, i * 8, 0);
        const auto* placed = sheet.findElement (id);
        REQUIRE (placed != nullptr);

        INFO (info.name << " starts at " << placed->value);

        if (placed->hasNumericValue())
            CHECK (placed->value == 0.0);

        if (placed->hasSecondValue())
            CHECK (placed->valueB == 0.0);
    }

    // And an unfilled part stops the build by name -- it has to be an error
    // rather than a warning, since a zero-ohm resistor is a short.
    Schematic stage;
    const auto input = stage.addElement (ElementType::Input, -4, 0);
    const auto r = stage.addElement (ElementType::Resistor, 0, 0);
    stage.findElement (r)->label = "R99";
    const auto output = stage.addElement (ElementType::Output, 4, 0);
    const auto load = stage.addElement (ElementType::Resistor, 0, 6);
    stage.findElement (load)->value = 1.0e6;
    const auto ground = stage.addElement (ElementType::Ground, 0, 12);

    stage.addWire (stage.findElement (input)->getPinPosition (0),
                   stage.findElement (r)->getPinPosition (0));
    stage.addWire (stage.findElement (r)->getPinPosition (1),
                   stage.findElement (output)->getPinPosition (0));
    stage.addWire (stage.findElement (r)->getPinPosition (1),
                   stage.findElement (load)->getPinPosition (0));
    stage.addWire (stage.findElement (load)->getPinPosition (1),
                   stage.findElement (ground)->getPinPosition (0));

    const auto refused = buildCircuits (stage, 48000.0, 1);
    INFO (refused.error);
    CHECK (! refused.isValid());
    CHECK (refused.error.contains ("no value"));

    // The headline says how many; the list says which, and where. That split is
    // the point of the console -- six problems in one sentence is a paragraph.
    INFO (joinDiagnostics (refused));
    CHECK (joinDiagnostics (refused).contains ("R99"));

    const auto named = std::find_if (refused.diagnostics.begin(), refused.diagnostics.end(),
                                     [] (const Diagnostic& d) { return d.subject == "R99"; });
    REQUIRE (named != refused.diagnostics.end());
    CHECK (named->isError());
    CHECK (named->hasPosition);
    CHECK (named->elementId == r);

    // Give it one and it builds.
    stage.findElement (r)->value = 100000.0;
    const auto accepted = buildCircuits (stage, 48000.0, 1);
    INFO (accepted.error);
    CHECK (accepted.isValid());
}

TEST_CASE ("Switches sharing a label become one ganged control", "[schematic]")
{
    using namespace SchematicModel;

    // Three switches: two called "CHANNEL", one on its own. A real amp's channel
    // switch moves several contacts at once, and keeping them in step by hand
    // while playing is not a thing you can do.
    Schematic sheet;
    const auto input = sheet.addElement (ElementType::Input, -8, 0);
    const auto output = sheet.addElement (ElementType::Output, 40, 0);
    const auto ground = sheet.addElement (ElementType::Ground, 16, 10);

    auto addSwitch = [&sheet] (const char* label, int x)
    {
        const auto id = sheet.addElement (ElementType::Switch, x, 0);
        sheet.findElement (id)->label = label;
        return id;
    };

    const auto a = addSwitch ("CHANNEL", 0);
    const auto b = addSwitch ("CHANNEL", 8);
    const auto lone = addSwitch ("BRIGHT", 16);

    // Wire them into a chain so they are all in the circuit.
    auto pin = [&sheet] (int id, int p) { return sheet.findElement (id)->getPinPosition (p); };
    sheet.addWire (pin (input, 0), pin (a, 0));
    sheet.addWire (pin (a, 1), pin (b, 0));
    sheet.addWire (pin (b, 1), pin (lone, 0));
    sheet.addWire (pin (lone, 1), pin (output, 0));

    const auto r = sheet.addElement (ElementType::Resistor, 40, 6);
    sheet.findElement (r)->value = 100000.0;
    sheet.addWire (pin (lone, 1), pin (r, 0));
    sheet.addWire (pin (r, 1), pin (ground, 0));

    const auto result = buildCircuits (sheet, 48000.0, 1);
    INFO (result.error);
    REQUIRE (result.isValid());

    // Two controls, not three: the pair collapsed into one.
    REQUIRE (result.controls.size() == 2);

    const auto channel = std::find_if (result.controls.begin(), result.controls.end(),
                                       [] (const LiveControl& c) { return c.name == "CHANNEL"; });
    REQUIRE (channel != result.controls.end());
    CHECK (channel->toggles.size() == 2);

    const auto bright = std::find_if (result.controls.begin(), result.controls.end(),
                                      [] (const LiveControl& c) { return c.name == "BRIGHT"; });
    REQUIRE (bright != result.controls.end());
    CHECK (bright->toggles.size() == 1);

    juce::ignoreUnused (a, b, lone);
}

TEST_CASE ("An SPDT sends the signal to one throw or the other", "[schematic]")
{
    using namespace SchematicModel;

    // Common in, two throws each through a different resistor to a shared load.
    // Which throw is made decides the divider ratio, so the gain says which one
    // it is -- counting contacts would not.
    //
    // Laid out so that every join is two pins on the same grid point, which
    // connect with no wire. A diagonal addWire() becomes an L, and an L run
    // across a part's other pin shorts it silently -- which is exactly what the
    // first draft of this test did.
    Schematic sheet;

    auto place = [&sheet] (ElementType type, int x, int y, double value = 0.0)
    {
        const auto id = sheet.addElement (type, x, y);
        sheet.findElement (id)->value = value;
        return id;
    };

    //  ground ---- load 10k ---- out ---- via 100k ---- throw B
    //                             |
    //                             +----- via 1k ------ throw A
    //                                     (common) ---- in
    const auto sw = place (ElementType::Spdt, 0, 0);
    place (ElementType::Input, -2, 2);          // pin lands on the common
    const auto viaA = place (ElementType::Resistor, -1, -4, 1000.0);
    const auto viaB = place (ElementType::Resistor, 1, -4, 100000.0);
    place (ElementType::Output, 1, -6);         // pin lands on viaA's top
    const auto load = place (ElementType::Resistor, 1, -8, 10000.0);
    const auto ground = place (ElementType::Ground, 1, -12);

    // The two joins that aren't coincident pins, both single straight segments:
    // across the tops of the throws, and down to ground.
    auto pin = [&sheet] (int id, int p) { return sheet.findElement (id)->getPinPosition (p); };
    sheet.addWire (pin (viaA, 0), pin (viaB, 0));
    sheet.addWire (pin (load, 0), pin (ground, 0));

    const auto result = buildCircuits (sheet, 48000.0, 1);
    INFO (result.error);
    REQUIRE (result.isValid());
    REQUIRE (result.controls.size() == 1);

    // A dangling pin here would mean the divider isn't loaded and both throws
    // read a gain of exactly one -- a layout slip that looks like a code bug.
    for (const auto& diagnostic : result.diagnostics)
        INFO (diagnostic.text);

    CHECK (result.diagnostics.empty());

    auto& circuit = *result.circuits[0];
    const auto& control = result.controls[0];

    // Purely resistive, so a steady input settles at once.
    auto gainWith = [&circuit, &control] (float position)
    {
        control.apply (circuit, position);
        float out = 0.0f;

        for (int i = 0; i < 64; ++i)
            out = circuit.process (1.0f);

        return out;
    };

    // Throw A is the 1k: 10k / 11k. Throw B is the 100k: 10k / 110k.
    const float throwA = gainWith (1.0f);
    const float throwB = gainWith (0.0f);
    INFO ("throw A " << throwA << ", throw B " << throwB);

    CHECK (throwA == Catch::Approx (10.0 / 11.0).margin (1.0e-3));
    CHECK (throwB == Catch::Approx (10.0 / 110.0).margin (1.0e-3));

    juce::ignoreUnused (sw);
}

TEST_CASE ("An SPDT gangs with a plain switch sharing its label", "[schematic]")
{
    using namespace SchematicModel;

    // A real channel switch has whatever mix of contacts the amp needed, so the
    // two kinds have to gang with each other, not just with their own kind.
    Schematic sheet;
    const auto input = sheet.addElement (ElementType::Input, -8, 0);
    const auto output = sheet.addElement (ElementType::Output, 40, 0);
    const auto ground = sheet.addElement (ElementType::Ground, 40, 12);

    const auto spdt = sheet.addElement (ElementType::Spdt, 0, 0);
    const auto spst = sheet.addElement (ElementType::Switch, 20, 0);
    sheet.findElement (spdt)->label = "CHANNEL";
    sheet.findElement (spst)->label = "CHANNEL";

    auto pin = [&sheet] (int id, int p) { return sheet.findElement (id)->getPinPosition (p); };
    sheet.addWire (pin (input, 0), pin (spdt, 0));
    sheet.addWire (pin (spdt, 1), pin (spst, 0));
    sheet.addWire (pin (spdt, 2), pin (spst, 0));
    sheet.addWire (pin (spst, 1), pin (output, 0));

    const auto load = sheet.addElement (ElementType::Resistor, 40, 6);
    sheet.findElement (load)->value = 10000.0;
    sheet.addWire (pin (output, 0), pin (load, 0));
    sheet.addWire (pin (load, 1), pin (ground, 0));

    const auto result = buildCircuits (sheet, 48000.0, 1);
    INFO (result.error);
    REQUIRE (result.isValid());

    // One control, not two, holding both kinds of contact.
    REQUIRE (result.controls.size() == 1);
    CHECK (result.controls[0].name == "CHANNEL");
    CHECK (result.controls[0].toggles.size() == 1);
    CHECK (result.controls[0].changeovers.size() == 1);
}

TEST_CASE ("Panel position reorders the control strip", "[schematic]")
{
    using namespace SchematicModel;

    Schematic sheet;
    const auto input = sheet.addElement (ElementType::Input, -8, -6);
    const auto ground = sheet.addElement (ElementType::Ground, 40, 10);
    const auto output = sheet.addElement (ElementType::Output, 44, 0);

    std::vector<int> pots;
    const char* names[] = { "Gain", "Bass", "Master" };
    int x = 0;

    for (auto* name : names)
    {
        const auto id = sheet.addElement (ElementType::Potentiometer, x, 0);
        auto* p = sheet.findElement (id);
        p->value = 100000.0;
        p->label = name;
        sheet.addWire (p->getPinPosition (1), p->getPinPosition (2));

        if (! pots.empty())
            sheet.addWire (sheet.findElement (pots.back())->getPinPosition (2), p->getPinPosition (0));

        pots.push_back (id);
        x += 10;
    }

    auto pin = [&sheet] (int id, int p) { return sheet.findElement (id)->getPinPosition (p); };

    const auto load = sheet.addElement (ElementType::Resistor, x, 6);
    sheet.findElement (load)->value = 1.0e6;

    sheet.addWire (pin (input, 0), pin (pots.front(), 0));
    const auto tail = pin (pots.back(), 2);
    sheet.addWire (tail, pin (output, 0));
    sheet.addWire (tail, pin (load, 0));
    sheet.addWire (pin (load, 1), pin (ground, 0));

    // Drawn order is Gain, Bass, Master.
    auto built = buildCircuits (sheet, 48000.0, 1);
    REQUIRE (built.isValid());
    REQUIRE (built.controls.size() == 3);
    CHECK (built.controls[0].name == "Gain");
    CHECK (built.controls[2].name == "Master");

    // Send Master to the front and Gain to the back.
    sheet.findElement (pots[2])->controlOrder = -1;
    sheet.findElement (pots[0])->controlOrder = 5;

    built = buildCircuits (sheet, 48000.0, 1);
    REQUIRE (built.isValid());
    CHECK (built.controls[0].name == "Master");
    CHECK (built.controls[1].name == "Bass");
    CHECK (built.controls[2].name == "Gain");

    // And it survives a save/load round trip.
    Schematic reloaded;
    reloaded.restoreFromValueTree (sheet.toValueTree());
    CHECK (reloaded.findElement (pots[2])->controlOrder == -1);
    CHECK (reloaded.findElement (pots[0])->controlOrder == 5);
}

TEST_CASE ("Oversampling trades CPU for aliasing", "[plugin]")
{
    // A fundamental that divides neither the host rate nor its harmonics
    // neatly, so anything folding down lands somewhere obviously not harmonic.
    constexpr double frequency = 1867.0;
    constexpr int order = 14;
    constexpr int size = 1 << order;

    auto aliasToHarmonicDb = [] (PluginProcessor& plugin)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        double phase = 0.0;
        const auto step = 2.0 * juce::MathConstants<double>::pi * frequency / 48000.0;

        auto fill = [&]
        {
            for (int i = 0; i < 512; ++i)
            {
                const auto v = 0.5f * (float) std::sin (phase);
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
                phase += step;
            }
        };

        for (int b = 0; b < 40; ++b) { fill(); plugin.processBlock (buffer, midi); }

        std::vector<float> data (static_cast<size_t> (size) * 2, 0.0f);

        for (int written = 0; written < size;)
        {
            fill();
            plugin.processBlock (buffer, midi);

            for (int i = 0; i < 512 && written < size; ++i, ++written)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi
                                                       * written / (size - 1));
                data[static_cast<size_t> (written)] = static_cast<float> (buffer.getSample (0, i) * w);
            }
        }

        juce::dsp::FFT fft (order);
        fft.performFrequencyOnlyForwardTransform (data.data());

        const double binHz = 48000.0 / size;
        double harmonic = 0.0, alias = 0.0;

        for (int bin = 4; bin < size / 2; ++bin)
        {
            const double hz = bin * binHz;

            if (hz > 20000.0)
                break;

            const double magnitude = data[static_cast<size_t> (bin)];
            const double ratio = hz / frequency;
            const double nearest = std::round (ratio);
            const bool isHarmonic = nearest >= 1.0
                                 && std::abs (ratio - nearest) * frequency < 6.0 * binHz;

            (isHarmonic ? harmonic : alias) += magnitude * magnitude;
        }

        return 10.0 * std::log10 (juce::jmax (1.0e-30, alias) / juce::jmax (1.0e-30, harmonic));
    };

    auto slammed = [] (PluginProcessor& plugin)
    {
        plugin.prepareToPlay (48000.0, 512);
        auto* input = plugin.apvts.getParameter ("input");
        input->setValueNotifyingHost (input->convertTo0to1 (24.0f));
    };

    PluginProcessor plain;
    slammed (plain);
    REQUIRE (plain.setOversamplingFactor (1).isValid());
    CHECK (plain.getLatencySamples() == 0);
    const double plainAlias = aliasToHarmonicDb (plain);

    PluginProcessor doubled;
    slammed (doubled);
    REQUIRE (doubled.setOversamplingFactor (2).isValid());
    const double doubledAlias = aliasToHarmonicDb (doubled);

    INFO ("1x " << plainAlias << " dB, 2x " << doubledAlias << " dB");

    // Measured at about 20 dB of improvement; assert well inside that so the
    // test is about the mechanism working, not about the exact figure.
    CHECK (doubledAlias < plainAlias - 10.0);

    // And the host is told about the delay the halfband filters introduce,
    // otherwise everything downstream sits a few samples early.
    CHECK (doubled.getLatencySamples() > 0);
}

TEST_CASE ("Performance options do not change the output", "[plugin][schematic]")
{
    using namespace SchematicModel;

    Schematic sheet;
    const auto names = Examples::getNames();
    REQUIRE (names.size() > 0);
    Examples::load (sheet, 0);

    auto render = [&sheet] (BuildOptions options)
    {
        auto built = buildCircuits (sheet, 48000.0, 1, options);
        REQUIRE (built.isValid());

        auto& circuit = *built.circuits[0];
        std::vector<float> out;
        double phase = 0.0;

        for (int i = 0; i < 4000; ++i)
        {
            out.push_back (circuit.process (0.4f * (float) std::sin (phase)));
            phase += 2.0 * juce::MathConstants<double>::pi * 440.0 / 48000.0;
        }

        return out;
    };

    const auto reference = render ({});

    // The Newton seed only changes where the iteration starts, so it must land
    // on exactly the same root.
    BuildOptions seeded;
    seeded.predictNewtonSeed = true;
    const auto withSeed = render (seeded);

    REQUIRE (withSeed.size() == reference.size());

    for (size_t i = 0; i < reference.size(); ++i)
        REQUIRE (std::abs (withSeed[i] - reference[i]) < 1.0e-5f);

    // Fast math is an approximation, but at 3e-10 relative it has four orders
    // of headroom over the Newton tolerance it feeds.
    BuildOptions fast;
    fast.fastMath = true;
    const auto withFastMath = render (fast);

    for (size_t i = 0; i < reference.size(); ++i)
        REQUIRE (std::abs (withFastMath[i] - reference[i]) < 1.0e-5f);

    CircuitComponents::FastMath::setEnabled (false);
}

TEST_CASE ("Performance settings travel with the document", "[plugin]")
{
    PluginProcessor plugin;

    CHECK (plugin.oversamplingFactor == 1);
    CHECK (! plugin.buildOptions.fastMath);
    CHECK (! plugin.buildOptions.predictNewtonSeed);

    // An untouched sheet writes none of them, so a file saved today still
    // matches one saved before any of this existed.
    CHECK (! plugin.createDocument().hasProperty ("oversampling"));

    plugin.buildOptions.fastMath = true;
    plugin.buildOptions.predictNewtonSeed = true;
    plugin.oversamplingFactor = 4;

    const auto document = plugin.createDocument();

    PluginProcessor loaded;
    REQUIRE (loaded.restoreDocument (document));
    CHECK (loaded.oversamplingFactor == 4);
    CHECK (loaded.buildOptions.fastMath);
    CHECK (loaded.buildOptions.predictNewtonSeed);

    CircuitComponents::FastMath::setEnabled (false);
}

TEST_CASE ("Changing oversampling while audio is running is safe", "[plugin]")
{
    // This crashed Ableton: prepareOversampler() destroyed the Oversampling
    // object the audio thread was mid-call on, because the swap wasn't taken
    // under the lock processBlock holds. A use-after-free, and a plain
    // single-threaded test would never see it -- so this runs the two threads
    // against each other the way a host does.
    PluginProcessor plugin;
    plugin.prepareToPlay (48000.0, 256);

    setParameter (plugin, "input", 0.0f);
    setParameter (plugin, "output", 0.0f);
    setParameter (plugin, "bypass", 0.0f);

    std::atomic<bool> running { true };
    std::atomic<int> blocksProcessed { 0 };

    // Catch2's macros are not thread safe -- REQUIRE from here trips an
    // assertion inside its own output redirection -- so the audio thread only
    // ever sets a flag, and the checking happens on the main thread after join.
    std::atomic<bool> sawNonFinite { false };

    std::thread audio ([&plugin, &running, &blocksProcessed, &sawNonFinite]
    {
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        double phase = 0.0;

        while (running.load())
        {
            for (int i = 0; i < 256; ++i)
            {
                const auto v = 0.4f * (float) std::sin (phase);
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
                phase += 2.0 * juce::MathConstants<double>::pi * 220.0 / 48000.0;
            }

            plugin.processBlock (buffer, midi);

            for (int i = 0; i < 256; ++i)
                if (! std::isfinite (buffer.getSample (0, i)))
                    sawNonFinite.store (true);

            blocksProcessed.fetch_add (1);
        }
    });

    // Hammer the factor from this thread, which is what the Settings menu does.
    for (int pass = 0; pass < 40; ++pass)
        for (const int factor : { 2, 4, 1, 4, 2, 1 })
            REQUIRE (plugin.setOversamplingFactor (factor).isValid());

    running.store (false);
    audio.join();

    INFO ("processed " << blocksProcessed.load() << " blocks while switching");
    CHECK (blocksProcessed.load() > 0);
    CHECK (! sawNonFinite.load());

    // Whatever it landed on, the plugin still works.
    REQUIRE (plugin.setOversamplingFactor (1).isValid());
    CHECK (measurePeak (plugin, 0.5f) > 0.0f);
}

TEST_CASE ("Build messages name the part and where it is", "[schematic]")
{
    using namespace SchematicModel;

    // A message you can act on says which part and where. "3 parts have no
    // value: R1, C4, and a Capacitor at 88,-12" is a paragraph to parse; three
    // rows each naming one part is a list to work through.
    Schematic sheet;
    const auto input = sheet.addElement (ElementType::Input, -4, 0);
    const auto r = sheet.addElement (ElementType::Resistor, 6, 8);
    sheet.findElement (r)->label = "R9";
    const auto c = sheet.addElement (ElementType::Capacitor, 14, 8);
    sheet.addElement (ElementType::Output, 20, 0);
    sheet.addElement (ElementType::Ground, 6, 16);
    juce::ignoreUnused (input);

    const auto result = buildCircuits (sheet, 48000.0, 1);
    CHECK (! result.isValid());

    auto find = [&result] (const juce::String& subject)
    {
        return std::find_if (result.diagnostics.begin(), result.diagnostics.end(),
                             [&subject] (const Diagnostic& d) { return d.subject == subject; });
    };

    // The labelled one is named by its label; the unlabelled one by its type.
    const auto labelled = find ("R9");
    REQUIRE (labelled != result.diagnostics.end());
    CHECK (labelled->isError());
    CHECK (labelled->elementId == r);
    CHECK (labelled->hasPosition);
    CHECK (labelled->position == juce::Point<int> (6, 8));

    const auto unlabelled = find ("Capacitor");
    REQUIRE (unlabelled != result.diagnostics.end());
    CHECK (unlabelled->elementId == c);
    CHECK (unlabelled->position == juce::Point<int> (14, 8));
}

TEST_CASE ("An output wired to ground is reported rather than left silent", "[schematic]")
{
    using namespace SchematicModel;

    // Shorting the output to ground used to surface as a singular matrix, via a
    // phantom node: the net takes ground's name, so setOutputNode("out") makes
    // one with nothing on it and an all-zero row.
    Schematic sheet;
    const auto input = sheet.addElement (ElementType::Input, -4, 0);
    const auto r = sheet.addElement (ElementType::Resistor, 0, 0);
    sheet.findElement (r)->value = 100000.0;
    const auto output = sheet.addElement (ElementType::Output, 4, 0);
    const auto ground = sheet.addElement (ElementType::Ground, 0, 6);

    auto pin = [&sheet] (int id, int p) { return sheet.findElement (id)->getPinPosition (p); };
    sheet.addWire (pin (input, 0), pin (r, 0));
    sheet.addWire (pin (r, 1), pin (output, 0));
    sheet.addWire (pin (r, 1), pin (ground, 0));

    const auto result = buildCircuits (sheet, 48000.0, 1);
    INFO (result.error);
    CHECK (! result.isValid());
    CHECK (result.error.contains ("wired to ground"));
}

TEST_CASE ("Ground names a shared net whatever order it was drawn in", "[schematic]")
{
    using namespace SchematicModel;

    // The same sheet as above, but with the Ground drawn *first* -- the order
    // a drawing usually happens in. Net naming used to run in element order
    // with the last terminal winning, so the Output named the shared net
    // "out": the sheet then had no ground net at all, and the friendly
    // diagnostic above never fired.
    Schematic sheet;
    const auto ground = sheet.addElement (ElementType::Ground, 0, 6);
    const auto r = sheet.addElement (ElementType::Resistor, 0, 0);
    sheet.findElement (r)->value = 100000.0;
    const auto input = sheet.addElement (ElementType::Input, -4, 0);
    const auto output = sheet.addElement (ElementType::Output, 4, 0);

    auto pin = [&sheet] (int id, int p) { return sheet.findElement (id)->getPinPosition (p); };
    sheet.addWire (pin (input, 0), pin (r, 0));
    sheet.addWire (pin (r, 1), pin (output, 0));
    sheet.addWire (pin (r, 1), pin (ground, 0));

    const auto result = buildCircuits (sheet, 48000.0, 1);
    INFO (result.error);
    CHECK (! result.isValid());
    CHECK (result.error.contains ("wired to ground"));
}

TEST_CASE ("An input wired straight to the output is reported", "[schematic]")
{
    using namespace SchematicModel;

    // Terminals stamp nothing, so whichever name the shared net got, the other
    // node was invented empty and the system was singular -- a confusing way to
    // learn the two terminals were shorted.
    Schematic sheet;
    const auto input = sheet.addElement (ElementType::Input, -4, 0);
    const auto output = sheet.addElement (ElementType::Output, 4, 0);
    sheet.addElement (ElementType::Ground, 0, 6);

    auto pin = [&sheet] (int id, int p) { return sheet.findElement (id)->getPinPosition (p); };
    sheet.addWire (pin (input, 0), pin (output, 0));

    const auto result = buildCircuits (sheet, 48000.0, 1);
    INFO (result.error);
    CHECK (! result.isValid());
    CHECK (result.error.contains ("wired together"));
}

TEST_CASE ("An input wired to ground is reported rather than left silent", "[schematic]")
{
    using namespace SchematicModel;

    // The third degenerate terminal case, and the only one that used to pass
    // validation. Its two siblings above end in a singular matrix, which is at
    // least loud about it. This one does not: the net keeps ground's name, so
    // setInputNode("in") invents a node nothing is attached to, and since the
    // input node is a known voltage rather than an unknown it takes no row.
    // Nothing is singular, so the build succeeded, the bias point solved, and
    // every sample came out at zero -- measured before the fix as a peak output
    // of exactly 0.0 for a full-scale input, with no diagnostic of any kind.
    Schematic sheet;
    const auto input = sheet.addElement (ElementType::Input, -4, 0);
    const auto r = sheet.addElement (ElementType::Resistor, 0, 0);
    sheet.findElement (r)->value = 100000.0;
    const auto output = sheet.addElement (ElementType::Output, 4, 0);
    const auto ground = sheet.addElement (ElementType::Ground, -4, 6);

    auto pin = [&sheet] (int id, int p) { return sheet.findElement (id)->getPinPosition (p); };
    sheet.addWire (pin (input, 0), pin (r, 0));
    sheet.addWire (pin (r, 1), pin (output, 0));
    sheet.addWire (pin (input, 0), pin (ground, 0)); // the mistake

    const auto result = buildCircuits (sheet, 48000.0, 1);
    INFO (result.error);
    CHECK (! result.isValid());
    CHECK (result.error.contains ("input terminal is wired to ground"));

    // The headline also has to reach the console, or the sheet says nothing.
    CHECK (! result.diagnostics.empty());
}

TEST_CASE ("The same sheet with the ground moved off the input builds", "[schematic]")
{
    using namespace SchematicModel;

    // The control for the test above: identical drawing, ground moved to the
    // output side instead. Without this, a check that rejected *any* sheet
    // would pass the test above just as well.
    Schematic sheet;
    const auto input = sheet.addElement (ElementType::Input, -4, 0);
    const auto r = sheet.addElement (ElementType::Resistor, 0, 0);
    sheet.findElement (r)->value = 100000.0;
    const auto r2 = sheet.addElement (ElementType::Resistor, 4, 3);
    sheet.findElement (r2)->value = 100000.0;
    const auto output = sheet.addElement (ElementType::Output, 4, 0);
    const auto ground = sheet.addElement (ElementType::Ground, 4, 8);

    auto pin = [&sheet] (int id, int p) { return sheet.findElement (id)->getPinPosition (p); };
    sheet.addWire (pin (input, 0), pin (r, 0));
    sheet.addWire (pin (r, 1), pin (output, 0));
    sheet.addWire (pin (r, 1), pin (r2, 0));
    sheet.addWire (pin (r2, 1), pin (ground, 0));

    const auto result = buildCircuits (sheet, 48000.0, 1);
    INFO (result.error);
    REQUIRE (result.isValid());
    REQUIRE (! result.circuits.empty());

    // And it is genuinely not silent -- a divider passes half of what it is
    // given, which is the thing the rejected sheet above could not do.
    auto& circuit = *result.circuits[0];
    float peak = 0.0f;

    for (int i = 0; i < 480; ++i)
        peak = juce::jmax (peak, std::abs (circuit.process (1.0f)));

    CHECK (peak == Catch::Approx (0.5f).margin (0.01));
}

TEST_CASE ("A bare letter names a part", "[plugin]")
{
    // The rule the shortcut map runs on: an unmodified letter arms a part, a
    // modified one acts on what is there. D is the diode for that reason, which
    // is why the Delete tool has no letter -- it keeps the toolbar button, the
    // Delete key and right-click.
    runWithinPluginEditor ([] (PluginProcessor& plugin) {
        using namespace SchematicModel;
        using SchematicUI::SchematicCanvas;

        auto* editor = plugin.getActiveEditor();

        auto* canvas = [editor]() -> SchematicCanvas*
        {
            for (auto* child : editor->getChildren())
                if (auto* c = dynamic_cast<SchematicCanvas*> (child))
                    return c;

            return nullptr;
        }();

        REQUIRE (canvas != nullptr);

        struct Binding { juce::juce_wchar key; ElementType type; };

        for (const auto& binding : { Binding { 'r', ElementType::Resistor },
                                     Binding { 'c', ElementType::Capacitor },
                                     Binding { 'l', ElementType::Inductor },
                                     Binding { 't', ElementType::Transistor },
                                     Binding { 'd', ElementType::Diode },
                                     Binding { 'g', ElementType::Ground },
                                     Binding { 'b', ElementType::Rectangle } })
        {
            canvas->setTool (SchematicCanvas::Tool::Select);
            REQUIRE (canvas->keyPressed (juce::KeyPress (binding.key)));

            INFO ("'" << juce::String::charToString (binding.key) << "' should arm "
                      << getElementInfo (binding.type).name);
            CHECK (canvas->getTool() == SchematicCanvas::Tool::Place);
            CHECK (canvas->getPendingType() == binding.type);
        }

        // S still picks the select tool, and W the wire.
        canvas->setTool (SchematicCanvas::Tool::Delete);
        CHECK (canvas->keyPressed (juce::KeyPress ('s')));
        CHECK (canvas->getTool() == SchematicCanvas::Tool::Select);

        CHECK (canvas->keyPressed (juce::KeyPress ('w')));
        CHECK (canvas->getTool() == SchematicCanvas::Tool::Wire);

        // X, not D -- D is the diode.
        CHECK (canvas->keyPressed (juce::KeyPress ('x')));
        CHECK (canvas->getTool() == SchematicCanvas::Tool::Delete);

        // F frames the drawing. It moves the view rather than the sheet, so it
        // leaves the tool alone -- and it must not be swallowed by the part
        // letters, which is the collision worth guarding against.
        CHECK (canvas->keyPressed (juce::KeyPress ('f')));
        CHECK (canvas->getTool() == SchematicCanvas::Tool::Delete);
    });
}

TEST_CASE ("Cloning a part carries its properties", "[schematic]")
{
    using namespace SchematicModel;

    // What middle-click does: pick a part up, then every click places another
    // with the same value, model, label and settings -- not just the same type.
    Schematic sheet;
    const auto source = sheet.addElement (ElementType::Capacitor, 4, 4);

    {
        auto* original = sheet.findElement (source);
        original->value = 22.0e-9;
        original->label = "C7";
        original->polarised = true;
        original->orientation = 1;
        original->mirrored = true;
    }

    // Taken by value, exactly as cloneElement does -- adding the next part can
    // reallocate the element list and invalidate any pointer into it.
    const Element picked = *sheet.findElement (source);

    const auto copy = sheet.addElement (ElementType::Resistor, 20, 20);
    auto* placed = sheet.findElement (copy);
    const auto copyId = placed->id;

    picked.copyPropertiesTo (*placed);

    // Everything that makes it that part came across...
    CHECK (placed->type == ElementType::Capacitor);
    CHECK (std::abs (placed->value - 22.0e-9) < 1.0e-18);
    CHECK (placed->label == "C7");
    CHECK (placed->polarised);

    // ...and nothing about which one it is or where it sits did.
    CHECK (placed->id == copyId);
    CHECK (placed->x == 20);
    CHECK (placed->y == 20);
    CHECK (placed->orientation == 0);
    CHECK (placed->mirrored == false);
}

TEST_CASE ("A control's position is one number whichever kind it is", "[schematic]")
{
    using namespace SchematicModel;

    Element pot;
    pot.type = ElementType::Potentiometer;
    CHECK (pot.getControlPosition() == Catch::Approx (0.5f));   // noon, not zero

    pot.setControlPosition (0.25f);
    CHECK (pot.getControlPosition() == Catch::Approx (0.25f));

    // Clamped rather than refused -- a knob past an end has an obvious meaning.
    pot.setControlPosition (1.7f);
    CHECK (pot.getControlPosition() == Catch::Approx (1.0f));

    for (const auto type : { ElementType::Switch, ElementType::Spdt })
    {
        Element sw;
        sw.type = type;
        sw.closed = true;
        CHECK (sw.getControlPosition() == Catch::Approx (1.0f));

        sw.setControlPosition (0.0f);
        CHECK_FALSE (sw.closed);

        sw.setControlPosition (1.0f);
        CHECK (sw.closed);
    }

    // Everything else has no control, and says so rather than pretending.
    Element resistor;
    resistor.type = ElementType::Resistor;
    resistor.setControlPosition (0.0f);
    CHECK (resistor.getControlPosition() == Catch::Approx (0.5f));
}

TEST_CASE ("A knob position survives a save and a clone", "[schematic]")
{
    using namespace SchematicModel;

    Schematic sheet;
    const auto id = sheet.addElement (ElementType::Potentiometer, 0, 0);
    sheet.findElement (id)->value = 250000.0;
    sheet.findElement (id)->controlPosition = 0.31;

    Schematic reloaded;
    reloaded.restoreFromValueTree (sheet.toValueTree());

    REQUIRE (reloaded.getElements().size() == 1);
    CHECK (reloaded.getElements()[0].controlPosition == Catch::Approx (0.31));

    // Middle-click clones a part with its properties, and where its knob sits is
    // one of them -- copyPropertiesTo is the single place that says what "the
    // same part" means.
    Element clone;
    reloaded.getElements()[0].copyPropertiesTo (clone);
    CHECK (clone.controlPosition == Catch::Approx (0.31));

    // A sheet written before parts remembered their knobs reads as centred, not
    // as zero -- those pots were drawn without a position at all.
    auto tree = sheet.toValueTree();
    tree.getChild (0).removeProperty ("controlPosition", nullptr);

    Schematic older;
    older.restoreFromValueTree (tree);
    CHECK (older.getElements()[0].controlPosition == Catch::Approx (0.5));
}

TEST_CASE ("A knob follows its part when the control slots shift", "[editor]")
{
    using namespace SchematicModel;

    // The whole point of a part remembering where its knob was. Controls are
    // mapped onto knob1..knob16 in order, so inserting one that sorts first
    // shifts every control down a slot. Without this, Volume's position stays
    // on knob1 and is quietly inherited by whatever now lives there -- you add
    // a gain control and your master volume jumps.
    runWithinPluginEditor ([] (PluginProcessor& plugin)
    {
        auto& sheet = plugin.getSchematic();
        sheet.clear();

        const auto input = sheet.addElement (ElementType::Input, -8, -6);
        const auto ground = sheet.addElement (ElementType::Ground, 40, 12);
        const auto output = sheet.addElement (ElementType::Output, 44, 0);

        std::vector<int> pots;
        int x = 0;

        auto addPot = [&] (const char* name, int order)
        {
            const auto id = sheet.addElement (ElementType::Potentiometer, x, 0);
            auto* p = sheet.findElement (id);
            p->value = 100000.0;
            p->label = name;
            p->controlOrder = order;
            sheet.addWire (p->getPinPosition (1), p->getPinPosition (2));

            if (! pots.empty())
                sheet.addWire (sheet.findElement (pots.back())->getPinPosition (2), p->getPinPosition (0));

            pots.push_back (id);
            x += 10;
            return id;
        };

        const auto volume = addPot ("Volume", 0);
        const auto tone = addPot ("Tone", 0);

        auto pin = [&sheet] (int id, int p) { return sheet.findElement (id)->getPinPosition (p); };
        const auto load = sheet.addElement (ElementType::Resistor, 40, 6);
        sheet.findElement (load)->value = 1.0e6;
        sheet.addWire (pin (input, 0), pin (pots.front(), 0));
        sheet.addWire (pin (pots.back(), 2), pin (output, 0));
        sheet.addWire (pin (pots.back(), 2), pin (load, 0));
        sheet.addWire (pin (load, 1), pin (ground, 0));

        REQUIRE (plugin.rebuild().isValid());
        REQUIRE (plugin.getLiveControls().size() == 2);

        auto knob = [&plugin] (int slot) { return plugin.apvts.getParameter (PluginProcessor::getControlParameterId (slot)); };

        // Play with them, as the strip does.
        knob (0)->setValueNotifyingHost (0.2f);
        knob (1)->setValueNotifyingHost (0.8f);

        // Which the parts remember -- the strip writes back as it is dragged,
        // and this is that same direction in one go.
        plugin.adoptControlPositions();
        CHECK (sheet.findElement (volume)->controlPosition == Catch::Approx (0.2).margin (1.0e-3));
        CHECK (sheet.findElement (tone)->controlPosition == Catch::Approx (0.8).margin (1.0e-3));

        // Now add one that sorts ahead of both, pushing them down a slot each.
        const auto gain = addPot ("Gain", -1);
        sheet.findElement (gain)->controlPosition = 0.65;
        sheet.addWire (pin (gain, 1), pin (gain, 2));

        REQUIRE (plugin.rebuild().isValid());

        const auto controls = plugin.getLiveControls();
        REQUIRE (controls.size() == 3);
        CHECK (controls[0].name == "Gain");
        CHECK (controls[1].name == "Volume");
        CHECK (controls[2].name == "Tone");

        // The editor rebuilds the strip off this, bounced through the message
        // thread, which is why the loop has to run before anything is checked.
        REQUIRE (plugin.onSchematicReplaced != nullptr);
        plugin.onSchematicReplaced();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        CHECK (knob (0)->getValue() == Catch::Approx (0.65f).margin (1.0e-3));
        CHECK (knob (1)->getValue() == Catch::Approx (0.2f).margin (1.0e-3));
        CHECK (knob (2)->getValue() == Catch::Approx (0.8f).margin (1.0e-3));
    });
}


TEST_CASE ("Pots sharing a label gang onto one shaft", "[schematic]")
{
    using namespace SchematicModel;

    // A dual-gang volume is one shaft turning two tracks. Two cascaded pots,
    // the second a thousand times the first so it barely loads it, so the gain
    // is roughly the product of the two positions -- which is how the test can
    // tell that both tracks moved and not just the one the control is named
    // after.
    Schematic sheet;

    auto place = [&sheet] (ElementType type, int x, int y, double value = 0.0)
    {
        const auto id = sheet.addElement (type, x, y);
        sheet.findElement (id)->value = value;
        return id;
    };

    const auto a = place (ElementType::Potentiometer, 0, 0, 1000.0);
    const auto b = place (ElementType::Potentiometer, 10, 0, 1.0e6);
    sheet.findElement (a)->label = "Volume";
    sheet.findElement (b)->label = "Volume";

    place (ElementType::Input, -2, -2);     // pin lands on A's top
    place (ElementType::Ground, 0, 4);      // pin lands on A's bottom
    place (ElementType::Ground, 10, 4);     // and on B's
    place (ElementType::Output, 15, 0);     // pin lands on B's wiper

    auto pin = [&sheet] (int id, int p) { return sheet.findElement (id)->getPinPosition (p); };
    sheet.addWire (pin (a, 1), pin (b, 0));

    const auto result = buildCircuits (sheet, 48000.0, 1);
    INFO (result.error);
    REQUIRE (result.isValid());

    for (const auto& diagnostic : result.diagnostics)
        INFO (diagnostic.text);

    CHECK (result.diagnostics.empty());

    // One control, two tracks, two parts.
    REQUIRE (result.controls.size() == 1);
    CHECK (result.controls[0].name == "Volume");
    CHECK (result.controls[0].pots.size() == 2);
    REQUIRE (result.controls[0].elementIds.size() == 2);
    CHECK (result.controls[0].elementIds[0] == a);
    CHECK (result.controls[0].elementIds[1] == b);

    auto& circuit = *result.circuits[0];

    auto gainWith = [&circuit, &result] (float position)
    {
        result.controls[0].apply (circuit, position);
        float out = 0.0f;

        for (int i = 0; i < 64; ++i)
            out = circuit.process (1.0f);

        return out;
    };

    // Both wide open: everything through. Had only one track been on the shaft
    // the other would have sat where addPotentiometer left it, at half, and
    // this would read about 0.5.
    CHECK (gainWith (1.0f) == Catch::Approx (1.0).margin (0.01));

    // Both at half: a quarter, not a half.
    CHECK (gainWith (0.5f) == Catch::Approx (0.25).margin (0.01));
}

TEST_CASE ("Turning a ganged knob moves every part on the shaft", "[editor]")
{
    using namespace SchematicModel;

    runWithinPluginEditor ([] (PluginProcessor& plugin)
    {
        auto& sheet = plugin.getSchematic();
        sheet.clear();

        auto place = [&sheet] (ElementType type, int x, int y, double value = 0.0)
        {
            const auto id = sheet.addElement (type, x, y);
            sheet.findElement (id)->value = value;
            return id;
        };

        const auto a = place (ElementType::Potentiometer, 0, 0, 1000.0);
        const auto b = place (ElementType::Potentiometer, 10, 0, 1.0e6);
        sheet.findElement (a)->label = "Volume";
        sheet.findElement (b)->label = "Volume";

        place (ElementType::Input, -2, -2);
        place (ElementType::Ground, 0, 4);
        place (ElementType::Ground, 10, 4);
        place (ElementType::Output, 15, 0);

        auto pin = [&sheet] (int id, int p) { return sheet.findElement (id)->getPinPosition (p); };
        sheet.addWire (pin (a, 1), pin (b, 0));

        REQUIRE (plugin.rebuild().isValid());
        REQUIRE (plugin.getLiveControls().size() == 1);

        auto* knob = plugin.apvts.getParameter (PluginProcessor::getControlParameterId (0));
        REQUIRE (knob != nullptr);
        knob->setValueNotifyingHost (0.3f);

        // Both halves have to remember it, or the next build seeds the shaft
        // from whichever was drawn first and the other silently disagrees.
        plugin.adoptControlPositions();
        CHECK (sheet.findElement (a)->controlPosition == Catch::Approx (0.3).margin (1.0e-3));
        CHECK (sheet.findElement (b)->controlPosition == Catch::Approx (0.3).margin (1.0e-3));
    });
}

TEST_CASE ("A rebuilt switch shows its throw straight away", "[editor]")
{
    using namespace SchematicModel;

    // The strip's buttons have no attachment, so a refresh left them at their
    // default off and the editor's timer corrected them a tick later -- which
    // read as the bottom bar flickering on every Rebuild. Sliders never did it,
    // because SliderAttachment seeds them as it is constructed.
    runWithinPluginEditor ([] (PluginProcessor& plugin)
    {
        auto& sheet = plugin.getSchematic();
        sheet.clear();

        // Laid out so no wire's L-corner lands on another part's pin or centre:
        // a coincident point is a connection, so a corner meeting one silently
        // shorts the parts -- and the builder now refuses such sheets.
        const auto input = sheet.addElement (ElementType::Input, -8, 0);
        const auto feed = sheet.addElement (ElementType::Resistor, -4, -4);
        const auto sw = sheet.addElement (ElementType::Switch, 0, 0);
        const auto load = sheet.addElement (ElementType::Resistor, 4, 4);
        const auto ground = sheet.addElement (ElementType::Ground, 4, 8);
        const auto output = sheet.addElement (ElementType::Output, 8, 0);

        sheet.findElement (feed)->value = 100000.0;
        sheet.findElement (load)->value = 1.0e6;

        auto* button = sheet.findElement (sw);
        button->label = "Bright";
        button->closed = true;

        auto pin = [&sheet] (int id, int p) { return sheet.findElement (id)->getPinPosition (p); };
        sheet.addWire (pin (input, 0), pin (feed, 0));
        sheet.addWire (pin (feed, 1), pin (sw, 0));
        sheet.addWire (pin (sw, 1), pin (load, 0));
        sheet.addWire (pin (sw, 1), pin (output, 0));
        sheet.addWire (pin (load, 1), pin (ground, 0));

        REQUIRE (plugin.rebuild().isValid());
        REQUIRE (plugin.getLiveControls().size() == 1);

        SchematicUI::ControlStrip strip (plugin);
        strip.refresh();

        // Whatever refresh() left on screen, before any timer has run.
        std::function<juce::ToggleButton*(juce::Component&)> find =
            [&find] (juce::Component& c) -> juce::ToggleButton*
        {
            for (auto* child : c.getChildren())
            {
                if (auto* t = dynamic_cast<juce::ToggleButton*> (child))
                    return t;

                if (auto* found = find (*child))
                    return found;
            }

            return nullptr;
        };

        auto* toggle = find (strip);
        REQUIRE (toggle != nullptr);
        CHECK (toggle->getToggleState());
    });
}


TEST_CASE ("A pot's wiper is drawn where its knob is", "[schematic]")
{
    using namespace SchematicModel;

    // The symbol used to park the wiper mid-track whatever the knob said, so
    // turning one changed nothing on the sheet.
    auto render = [] (double position)
    {
        Element element;
        element.type = ElementType::Potentiometer;
        element.value = 100000.0;
        element.controlPosition = position;

        juce::Image image (juce::Image::ARGB, 120, 120, true);
        juce::Graphics g (image);

        SchematicUI::SymbolPainter painter;
        painter.gridSize = 14.0f;
        painter.origin = { 60.0f, 60.0f };
        painter.draw (g, element, juce::Colours::white);

        return image;
    };

    auto differs = [] (const juce::Image& left, const juce::Image& right)
    {
        for (int y = 0; y < left.getHeight(); ++y)
            for (int x = 0; x < left.getWidth(); ++x)
                if (left.getPixelAt (x, y) != right.getPixelAt (x, y))
                    return true;

        return false;
    };

    const auto down = render (0.0);
    const auto middle = render (0.5);
    const auto up = render (1.0);

    CHECK (differs (down, middle));
    CHECK (differs (middle, up));
    CHECK (differs (down, up));
}


TEST_CASE ("Two nodes sharing a label are one net", "[schematic]")
{
    using namespace SchematicModel;

    // A divider whose halves are joined only by a pair of labelled nodes, with
    // no wire between them at all -- the lower half is drawn twenty squares
    // away. If the label does not join them the bottom resistor is dangling,
    // so the measured ratio is the proof rather than the netlist.
    Schematic sheet;

    auto place = [&sheet] (ElementType type, int x, int y, double value = 0.0)
    {
        const auto id = sheet.addElement (type, x, y);
        sheet.findElement (id)->value = value;
        return id;
    };

    auto pin = [&sheet] (int id, int p) { return sheet.findElement (id)->getPinPosition (p); };

    const auto top = place (ElementType::Resistor, 0, 0, 10000.0);
    const auto input = place (ElementType::Input, -2, -2);       // pin lands on top's head

    const auto nodeA = place (ElementType::Node, 0, 4);
    sheet.findElement (nodeA)->label = "MID";
    sheet.addWire (pin (top, 1), pin (nodeA, 0));

    // The other half, well away, joined by nothing but the name.
    const auto nodeB = place (ElementType::Node, 20, 0);
    sheet.findElement (nodeB)->label = "MID";

    const auto bottom = place (ElementType::Resistor, 20, 6, 10000.0);
    const auto ground = place (ElementType::Ground, 20, 12);
    const auto output = place (ElementType::Output, 24, 2);

    sheet.addWire (pin (nodeB, 0), pin (bottom, 0));
    sheet.addWire (pin (bottom, 1), pin (ground, 0));
    sheet.addWire (pin (nodeB, 0), pin (output, 0));

    const auto result = buildCircuits (sheet, 48000.0, 1);
    INFO (result.error);
    REQUIRE (result.isValid());

    for (const auto& d : result.diagnostics)
        INFO (d.text);

    CHECK (result.diagnostics.empty());

    // Two equal resistors: half in, half out. Only true if MID joined them.
    auto& circuit = *result.circuits[0];
    float out = 0.0f;

    for (int i = 0; i < 64; ++i)
        out = circuit.process (1.0f);

    INFO ("gain " << out);
    CHECK (out == Catch::Approx (0.5).margin (1.0e-3));

    juce::ignoreUnused (input);
}

TEST_CASE ("An unlabelled node joins nothing, and says so", "[schematic]")
{
    using namespace SchematicModel;

    // The label is the whole of what a node is for, and the symbol looks the
    // same either way -- so a build that quietly did nothing would be the worst
    // of the options.
    Schematic sheet;

    auto place = [&sheet] (ElementType type, int x, int y, double value = 0.0)
    {
        const auto id = sheet.addElement (type, x, y);
        sheet.findElement (id)->value = value;
        return id;
    };

    auto pin = [&sheet] (int id, int p) { return sheet.findElement (id)->getPinPosition (p); };

    // A divider: input through R to the output, with a load down to ground.
    // (The original sheet shorted the output straight to the ground symbol and
    // only ever built because net naming happened to hide it.)
    const auto r = place (ElementType::Resistor, 0, 0, 10000.0);
    place (ElementType::Input, -2, -2);
    const auto load = place (ElementType::Resistor, 4, 6, 10000.0);
    const auto ground = place (ElementType::Ground, 4, 10);
    const auto output = place (ElementType::Output, 4, 2);
    const auto orphan = place (ElementType::Node, 10, 0);   // no label

    sheet.addWire (pin (r, 1), pin (output, 0));
    sheet.addWire (pin (output, 0), pin (load, 0));
    sheet.addWire (pin (load, 1), pin (ground, 0));

    const auto result = buildCircuits (sheet, 48000.0, 1);
    INFO (result.error);
    REQUIRE (result.isValid());   // a warning, not an error -- it still builds

    const auto complained =
        std::any_of (result.diagnostics.begin(), result.diagnostics.end(),
                     [orphan] (const Diagnostic& d)
                     { return d.elementId == orphan && d.text.containsIgnoreCase ("no label"); });

    CHECK (complained);
}

TEST_CASE ("Wires are selectable, movable and deletable", "[editor]")
{
    using namespace SchematicModel;

    runWithinPluginEditor ([] (PluginProcessor& plugin)
    {
        auto& sheet = plugin.getSchematic();
        sheet.clear();

        sheet.addWire ({ 0, 0 }, { 10, 0 });
        sheet.addWire ({ 0, 6 }, { 10, 6 });
        REQUIRE (sheet.getWires().size() == 2);

        // Ids, not indices: deleting the first must not renumber the second.
        const int first = sheet.getWires()[0].id;
        const int second = sheet.getWires()[1].id;
        CHECK (first != second);
        CHECK (sheet.findWire (first) != nullptr);

        SchematicUI::SchematicCanvas canvas (sheet);
        canvas.setSize (600, 400);
        canvas.zoomToFit();

        // A click on the wire selects it; a click well clear of it does not.
        const auto onWire = canvas.pixelAt ({ 5.0f, 0.0f });
        const auto offWire = canvas.pixelAt ({ 5.0f, 3.0f });

        CHECK (canvas.wireAt (onWire) == first);
        CHECK (canvas.wireAt (offWire) == -1);

        // Moving takes both ends with it.
        canvas.setSelectedWires ({ second });
        sheet.moveWire (second, { 4, -2 });
        const auto* moved = sheet.findWire (second);
        REQUIRE (moved != nullptr);
        CHECK (moved->a == juce::Point<int> (4, 4));
        CHECK (moved->b == juce::Point<int> (14, 4));

        // Deleting the selection takes the wire and leaves its neighbour.
        canvas.deleteSelection();
        CHECK (sheet.getWires().size() == 1);
        CHECK (sheet.findWire (second) == nullptr);
        CHECK (sheet.findWire (first) != nullptr);
        CHECK (canvas.getSelectedWires().empty());
    });
}

TEST_CASE ("A wire between two parts is clickable along its span", "[editor]")
{
    using namespace SchematicModel;

    runWithinPluginEditor ([] (PluginProcessor& plugin)
    {
        auto& sheet = plugin.getSchematic();
        sheet.clear();

        // Two resistors with a long wire between them -- the ordinary case, and
        // the one where a part's bounds could swallow clicks meant for the wire.
        const auto left = sheet.addElement (ElementType::Resistor, 0, 0);
        const auto right = sheet.addElement (ElementType::Resistor, 16, 0);
        sheet.findElement (left)->value = 1000.0;
        sheet.findElement (right)->value = 1000.0;

        auto pin = [&sheet] (int id, int p) { return sheet.findElement (id)->getPinPosition (p); };
        sheet.addWire (pin (left, 1), pin (right, 1));
        const int wire = sheet.getWires().front().id;

        SchematicUI::SchematicCanvas canvas (sheet);
        canvas.setSize (700, 420);
        canvas.zoomToFit();

        // Walk the middle of the span. Every point along it should find the
        // wire, and none of it should be claimed by either resistor.
        int wireHits = 0, partHits = 0;

        for (int x = 4; x <= 12; ++x)
        {
            const auto at = canvas.pixelAt ({ (float) x, 2.0f });

            if (canvas.wireAt (at) == wire)
                ++wireHits;

            if (canvas.elementAt (at) >= 0)
                ++partHits;
        }

        INFO ("wire hits " << wireHits << " of 9, part hits " << partHits);
        CHECK (wireHits == 9);
        CHECK (partHits == 0);
    });
}


TEST_CASE ("Collinear wires join, and only collinear ones", "[schematic]")
{
    using namespace SchematicModel;

    auto span = [] (const Wire& w)
    {
        return std::pair { juce::jmin (w.a.x, w.b.x), juce::jmax (w.a.x, w.b.x) };
    };

    SECTION ("two squares plus four squares is one six-square wire")
    {
        Schematic sheet;
        sheet.addWire ({ 0, 0 }, { 2, 0 });
        sheet.addWire ({ 2, 0 }, { 6, 0 });
        REQUIRE (sheet.getWires().size() == 2);

        CHECK (sheet.mergeCollinearWires() == 1);
        REQUIRE (sheet.getWires().size() == 1);

        const auto& merged = sheet.getWires().front();
        CHECK (span (merged) == std::pair { 0, 6 });
        CHECK (merged.a.y == 0);
        CHECK (merged.b.y == 0);
    }

    SECTION ("a gap between them is not a join")
    {
        Schematic sheet;
        sheet.addWire ({ 0, 0 }, { 2, 0 });
        sheet.addWire ({ 4, 0 }, { 6, 0 });

        CHECK (sheet.mergeCollinearWires() == 0);
        CHECK (sheet.getWires().size() == 2);
    }

    SECTION ("a corner is two wires, not one")
    {
        // Perpendicular, so there is no line for them to share. An L drawn by
        // one diagonal drag is exactly this, and folding it into a single wire
        // would mean inventing a wire that cannot be drawn.
        Schematic sheet;
        sheet.addWire ({ 0, 0 }, { 6, 0 });
        sheet.addWire ({ 6, 0 }, { 6, 4 });

        CHECK (sheet.mergeCollinearWires() == 0);
        CHECK (sheet.getWires().size() == 2);
    }

    SECTION ("a T keeps its stem, and keeps its dot")
    {
        // The bar drawn as two halves with a stem meeting the seam. The halves
        // join -- they are one straight line and they look like one -- but the
        // stem stays its own wire.
        //
        // The dot is the half of this worth pinning. Merging removes the two
        // wire *ends* that used to sit at the seam, and a junction found by
        // counting ends alone would go quiet exactly where the drawing most
        // needs to say "these are connected". findJunctions also asks whether an
        // end sits strictly inside another wire, which is what the seam becomes.
        Schematic sheet;
        sheet.addWire ({ 0, 0 }, { 3, 0 });
        sheet.addWire ({ 3, 0 }, { 6, 0 });
        sheet.addWire ({ 3, 0 }, { 3, 4 });

        REQUIRE (sheet.findJunctions().size() == 1);

        CHECK (sheet.mergeCollinearWires() == 1);
        REQUIRE (sheet.getWires().size() == 2);

        const auto junctions = sheet.findJunctions();
        REQUIRE (junctions.size() == 1);
        CHECK (junctions.front() == juce::Point<int> (3, 0));
    }

    SECTION ("one join can open another")
    {
        // Three in a row, joined a pair at a time. A single pass would leave the
        // result depending on the order they happen to sit in.
        Schematic sheet;
        sheet.addWire ({ 0, 0 }, { 2, 0 });
        sheet.addWire ({ 4, 0 }, { 6, 0 });
        sheet.addWire ({ 2, 0 }, { 4, 0 });

        CHECK (sheet.mergeCollinearWires() == 2);
        REQUIRE (sheet.getWires().size() == 1);
        CHECK (span (sheet.getWires().front()) == std::pair { 0, 6 });
    }

    SECTION ("the wire you were dragging is the one that survives")
    {
        Schematic sheet;
        sheet.addWire ({ 0, 0 }, { 2, 0 });
        sheet.addWire ({ 2, 0 }, { 6, 0 });

        const int second = sheet.getWires()[1].id;

        CHECK (sheet.mergeCollinearWires (second) == 1);
        REQUIRE (sheet.getWires().size() == 1);
        CHECK (sheet.getWires().front().id == second);
        CHECK (sheet.findWire (second) != nullptr);
    }
}

TEST_CASE ("A wire end drags along its own axis", "[schematic]")
{
    using namespace SchematicModel;

    Schematic sheet;
    sheet.addWire ({ 0, 0 }, { 6, 0 });
    const int id = sheet.getWires().front().id;

    // Off-axis entirely: only the coordinate that runs along the wire is taken,
    // so the wire cannot be dragged into a diagonal it has no meaning for.
    REQUIRE (sheet.resizeWireEnd (id, 1, { 9, 5 }));
    CHECK (sheet.findWire (id)->b == juce::Point<int> (9, 0));
    CHECK (sheet.findWire (id)->a == juce::Point<int> (0, 0));

    // Onto the other end is the one refusal -- that is a zero-length wire.
    CHECK (! sheet.resizeWireEnd (id, 1, { 0, 4 }));
    CHECK (sheet.findWire (id)->b == juce::Point<int> (9, 0));

    // Asking for where it already is changes nothing and says so.
    CHECK (! sheet.resizeWireEnd (id, 1, { 9, 0 }));

    // The other end moves independently.
    REQUIRE (sheet.resizeWireEnd (id, 0, { -3, 0 }));
    CHECK (sheet.findWire (id)->a == juce::Point<int> (-3, 0));

    // A vertical wire takes its y from the pointer and keeps its x.
    Schematic column;
    column.addWire ({ 4, 0 }, { 4, 6 });
    const int vertical = column.getWires().front().id;

    REQUIRE (column.resizeWireEnd (vertical, 1, { 11, 9 }));
    CHECK (column.findWire (vertical)->b == juce::Point<int> (4, 9));
}

TEST_CASE ("A node's tag is as wide as its name, and clickable across it", "[schematic]")
{
    using namespace SchematicModel;

    Schematic sheet;

    const auto shortName = sheet.addElement (ElementType::Node, 0, 0);
    sheet.findElement (shortName)->label = "out";

    const auto longName = sheet.addElement (ElementType::Node, 0, 20);
    sheet.findElement (longName)->label = "power_amp_grid";

    const auto wide = sheet.getElementBounds (*sheet.findElement (longName));
    const auto narrow = sheet.getElementBounds (*sheet.findElement (shortName));

    INFO ("short " << narrow.toString() << " long " << wide.toString());
    CHECK (wide.getWidth() > narrow.getWidth());

    // A fixed height whatever it says -- the tag only grows sideways. Three and
    // not two because this is the hit box, which counts grid lines rather than
    // squares; the drawn tag is two squares tall.
    CHECK (narrow.getHeight() == 3);
    CHECK (wide.getHeight() == 3);
    CHECK (sheet.findElement (longName)->getNodeBounds().getHeight() == 2);

    // The point lands on the pin, which is what a tag pointing at a wire means
    // -- and the tip is clickable, which is the obvious place to aim.
    const auto pin = sheet.findElement (longName)->getPinPosition (0);
    CHECK (pin == juce::Point<int> (2, 20));
    CHECK (wide.contains (pin));

    // Clickable along the whole name, not just at the pin. A box taken from the
    // pins alone would leave most of a long name dead to the mouse.
    CHECK (sheet.findElementAt ({ -4, 20 }) == longName);
    CHECK (sheet.findElementAt ({ -4, 0 }) != shortName);

    // On end, the tag turns with the part: what was wide is now tall.
    sheet.findElement (longName)->orientation = 1;
    const auto turned = sheet.getElementBounds (*sheet.findElement (longName));

    INFO ("turned " << turned.toString());
    CHECK (turned.getHeight() == wide.getWidth());
    CHECK (turned.getWidth() == wide.getHeight());
}

TEST_CASE ("A wire stays clickable when the sheet is zoomed out", "[editor]")
{
    using namespace SchematicModel;

    // The case that matters: a big sheet, zoomed to fit, so a grid square is a
    // handful of pixels. A tolerance measured only in grid units shrinks with
    // the zoom and the wire becomes a two-pixel target -- which reads as the
    // feature being broken rather than fiddly.
    Schematic sheet;
    sheet.addWire ({ 0, 0 }, { 120, 0 });
    sheet.addWire ({ 0, 0 }, { 0, 80 });
    const int wire = sheet.getWires().front().id;

    SchematicUI::SchematicCanvas canvas (sheet);
    canvas.setSize (600, 400);
    canvas.zoomToFit();
    canvas.setTool (SchematicUI::SchematicCanvas::Tool::Select);

    const auto centre = canvas.pixelAt ({ 60.0f, 0.0f });
    const auto pixelsPerSquare = canvas.pixelAt ({ 61.0f, 0.0f }).x - centre.x;

    // However far out the view is, a few pixels off the line still finds it.
    INFO ("pixels per grid square: " << pixelsPerSquare);
    CHECK (pixelsPerSquare < 10.0f);            // genuinely zoomed out
    CHECK (canvas.wireAt (centre) == wire);
    CHECK (canvas.wireAt (centre.translated (0.0f, 4.0f)) == wire);

    // And the click actually lands, through the real handler.
    const juce::MouseEvent event (juce::Desktop::getInstance().getMainMouseSource(),
                                  centre.translated (0.0f, 3.0f), juce::ModifierKeys(),
                                  1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  &canvas, &canvas, juce::Time::getCurrentTime(),
                                  centre, juce::Time::getCurrentTime(), 1, false);
    canvas.mouseDown (event);

    REQUIRE (canvas.getSelectedWires().size() == 1);
    CHECK (canvas.getSelectedWires().front() == wire);
}

TEST_CASE ("A box catches wires, crossing and enclosing alike", "[editor]")
{
    using namespace SchematicModel;

    Schematic sheet;
    sheet.addWire ({ 0, 0 }, { 20, 0 });     // horizontal: zero-height bounds
    sheet.addWire ({ 30, -10 }, { 30, 10 }); // vertical: zero-width bounds
    const int across = sheet.getWires()[0].id;
    const int down = sheet.getWires()[1].id;

    SchematicUI::SchematicCanvas canvas (sheet);
    canvas.setSize (600, 400);
    canvas.zoomToFit();
    canvas.setTool (SchematicUI::SchematicCanvas::Tool::Select);

    // Drags a box from one grid point to another and returns what it caught.
    auto dragBox = [&canvas] (juce::Point<float> fromGrid, juce::Point<float> toGrid)
    {
        const auto from = canvas.pixelAt (fromGrid);
        const auto to = canvas.pixelAt (toGrid);
        const auto now = juce::Time::getCurrentTime();
        const auto source = juce::Desktop::getInstance().getMainMouseSource();

        canvas.mouseDown ({ source, from, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &canvas, &canvas, now, from, now, 1, false });
        canvas.mouseDrag ({ source, to, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &canvas, &canvas, now, from, now, 1, true });
        canvas.mouseUp   ({ source, to, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &canvas, &canvas, now, from, now, 1, true });

        return canvas.getSelectedWires();
    };

    SECTION ("crossing, right to left, takes anything it touches")
    {
        // Straight through the middle of the horizontal wire, catching neither
        // of its ends -- which is the whole point of a crossing box.
        const auto caught = dragBox ({ 12.0f, 6.0f }, { 8.0f, -6.0f });

        INFO ("caught " << (int) caught.size() << " wires");
        REQUIRE (caught.size() == 1);
        CHECK (caught.front() == across);
    }

    SECTION ("crossing catches a vertical wire too")
    {
        const auto caught = dragBox ({ 34.0f, 2.0f }, { 26.0f, -2.0f });

        REQUIRE (caught.size() == 1);
        CHECK (caught.front() == down);
    }

    SECTION ("enclosing, left to right, needs the whole wire inside")
    {
        // Round the horizontal one only. The vertical wire is crossed but not
        // contained, so an enclosing box must leave it alone.
        const auto caught = dragBox ({ -4.0f, -4.0f }, { 24.0f, 4.0f });

        REQUIRE (caught.size() == 1);
        CHECK (caught.front() == across);
    }
}

TEST_CASE ("A centre-tapped transformer's strays are sized the right way up", "[schematic]")
{
    using namespace SchematicModel;

    // A push-pull output transformer is the reason this part exists, and it is
    // the one shape that inverts straysFor's assumption. The tap has to carry
    // the valve plates and this element puts the tap on the *secondary*, so the
    // plates go on the secondary and the speaker on the primary -- which leaves
    // the primary as the low-impedance winding rather than the high one.
    //
    // Sized from primary/secondary the ratio comes out at 0.05 instead of 20,
    // the magnetising inductance lands at 0.18 mH across the speaker winding,
    // and it shunts nearly the whole signal away. The failure was silent in the
    // worst way: the sheet builds, the operating point solves, and the amplifier
    // is simply 17 dB quiet.
    //
    // Its own bench rather than makeTransformerBench, which drives the primary
    // and loads the secondary -- the other way round from an output stage.
    constexpr double speakerTurns = 1.0, plateTurns = 20.0;
    constexpr double plateLoad = plateTurns * plateTurns * 8.0;   // 3.2k, matched

    auto bench = [] (int modelIndex)
    {
        Schematic sheet;
        const auto xf = sheet.addElement (ElementType::CenterTapTransformer, 0, 0);
        auto* transformer = sheet.findElement (xf);
        transformer->modelIndex = modelIndex;
        transformer->value = speakerTurns;
        transformer->valueB = plateTurns;

        // Mirrored so the tapped winding faces left, where the drive is: an
        // Input's pin points right and an Output's left, so a bench that feeds
        // the right-hand winding has to fold the source back over itself.
        transformer->mirrored = true;

        auto pin = [&sheet] (int id, int index) { return sheet.findElement (id)->getPinPosition (index); };

        const auto speakerA = pin (xf, 0), speakerB = pin (xf, 1);
        const auto plateA = pin (xf, 2), tap = pin (xf, 3), plateB = pin (xf, 4);

        // Driven across the whole tapped winding, from a source matched to it.
        const auto rs = sheet.addElement (ElementType::Resistor, plateA.x - 4, plateA.y);
        sheet.findElement (rs)->orientation = 1;
        sheet.findElement (rs)->value = plateLoad;
        sheet.addElement (ElementType::Input, plateA.x - 8, plateA.y);
        sheet.addWire (pin (rs, 0), plateA);
        sheet.addElement (ElementType::Ground, plateB.x, plateB.y + 2);

        // The tap is B+ in a real amplifier, which is an AC ground. Here it only
        // needs *some* path to ground: a node whose only company is two winding
        // rows has no conductance at all, and the factorisation is singular. A
        // megohm defines it and loads the winding with nothing.
        const auto bleeder = sheet.addElement (ElementType::Resistor, tap.x - 6, tap.y);
        sheet.findElement (bleeder)->orientation = 1;
        sheet.findElement (bleeder)->value = 1.0e6;
        sheet.addWire (tap, pin (bleeder, 0));
        sheet.addElement (ElementType::Ground, pin (bleeder, 1).x, pin (bleeder, 1).y + 2);

        // The speaker hangs off the primary, which is where an output
        // transformer puts it, and the output reads across it.
        const auto load = sheet.addElement (ElementType::Resistor, speakerA.x, speakerA.y + 2);
        sheet.findElement (load)->value = 8.0;
        sheet.addWire (pin (load, 1), speakerB);
        sheet.addElement (ElementType::Output, speakerA.x + 2, speakerA.y);
        sheet.addElement (ElementType::Ground, speakerB.x, speakerB.y + 2);

        auto result = buildCircuits (sheet, 48000.0, 1);
        INFO (result.error);
        REQUIRE (result.isValid());
        return result;
    };

    auto ideal = bench (0);
    auto real = bench (1);

    const float idealMid = gainAt (*ideal.circuits[0], 1000.0);
    const float realMid = gainAt (*real.circuits[0], 1000.0);

    INFO ("ideal " << idealMid << "   real " << realMid
                   << "   ratio " << (realMid / juce::jmax (1.0e-9f, idealMid)));

    // Real costs a little copper in the midband and no more. Sized the wrong
    // way up it costs 17 dB, so the bar here is nowhere near the noise.
    CHECK (realMid > idealMid * 0.80f);
    CHECK (realMid < idealMid * 1.02f);

    // And it must still be a transformer: bass rolls off where the magnetising
    // inductance gives up, which the inverted sizing also destroyed.
    const float realLow = gainAt (*real.circuits[0], 25.0);
    INFO ("real 25 Hz " << realLow << " against 1 kHz " << realMid);
    CHECK (realLow < realMid * 0.95f);
}

TEST_CASE ("The wheel scrolls the control strip rather than turning a knob", "[editor]")
{
    // The drawn controls live in a Viewport that scrolls sideways once there are
    // more of them than fit, so a wheel gesture over the strip is nearly always
    // someone reaching for a knob that is off-screen. Answering it by moving
    // whichever knob the pointer happened to be over changes the sound while
    // they are still looking for the one they wanted.
    //
    // Every knob, not just the scrolling ones: Input and Output sit at the ends
    // of the same band, and a rule with an exception three inches to the left is
    // one people find by changing their output gain.
    //
    // Turning this off is also what makes the strip scroll. Slider::mouseWheelMove
    // falls through to Component::mouseWheelMove when the wheel is disabled, so
    // the event carries on up to the Viewport instead of being eaten.
    runWithinPluginEditor ([] (PluginProcessor& plugin)
    {
        auto* editor = plugin.getActiveEditor();
        REQUIRE (editor != nullptr);

        int sliders = 0, live = 0;

        const std::function<void (juce::Component&)> walk = [&] (juce::Component& parent)
        {
            for (auto* child : parent.getChildren())
            {
                if (auto* slider = dynamic_cast<juce::Slider*> (child))
                {
                    ++sliders;

                    if (slider->isScrollWheelEnabled())
                        ++live;
                }

                walk (*child);
            }
        };

        walk (*editor);

        INFO ("found " << sliders << " sliders, " << live << " still answering the wheel");
        CHECK (sliders >= 2);          // Input and Output at minimum
        CHECK (live == 0);
    });
}

TEST_CASE ("Merging collinear wires does not dissolve a junction", "[schematic]")
{
    using namespace SchematicModel;

    // Connectivity is mediated by points: two wires meet only where a registered
    // point -- a pin, or some wire's end -- sits on both. Merging a collinear
    // pair deletes the ends where they met, so if nothing else registers that
    // point, everything else through it comes loose.
    //
    // A four-way node is where that showed. Four segments meeting at a point are
    // one net. Merge the horizontals and the verticals still end there, so it
    // holds. Merge the verticals too and the point is registered by nothing: a
    // long horizontal and a long vertical that cross, and a crossing is not a
    // connection. The drawing looked the same and the circuit had parted in the
    // middle.
    auto netOf = [] (const Schematic& sheet, const NetList& nets, int elementId, int pin)
    {
        for (size_t k = 0; k < sheet.getElements().size(); ++k)
            if (sheet.getElements()[k].id == elementId)
                return nets.netOfPin[k][(size_t) pin];

        return -1;
    };

    SECTION ("a four-way node stays one net")
    {
        Schematic sheet;
        sheet.addWire ({ 0, 0 }, { 4, 0 });
        sheet.addWire ({ 4, 0 }, { 8, 0 });
        sheet.addWire ({ 4, -4 }, { 4, 0 });
        sheet.addWire ({ 4, 0 }, { 4, 4 });

        // A resistor on each leg, so the nets are observable from outside.
        struct Leg { int x, y, orientation, pin; };
        const Leg legs[] = { { -2, 0, 1, 0 }, { 10, 0, 1, 1 }, { 4, -6, 0, 1 }, { 4, 6, 0, 0 } };
        std::vector<int> ids;

        for (const auto& leg : legs)
        {
            const auto id = sheet.addElement (ElementType::Resistor, leg.x, leg.y);
            sheet.findElement (id)->orientation = leg.orientation;
            sheet.findElement (id)->value = 1000.0;
            ids.push_back (id);
        }

        sheet.mergeCollinearWires();

        const auto nets = sheet.extractNets();
        const int first = netOf (sheet, nets, ids[0], legs[0].pin);

        INFO ("legs on nets "
              << netOf (sheet, nets, ids[0], legs[0].pin) << " "
              << netOf (sheet, nets, ids[1], legs[1].pin) << " "
              << netOf (sheet, nets, ids[2], legs[2].pin) << " "
              << netOf (sheet, nets, ids[3], legs[3].pin)
              << ", " << (int) sheet.getWires().size() << " wires");

        REQUIRE (first >= 0);

        for (size_t i = 1; i < ids.size(); ++i)
            CHECK (netOf (sheet, nets, ids[i], legs[i].pin) == first);
    }

    SECTION ("a plain seam still merges")
    {
        // Nothing at the join, so there is nothing to lose and two wires drawn
        // end to end are one wire.
        Schematic sheet;
        sheet.addWire ({ 0, 0 }, { 4, 0 });
        sheet.addWire ({ 4, 0 }, { 8, 0 });

        CHECK (sheet.mergeCollinearWires() == 1);
        REQUIRE (sheet.getWires().size() == 1);
        CHECK (sheet.getWires().front().contains ({ 4, 0 }));
    }

    SECTION ("a wire running through the seam blocks the merge")
    {
        // The general form of the four-way. The vertical passes through (4,0)
        // without ending there, so it is registered by nothing once the bar's
        // two halves stop meeting -- merge and the vertical is stranded.
        Schematic sheet;
        sheet.addWire ({ 0, 0 }, { 4, 0 });
        sheet.addWire ({ 4, 0 }, { 8, 0 });
        sheet.addWire ({ 4, -4 }, { 4, 4 });

        CHECK (sheet.mergeCollinearWires() == 0);
        CHECK (sheet.getWires().size() == 3);
    }

    SECTION ("a pin on the seam does not block it")
    {
        // A pin registers the point by itself, so the bar can become one wire
        // and the part still meets it. Merging is only unsafe when the seam is
        // the last thing holding the point in existence.
        Schematic sheet;
        sheet.addWire ({ 0, 0 }, { 4, 0 });
        sheet.addWire ({ 4, 0 }, { 8, 0 });

        const auto id = sheet.addElement (ElementType::Resistor, 4, 2);
        sheet.findElement (id)->value = 1000.0;
        REQUIRE (sheet.findElement (id)->getPinPosition (0) == juce::Point<int> { 4, 0 });

        CHECK (sheet.mergeCollinearWires() == 1);
        REQUIRE (sheet.getWires().size() == 1);

        // And it is still attached to the wire it was attached to.
        const auto nets = sheet.extractNets();
        CHECK (netOf (sheet, nets, id, 0) >= 0);
    }
}

TEST_CASE ("A wire's hit target is the same size however far you are zoomed in", "[editor]")
{
    using namespace SchematicModel;

    // A hit target is a thing you aim at with a pointer, and the pointer does
    // not zoom. Measured in grid squares this was a third of a square, which is
    // seven pixels at one zoom and twenty at another -- so zoomed in a wire wore
    // a halo three times its own width, and clicking beside one to drop the
    // selection kept finding the wire instead.
    //
    // Measured rather than asserted against the constant, so the test fails if
    // the *behaviour* changes rather than only if someone edits the number.
    auto reachAt = [] (int extent)
    {
        Schematic sheet;
        sheet.addWire ({ 0, 0 }, { extent, 0 });
        const int wire = sheet.getWires().front().id;

        SchematicUI::SchematicCanvas canvas (sheet);
        canvas.setSize (900, 600);
        canvas.setTool (SchematicUI::SchematicCanvas::Tool::Select);
        canvas.zoomToFit();

        const auto half = static_cast<float> (extent) * 0.5f;
        const auto centre = canvas.pixelAt ({ half, 0.0f });
        const auto pixelsPerSquare = canvas.pixelAt ({ half + 1.0f, 0.0f }).x - centre.x;

        float reach = 0.0f;

        for (float d = 0.0f; d < 80.0f; d += 0.25f)
            if (canvas.wireAt (centre.translated (0.0f, d)) == wire)
                reach = d;

        return std::pair { pixelsPerSquare, reach };
    };

    const auto [zoomedInSquare, zoomedInReach] = reachAt (4);
    const auto [zoomedOutSquare, zoomedOutReach] = reachAt (160);

    INFO ("zoomed in: " << zoomedInSquare << " px/square, reach " << zoomedInReach
          << " | zoomed out: " << zoomedOutSquare << " px/square, reach " << zoomedOutReach);

    // Genuinely different zooms, or the comparison below says nothing.
    REQUIRE (zoomedInSquare > zoomedOutSquare * 4.0f);

    // The target stays put on screen. It used to be 13.5 px against 7.
    CHECK (std::abs (zoomedInReach - zoomedOutReach) <= 1.0f);

    // And it is a target you can both hit and get away from: wide enough to
    // click, narrow enough that clicking beside the wire misses it.
    CHECK (zoomedInReach >= 5.0f);
    CHECK (zoomedInReach <= 9.0f);
}

TEST_CASE ("A selected wire's handles do not guard the space around it", "[editor]")
{
    using namespace SchematicModel;

    // Clicking beside a selected wire is how you drop the selection, and the
    // drag handles at its ends used to make that most of a grid square's work.
    // They were hit-tested a whole grid square either side, on the *snapped*
    // point, against a diamond drawn at a third of a square -- so the area that
    // answered a click was several times the thing you could see, and the
    // diagonal was worse still because the test was a square box while the
    // handle is a diamond. Clicking near a selected wire grabbed a resize
    // handle instead of deselecting, with nothing on screen to say why.
    auto stillSelectedAfterClickAt = [] (float dx, float dy)
    {
        Schematic sheet;
        sheet.addWire ({ 0, 0 }, { 10, 0 });
        const int id = sheet.getWires().front().id;

        SchematicUI::SchematicCanvas canvas (sheet);
        canvas.setSize (900, 600);
        canvas.setTool (SchematicUI::SchematicCanvas::Tool::Select);
        canvas.zoomToFit();

        const auto end = canvas.pixelAt ({ 10.0f, 0.0f });
        const auto pixelsPerSquare = canvas.pixelAt ({ 11.0f, 0.0f }).x - end.x;

        float stuck = 0.0f;

        for (float d = 0.0f; d < 120.0f; d += 0.5f)
        {
            canvas.setSelectedWires ({ id });

            const auto at = end.translated (dx * d, dy * d);
            const juce::MouseEvent event (juce::Desktop::getInstance().getMainMouseSource(),
                                          at, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                          &canvas, &canvas, juce::Time::getCurrentTime(),
                                          at, juce::Time::getCurrentTime(), 1, false);
            canvas.mouseDown (event);
            canvas.mouseUp (event);

            if (! canvas.getSelectedWires().empty())
                stuck = d;
        }

        return std::pair { stuck, pixelsPerSquare };
    };

    const auto [along, square] = stillSelectedAfterClickAt (1.0f, 0.0f);
    const auto [beside, unused1] = stillSelectedAfterClickAt (0.0f, 1.0f);
    const auto [diagonal, unused2] = stillSelectedAfterClickAt (0.7071f, 0.7071f);
    juce::ignoreUnused (unused1, unused2);

    INFO ("grid " << square << " px -- along " << along << ", beside " << beside
          << ", diagonal " << diagonal);

    // Comfortably inside a grid square in every direction. It used to be 1.5
    // squares along the axes and 2.1 diagonally.
    CHECK (along < square * 0.5f);
    CHECK (beside < square * 0.5f);
    CHECK (diagonal < square * 0.5f);

    // The handle is a diamond, so going out at 45 degrees leaves it *sooner*
    // than going along an axis. A square hit box gets this backwards, and that
    // corner was the worst of the old behaviour.
    CHECK (diagonal <= along);
}

TEST_CASE ("Undo restores the drawing and leaves the knobs alone", "[editor]")
{
    // A snapshot is the whole sheet, so it carries every pot's position whether
    // undo wants it or not. Restoring one used to put those back into the
    // drawing while the parameters -- which are the live truth, driving the
    // audio and attached to the strip's sliders -- carried on unchanged. The
    // wiper drawn on the sheet moved, nothing else did, and the two disagreed
    // until the next rebuild quietly took the parameter's side.
    runWithinPluginEditor ([] (PluginProcessor& plugin)
    {
        auto* editor = plugin.getActiveEditor();
        REQUIRE (editor != nullptr);

        // The canvas, for the two callbacks the editor hangs undo off.
        SchematicUI::SchematicCanvas* canvas = nullptr;

        const std::function<void (juce::Component&)> walk = [&] (juce::Component& parent)
        {
            for (auto* child : parent.getChildren())
            {
                if (auto* found = dynamic_cast<SchematicUI::SchematicCanvas*> (child))
                    canvas = found;

                walk (*child);
            }
        };

        walk (*editor);
        REQUIRE (canvas != nullptr);
        REQUIRE (canvas->onSchematicChanged != nullptr);
        REQUIRE (canvas->onUndoRequested != nullptr);

        auto& sheet = plugin.getSchematic();

        // The first drawn control on the sheet, and the parameter working it.
        int controlId = -1;

        for (const auto& element : sheet.getElements())
            if (element.type == SchematicModel::ElementType::Potentiometer)
            {
                controlId = element.id;
                break;
            }

        REQUIRE (controlId >= 0);

        auto* knob = plugin.apvts.getRawParameterValue (PluginProcessor::getControlParameterId (0));
        REQUIRE (knob != nullptr);

        // Somewhere unmistakable, and agreed between the two.
        plugin.apvts.getParameter (PluginProcessor::getControlParameterId (0))->setValueNotifyingHost (0.25f);
        plugin.adoptControlPositions();

        // An edit worth undoing, recorded the way the canvas records one.
        const auto moved = sheet.addElement (SchematicModel::ElementType::Resistor, 40, 40);
        sheet.findElement (moved)->value = 1000.0;
        canvas->onSchematicChanged();

        // Now the knob is turned, as a player would turn it: the parameter
        // moves and the drawing follows, with no history step for either.
        plugin.apvts.getParameter (PluginProcessor::getControlParameterId (0))->setValueNotifyingHost (0.80f);
        plugin.adoptControlPositions();

        canvas->onUndoRequested();

        const auto* control = sheet.findElement (controlId);
        REQUIRE (control != nullptr);

        INFO ("parameter " << knob->load() << ", drawing " << control->getControlPosition());

        // The drawing came back...
        CHECK (sheet.findElement (moved) == nullptr);

        // ...and the knob did not move with it.
        CHECK (knob->load() == Catch::Approx (0.80f).margin (0.001));
        CHECK (control->getControlPosition() == Catch::Approx (knob->load()).margin (0.001));
    });
}

//==============================================================================
TEST_CASE ("A sheet with duplicated or missing ids loads with every part reachable", "[schematic]")
{
    using namespace SchematicModel;

    // Every lookup in the codebase -- findElement, findWire, and through them
    // selection, the inspector and the ganged write-back -- returns the *first*
    // match. So a file carrying two parts on one id leaves the second drawn and
    // audible but impossible to select, edit or delete, and a part carrying id 0
    // collides with the "nothing inspected" sentinel as well. Neither is
    // reachable by drawing; both are reachable by hand-editing a .celsch or by
    // truncating one, which is exactly when you least want a silent failure.
    Schematic sheet;
    const int r1 = sheet.addElement (ElementType::Resistor, 0, 0);
    const int r2 = sheet.addElement (ElementType::Resistor, 8, 0);
    const int r3 = sheet.addElement (ElementType::Capacitor, 16, 0);
    sheet.addWire ({ 0, 8 }, { 8, 8 });
    sheet.addWire ({ 8, 8 }, { 16, 8 });

    REQUIRE (r1 != r2);
    REQUIRE (r2 != r3);

    auto tree = sheet.toValueTree();

    // Corrupt it the two ways a damaged file does: a collision, and a missing
    // id -- getProperty's default for which is 0.
    int elementsSeen = 0, wiresSeen = 0;

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto node = tree.getChild (i);

        if (node.hasType ("ELEMENT"))
        {
            if (elementsSeen == 1)
                node.setProperty ("id", r1, nullptr); // duplicate of the first
            else if (elementsSeen == 2)
                node.removeProperty ("id", nullptr);  // reads back as 0

            ++elementsSeen;
        }
        else if (node.hasType ("WIRE"))
        {
            if (wiresSeen == 1)
                node.setProperty ("id", tree.getChild (i - 1).getProperty ("id"), nullptr);

            ++wiresSeen;
        }
    }

    REQUIRE (elementsSeen == 3);
    REQUIRE (wiresSeen == 2);

    Schematic reloaded;
    reloaded.restoreFromValueTree (tree);

    // Nothing dropped.
    REQUIRE (reloaded.getElements().size() == 3);
    REQUIRE (reloaded.getWires().size() == 2);

    const auto idsAreSoundAndReachable = [] (const auto& items, const auto& lookup)
    {
        std::set<int> seen;

        for (const auto& item : items)
        {
            CHECK (item.id > 0);              // 0 is the "nothing selected" sentinel
            CHECK (seen.insert (item.id).second); // and no two share one
            CHECK (lookup (item.id) == &item);    // so each is the one found by its id
        }
    };

    idsAreSoundAndReachable (reloaded.getElements(),
                             [&] (int id) { return reloaded.findElement (id); });
    idsAreSoundAndReachable (reloaded.getWires(),
                             [&] (int id) { return reloaded.findWire (id); });

    // A part placed afterwards must not collide with anything healed above --
    // the repair has to hand the counter back, not just fix what it saw.
    const int fresh = reloaded.addElement (ElementType::Resistor, 24, 0);

    for (const auto& element : reloaded.getElements())
        if (&element != reloaded.findElement (fresh))
            CHECK (element.id != fresh);
}

//==============================================================================
TEST_CASE ("A healthy sheet keeps its ids through a save and load", "[schematic]")
{
    using namespace SchematicModel;

    // The repair above must be a repair, not a renumbering: ids that are already
    // unique and positive are what selection and the inspector are holding when
    // a document is restored under them, so a round trip has to leave them be.
    Schematic sheet;
    const int a = sheet.addElement (ElementType::Resistor, 0, 0);
    const int b = sheet.addElement (ElementType::Capacitor, 8, 0);
    sheet.addWire ({ 0, 8 }, { 8, 8 });

    REQUIRE (sheet.getWires().size() == 1);
    const int wire = sheet.getWires()[0].id;

    Schematic reloaded;
    reloaded.restoreFromValueTree (sheet.toValueTree());

    REQUIRE (reloaded.getElements().size() == 2);
    CHECK (reloaded.getElements()[0].id == a);
    CHECK (reloaded.getElements()[1].id == b);
    REQUIRE (reloaded.getWires().size() == 1);
    CHECK (reloaded.getWires()[0].id == wire);
}

TEST_CASE ("A document records the build that wrote it", "[plugin]")
{
    PluginProcessor plugin;

    const auto document = plugin.createDocument();
    const auto saved = document.getProperty (PluginProcessor::documentVersionProperty).toString();

    INFO ("saved version: " << saved);
    CHECK (saved.isNotEmpty());
    CHECK (saved == PluginProcessor::getBuildVersion());

    // Always present, unlike the build options, which are omitted when they hold
    // their default so an unchanged sheet stays byte-identical.
    CHECK (document.hasProperty (PluginProcessor::documentVersionProperty));
}

TEST_CASE ("Versions compare as numbers, not as text", "[plugin]")
{
    const auto cmp = [] (const char* a, const char* b)
    { return PluginProcessor::compareVersions (a, b); };

    CHECK (cmp ("1.0.0", "1.0.0") == 0);
    CHECK (cmp ("1.0", "1.0.0") == 0);        // absent components read as zero
    CHECK (cmp ("0.9.79", "0.9.80") < 0);
    CHECK (cmp ("0.9.80", "0.9.79") > 0);

    // The case a string compare gets backwards, and the reason this exists:
    // "0.10.0" sorts before "0.9.79" as text and after it as a version.
    CHECK (cmp ("0.10.0", "0.9.79") > 0);
    CHECK (cmp ("0.9.79", "0.10.0") < 0);

    CHECK (cmp ("2.0.0", "1.99.99") > 0);
    CHECK (cmp ("1.2.3-rc1", "1.2.3") == 0);  // a suffix is not a version bump
    CHECK (cmp ("", "0.0.0") == 0);           // nothing is not newer than anything
}

TEST_CASE ("A sheet from a newer build is flagged at import", "[plugin]")
{
    // Answered by the import, not by the build that follows it. The editor
    // raises it from schematicChangedExternally(), which is the one point every
    // route in passes through, so the flag is what has to be right.
    PluginProcessor plugin;

    auto document = plugin.createDocument();
    document.setProperty (PluginProcessor::documentVersionProperty, "99.0.0", nullptr);

    PluginProcessor loaded;
    CHECK (! loaded.wasDocumentFromNewerBuild());     // nothing restored yet

    REQUIRE (loaded.restoreDocument (document));
    CHECK (loaded.wasDocumentFromNewerBuild());

    // And it describes the current document only -- restoring something older
    // afterwards must clear it, or the warning outlives the file it was about.
    auto ordinary = plugin.createDocument();
    REQUIRE (loaded.restoreDocument (ordinary));
    CHECK (! loaded.wasDocumentFromNewerBuild());
}

TEST_CASE ("An older or unversioned sheet is not flagged", "[plugin]")
{
    // A sheet with no version predates the field, so it is older by definition,
    // and an older sheet loads correctly by construction. Warning about it would
    // be crying wolf on every file anyone already has.
    PluginProcessor plugin;

    for (const auto& version : { juce::String ("0.0.1"), juce::String() })
    {
        auto document = plugin.createDocument();

        if (version.isNotEmpty())
            document.setProperty (PluginProcessor::documentVersionProperty, version, nullptr);
        else
            document.removeProperty (PluginProcessor::documentVersionProperty, nullptr);

        PluginProcessor loaded;
        REQUIRE (loaded.restoreDocument (document));

        INFO ((version.isNotEmpty() ? version : juce::String ("no version")));
        CHECK (! loaded.wasDocumentFromNewerBuild());
    }
}

// The version notice no longer travels with the build, so a newer sheet must
// leave the console alone -- a missing model still reports there, and mixing the
// two would put a fact about the file among the facts about the circuit.
TEST_CASE ("The version notice stays out of the build's notes", "[plugin]")
{
    PluginProcessor plugin;

    auto document = plugin.createDocument();
    document.setProperty (PluginProcessor::documentVersionProperty, "99.0.0", nullptr);

    PluginProcessor loaded;
    REQUIRE (loaded.restoreDocument (document));

    for (const auto& d : loaded.rebuild().diagnostics)
    {
        INFO (d.text);
        CHECK (! d.text.contains ("newer version"));
    }
}

TEST_CASE ("Load warnings survive restoreDocument's own rebuild", "[plugin]")
{
    // restoreDocument() rebuilds before it returns, and returns only a bool --
    // so the BuildResult that rebuild drains the warnings into is thrown away.
    // Anything the load noticed would be consumed there and never reach the
    // console, which is fed by the editor's rebuild a moment later.
    //
    // Checked with a missing model rather than a version, because that path
    // predates the version field and had the same hole.
    PluginProcessor plugin;

    auto document = plugin.createDocument();
    auto drawing = document.getChildWithName ("SCHEMATIC");

    juce::ValueTree node ("ELEMENT");
    node.setProperty ("id", 4242, nullptr);
    node.setProperty ("type", static_cast<int> (SchematicModel::ElementType::Triode), nullptr);
    node.setProperty ("x", 40, nullptr);
    node.setProperty ("y", 20, nullptr);
    node.setProperty ("modelId", "lucas:not-in-this-build:7f3a", nullptr);
    drawing.appendChild (node, nullptr);

    PluginProcessor loaded;
    REQUIRE (loaded.restoreDocument (document));

    bool reported = false;

    for (const auto& d : loaded.rebuild().diagnostics)
        if (d.text.contains ("lucas:not-in-this-build:7f3a"))
            reported = true;

    CHECK (reported);

    // Still said once, not on every Rebuild afterwards.
    bool again = false;

    for (const auto& d : loaded.rebuild().diagnostics)
        if (d.text.contains ("lucas:not-in-this-build:7f3a"))
            again = true;

    CHECK (! again);
}


TEST_CASE ("A tube added in a newer build degrades safely", "[plugin][schematic]")
{
    using namespace SchematicModel;

    // The realistic version-skew case: a sheet drawn with a valve added after
    // this build shipped. It still builds as something real rather than
    // refusing the sheet, it names the model that is missing, and -- the part
    // that matters most -- the reference survives the next save, so updating
    // restores the valve instead of finding the sheet has forgotten it.
    Schematic sheet;
    sheet.addElement (ElementType::Triode, 4, 2);

    auto tree = sheet.toValueTree();
    tree.getChild (0).setProperty ("modelId", "celine:triode-12ax7-mullard", nullptr);

    Schematic opened;
    opened.restoreFromValueTree (tree);

    REQUIRE (opened.getElements().size() == 1);
    CHECK (opened.getElements()[0].modelIndex == 0);

    const auto warnings = opened.getLoadWarnings();
    REQUIRE (warnings.size() == 1);
    INFO (warnings[0]);
    CHECK (warnings[0] == "\"celine:triode-12ax7-mullard\" model not found in this version.");

    CHECK (opened.getElements()[0].unresolvedModelId == "celine:triode-12ax7-mullard");
    CHECK (opened.toValueTree().getChild (0).getProperty ("modelId").toString()
             == "celine:triode-12ax7-mullard");
}
