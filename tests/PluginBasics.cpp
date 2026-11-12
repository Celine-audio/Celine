#include "helpers/test_helpers.h"
#include <PluginEditor.h>
#include <PluginProcessor.h>
#include <CelineEngine/Components/FastMath.h>
#include <Schematic/ExampleSchematics.h>
#include <ui/ControlStrip.h>
#include <ui/EditorPanels.h>
#include <ui/SchematicCanvas.h>
#include <CelineUI/Fonts.h>
#include <CelineUI/Theme.h>
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
        // Match the stable part of PRODUCT_NAME.
        CHECK_THAT (testPlugin.getName().toStdString(),
                    Catch::Matchers::ContainsSubstring (std::string ("Celine")));

        // ASCII only: the name becomes the VST3 bundle directory and class-info string, which
        // JUCE widens byte by byte (accents become garbage in Windows hosts).
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

    // Input, Output, Bypass and Channels always exist. The drawn controls map onto a fixed
    // pool of knob parameters, declared at construction.
    for (auto* expected : { "input", "output", "bypass", "channels" })
        CHECK (ids.contains (expected));

    for (int i = 0; i < PluginProcessor::maxLiveControls; ++i)
        CHECK (ids.contains (PluginProcessor::getControlParameterId (i)));

    CHECK (ids.size() == 4 + PluginProcessor::maxLiveControls);

    // The host gets a real bypass parameter.
    CHECK (plugin.getBypassParameter() != nullptr);
}

namespace
{
    /** Every message a build produced, as one string. */
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

    // Warm up engaged, then bypass. The circuit keeps running while bypassed, so this
    // checks that the output is the untouched input.
    setParameter (plugin, "bypass", 0.0f);
    for (int block = 0; block < 20; ++block) { fillSine (buffer, phase, 440.0, 0.5f); plugin.processBlock (buffer, midi); }

    setParameter (plugin, "bypass", 1.0f);

    // Bypass crossfades over 30 ms; four blocks is 43 ms.
    for (int block = 0; block < 4; ++block) { fillSine (buffer, phase, 440.0, 0.5f); plugin.processBlock (buffer, midi); }

    juce::AudioBuffer<float> expected (2, 512);
    for (int block = 0; block < 5; ++block)
    {
        fillSine (buffer, phase, 440.0, 0.5f);
        expected.makeCopyOf (buffer);
        plugin.processBlock (buffer, midi);

        // Bit-exact. Written as a magnitude to keep -Wfloat-equal quiet.
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

    // The default sheet is the diode clipper, whose one pot is Volume.
    const auto controls = plugin.getLiveControls();
    REQUIRE (controls.size() == 1);
    CHECK (controls[0].name == "Volume");

    // The knob is live: it moves the output with no rebuild.
    setParameter (plugin, PluginProcessor::getControlParameterId (0), 1.0f);
    const float wideOpen = measurePeak (plugin, 0.5f);

    setParameter (plugin, PluginProcessor::getControlParameterId (0), 0.1f);
    const float turnedDown = measurePeak (plugin, 0.5f);

    INFO ("volume wide open " << wideOpen << ", turned down " << turnedDown);
    CHECK (wideOpen > turnedDown * 2.0f);
    CHECK (turnedDown > 0.0f);

    // And it clips: doubling the input doesn't double the output.
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

    // Driven well past the silicon knee, so the diodes are clipping.
    const float before = measurePeak (plugin, 3.0f);

    // Swap the diodes for blue LEDs, which clip much later, and rebuild.
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

    // Blue LEDs need about 3 V, so far more signal gets through.
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

    // Different tones per side.
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

    // Stereo runs a circuit per side: two inputs, two outputs.
    const auto [stereoLeft, stereoRight] = run (0);
    CHECK (std::abs (stereoLeft - stereoRight) > 1.0e-6f);

    // Mono modes run one circuit and copy it: identical channels.
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

    // A .celsch carries no window size.
    const auto preset = plugin.createDocument();
    CHECK (! preset.hasProperty ("editorWidth"));

    juce::MemoryBlock state;
    plugin.getStateInformation (state);

    PluginProcessor reopened;
    reopened.setStateInformation (state.getData(), static_cast<int> (state.getSize()));

    CHECK (reopened.editorWidth.load() == 1480);
    CHECK (reopened.editorHeight.load() == 900);
}


TEST_CASE ("The window is never smaller than 1024 x 689", "[plugin]")
{
    runWithinPluginEditor ([] (PluginProcessor& plugin) {
        auto* limits = plugin.getActiveEditor()->getConstrainer();
        REQUIRE (limits != nullptr);
        CHECK (limits->getMinimumWidth() == 1024);
        CHECK (limits->getMinimumHeight() == 689);
    });

    // A size below the current minimum opens at the minimum.
    PluginProcessor plugin;
    plugin.editorWidth = 900;
    plugin.editorHeight = 500;

    std::unique_ptr<juce::AudioProcessorEditor> editor (plugin.createEditor());
    CHECK (editor->getWidth() == 1024);
    CHECK (editor->getHeight() == 689);
}

TEST_CASE ("The smallest window shows the whole palette", "[plugin][ui]")
{
    // The minimum height fits everything without scrolling.
    runWithinPluginEditor ([] (PluginProcessor& plugin) {
        auto* editor = plugin.getActiveEditor();
        auto* limits = editor->getConstrainer();
        REQUIRE (limits != nullptr);

        Celine::ElementPalette* palette = nullptr;

        for (auto* child : editor->getChildren())
            if (auto* p = dynamic_cast<Celine::ElementPalette*> (child))
                palette = p;

        REQUIRE (palette != nullptr);

        editor->setSize (limits->getMinimumWidth(), limits->getMinimumHeight());
        CHECK (! palette->isScrolling());

        // And it is the floor: a pixel shorter scrolls.
        editor->setSize (limits->getMinimumWidth(), limits->getMinimumHeight() - 1);
        CHECK (palette->isScrolling());
    });
}

TEST_CASE ("Resizing the editor gives every extra pixel to the canvas", "[plugin]")
{
    runWithinPluginEditor ([] (PluginProcessor& plugin) {
        auto* editor = plugin.getActiveEditor();
        REQUIRE (editor->isResizable());

        // The sheet runs edge to edge below the toolbar; the band floats over it.
        auto canvasBounds = [] (int width, int height)
        {
            // Read from the theme, so the test follows the design.
            constexpr int toolbar = Celine::Theme::toolbarHeight;

            return juce::Rectangle<int> { 0, toolbar, width, height - toolbar };
        };

        auto hasChildAt = [editor] (juce::Rectangle<int> bounds)
        {
            for (auto* child : editor->getChildren())
                if (child->getBounds() == bounds)
                    return true;

            return false;
        };

        for (auto size : { juce::Point<int> { 1100, 700 }, juce::Point<int> { 1400, 900 } })
        {
            editor->setSize (size.x, size.y);
            INFO ("at " << size.x << "x" << size.y);
            CHECK (hasChildAt (canvasBounds (size.x, size.y)));
        }

        // The size survives the editor being destroyed and rebuilt.
        CHECK (plugin.editorWidth.load() == 1400);
        CHECK (plugin.editorHeight.load() == 900);
    });
}

namespace
{
    /** The palette's list and fold tab. */
    struct PaletteParts
    {
        juce::Viewport* viewport = nullptr;
        juce::Component* list = nullptr;
        juce::Button* tab = nullptr;

        explicit PaletteParts (Celine::ElementPalette& palette)
        {
            for (auto* child : palette.getChildren())
            {
                if (auto* v = dynamic_cast<juce::Viewport*> (child)) viewport = v;
                else if (auto* b = dynamic_cast<juce::Button*> (child)) tab = b;
            }

            if (viewport != nullptr)
                list = viewport->getViewedComponent();
        }

        juce::Component* entryNamed (const juce::String& name) const
        {
            for (auto* entry : list->getChildren())
                if (entry->getName() == name)
                    return entry;

            return nullptr;
        }
    };
}

TEST_CASE ("The palette is two to a row, with every name fitted to its column", "[plugin][ui]")
{
    Celine::ElementPalette palette;
    const PaletteParts parts (palette);
    REQUIRE (parts.list != nullptr);
    REQUIRE (parts.tab != nullptr);

    const auto size = palette.getPreferredSize();
    palette.setSize (size.x, size.y);
    CHECK (! palette.isScrolling());

    constexpr int column = Celine::Theme::paletteColumnWidth;

    // One size for every name: the full size (the column fits MOSFE Transistor at it).
    const float fontHeight = palette.getNameFontHeight();
    CHECK (juce::approximatelyEqual (fontHeight, Celine::Theme::paletteFontHeight));

    const auto font = Celine::Fonts::light (fontHeight);
    const float room = static_cast<float> (column - (Celine::Theme::paletteRowHeight - 4) - 10);

    // Every entry is one column wide, two to a row.
    const int count = parts.list->getNumChildComponents();

    for (int i = 0; i < count; ++i)
    {
        auto* entry = parts.list->getChildComponent (i);
        INFO (entry->getName());
        CHECK (entry->getWidth() == column);
        CHECK (juce::GlyphArrangement::getStringWidth (font, entry->getName()) <= room);

        if (i % 2 == 1)
        {
            auto* partner = parts.list->getChildComponent (i - 1);
            CHECK (partner->getY() == entry->getY());
            CHECK (partner->getRight() < entry->getX());
        }
    }

    for (auto pair : { std::pair { "Transformer", "Transformer (CT)" }, std::pair { "MOSFE Transistor", "Scope" },
                       std::pair { "BJ Transistor", "JFE Transistor" } })
    {
        INFO (pair.first << " | " << pair.second);
        REQUIRE (parts.entryNamed (pair.first) != nullptr);
        REQUIRE (parts.entryNamed (pair.second) != nullptr);
        CHECK (parts.entryNamed (pair.first)->getY() == parts.entryNamed (pair.second)->getY());
    }

    // Nothing overlaps and nothing leaves the list.
    for (int i = 0; i < parts.list->getNumChildComponents(); ++i)
    {
        const auto a = parts.list->getChildComponent (i)->getBounds();
        INFO ("entry " << i << " at " << a.toString());
        CHECK (parts.list->getLocalBounds().contains (a));

        for (int j = i + 1; j < parts.list->getNumChildComponents(); ++j)
            CHECK (! a.intersects (parts.list->getChildComponent (j)->getBounds()));
    }

    // The tab stands off the right edge; the corners beside it belong to the sheet.
    const auto body = palette.getBodyBounds();
    CHECK (parts.tab->getX() == body.getRight());
    CHECK (palette.getLocalBounds().contains (parts.tab->getBounds()));
    CHECK (palette.hitTest (parts.tab->getBounds().getCentreX(), parts.tab->getBounds().getCentreY()));
    CHECK (! palette.hitTest (parts.tab->getBounds().getCentreX(), 1));
    CHECK (palette.hitTest (body.getCentreX(), body.getCentreY()));
}

TEST_CASE ("The palette folds to symbols when asked, and only then", "[plugin][ui]")
{
    Celine::ElementPalette palette;
    const PaletteParts parts (palette);
    REQUIRE (parts.tab != nullptr);

    const auto unfolded = palette.getPreferredSize();

    // However small, it scrolls rather than folding on its own.
    palette.setSize (unfolded.x, 150);
    CHECK (! palette.isCollapsed());
    CHECK (palette.isScrolling());
    CHECK (parts.entryNamed ("Resistor")->getWidth() == Celine::Theme::paletteColumnWidth);

    int announcements = 0;
    palette.onCollapsedChanged = [&announcements] { ++announcements; };

    // The tab folds it, says so, and every entry becomes its symbol's square.
    parts.tab->onClick();
    CHECK (palette.isCollapsed());
    CHECK (announcements == 1);

    const auto folded = palette.getPreferredSize();
    palette.setSize (folded.x, folded.y);
    CHECK (folded.x < unfolded.x / 2);

    // The same rows either way.
    CHECK (folded.y == unfolded.y);

    for (auto* entry : parts.list->getChildren())
        CHECK (entry->getWidth() == entry->getHeight());

    // And back.
    parts.tab->onClick();
    CHECK (! palette.isCollapsed());
    CHECK (announcements == 2);
    CHECK (palette.getPreferredSize() == unfolded);
}

TEST_CASE ("A palette taller than its room scrolls rather than growing", "[plugin][ui]")
{
    Celine::ElementPalette palette;
    const PaletteParts parts (palette);
    REQUIRE (parts.viewport != nullptr);

    const auto natural = palette.getPreferredSize();
    const auto size = palette.getPreferredSize (natural.y / 2);
    palette.setSize (size.x, size.y);

    CHECK (size.y == natural.y / 2);
    CHECK (palette.isScrolling());
    CHECK (parts.viewport->getVerticalScrollBar().isVisible());

    // The scrollbar is beside the rows.
    CHECK (parts.list->getWidth() <= parts.viewport->getMaximumVisibleWidth());
}

TEST_CASE ("The floating panels sit on the sheet and clear of one another", "[plugin][ui]")
{
    runWithinPluginEditor ([] (PluginProcessor& plugin) {
        auto* editor = plugin.getActiveEditor();

        Celine::SchematicCanvas* canvas = nullptr;
        Celine::ElementPalette* palette = nullptr;
        Celine::ElementInspector* inspector = nullptr;
        Celine::MessageConsole* console = nullptr;

        for (auto* child : editor->getChildren())
        {
            if (auto* c = dynamic_cast<Celine::SchematicCanvas*> (child)) canvas = c;
            if (auto* p = dynamic_cast<Celine::ElementPalette*> (child)) palette = p;
            if (auto* i = dynamic_cast<Celine::ElementInspector*> (child)) inspector = i;
            if (auto* m = dynamic_cast<Celine::MessageConsole*> (child)) console = m;
        }

        REQUIRE (canvas != nullptr);
        REQUIRE (palette != nullptr);
        REQUIRE (inspector != nullptr);
        REQUIRE (console != nullptr);

        // In front of the sheet, or the sheet takes their clicks.
        auto index = [editor] (juce::Component* c) { return editor->getIndexOfChildComponent (c); };
        CHECK (index (palette) > index (canvas));
        CHECK (index (inspector) > index (canvas));
        CHECK (index (console) > index (canvas));

        // Something to show, so the console has a size.
        console->setMessages ("Built 15 parts", false, {});

        // From the minimum window size up to a large one, through the sizes where the
        // arrangement changes.
        auto* limits = editor->getConstrainer();
        REQUIRE (limits != nullptr);
        const int minWidth = limits->getMinimumWidth();
        const int minHeight = limits->getMinimumHeight();

        std::vector<juce::Point<int>> sizes { { minWidth, minHeight } };

        for (int width = minWidth; width <= 2000; width += 97)
            for (int height = minHeight; height <= 1200; height += 83)
                sizes.push_back ({ width, height });

        SchematicModel::Element resistor;
        resistor.id = 1;
        resistor.type = SchematicModel::ElementType::Resistor;

        for (const bool folded : { false, true })
        for (auto* selected : { (SchematicModel::Element*) nullptr, &resistor })
        {
            palette->setCollapsed (folded);
            inspector->setElement (selected);

            for (auto size : sizes)
            {
                editor->setSize (size.x, size.y);
                editor->resized();
                INFO ((folded ? "folded, " : "") << (selected ? "resistor" : "nothing") << " selected, at " << size.x << "x" << size.y
                          << ": sheet " << canvas->getBounds().toString()
                          << ", palette " << palette->getBounds().toString()
                          << ", inspector " << inspector->getBounds().toString()
                          << ", console " << console->getBounds().toString());

                // On the sheet, clear of its rulers, the toolbar and the band.
                const auto open = canvas->getBounds()
                                      .withTrimmedTop (Celine::SchematicCanvas::topRulerThickness)
                                      .withTrimmedLeft (Celine::SchematicCanvas::leftRulerThickness);

                for (auto* panel : { (juce::Component*) palette, (juce::Component*) inspector, (juce::Component*) console })
                {
                    CHECK (! panel->getBounds().isEmpty());
                    CHECK (open.contains (panel->getBounds()));
                }

                // Palette left, inspector right.
                CHECK (palette->getX() < inspector->getX());

                // The panels stay a margin above the band and are centred vertically in what is left.
                const int margin = Celine::Theme::floatingMargin;
                const int bandTop = canvas->getBottom() - margin - Celine::ControlStrip::preferredHeight;

                for (auto* panel : { (juce::Component*) palette, (juce::Component*) inspector, (juce::Component*) console })
                    CHECK (panel->getBottom() <= bandTop - margin);

                const int above = palette->getY() - open.getY() - margin;
                const int below = bandTop - margin - palette->getBottom();
                CHECK (std::abs (above - below) <= 1);

                const int inspectorAbove = inspector->getY() - open.getY() - margin;
                const int inspectorBelow = bandTop - margin - inspector->getBottom();
                CHECK (std::abs (inspectorAbove - inspectorBelow) <= 1);

                if (palette->isScrolling())
                    CHECK (above == 0);

                CHECK (console->getX() >= palette->getRight() + margin);

                CHECK (! palette->getBounds().intersects (inspector->getBounds()));
                CHECK (! palette->getBounds().intersects (console->getBounds()));
                CHECK (! inspector->getBounds().intersects (console->getBounds()));

                // The console always has its line.
                CHECK (console->getHeight() >= 40);
            }
        }

        inspector->setElement (nullptr);
        palette->setCollapsed (false);
    });
}

TEST_CASE ("The inspector card is the size of what it shows", "[plugin][ui]")
{
    Celine::ElementInspector inspector;

    int announcements = 0;
    inspector.onPreferredSizeChanged = [&announcements] { ++announcements; };

    const auto nothing = inspector.getPreferredSize();

    // A note is a heading and one text field; the inspector announces its size change.
    SchematicModel::Element note;
    note.id = 1;
    note.type = SchematicModel::ElementType::Text;
    inspector.setElement (&note);

    CHECK (announcements == 1);
    const auto forNote = inspector.getPreferredSize();
    CHECK (forNote.y < nothing.y);

    // Every field fits without scrolling.
    inspector.setSize (forNote.x, forNote.y);
    auto* viewport = dynamic_cast<juce::Viewport*> (inspector.getChildComponent (0));
    REQUIRE (viewport != nullptr);
    CHECK (! viewport->getVerticalScrollBar().isVisible());

    // A card slightly too short scrolls rather than clipping.
    inspector.setSize (forNote.x, forNote.y - 20);
    CHECK (viewport->getVerticalScrollBar().isVisible());

    // With nothing selected, the shortcut list fits whole.
    inspector.setElement (nullptr);
    const auto hint = inspector.getPreferredSize();
    inspector.setSize (hint.x, hint.y);
    CHECK (! viewport->getVerticalScrollBar().isVisible());
}

TEST_CASE ("With the shortcuts turned off, the inspector shows only for a selection", "[plugin][ui]")
{
    Celine::ElementInspector inspector;
    inspector.setVisible (true);

    int announcements = 0;
    inspector.onPreferredSizeChanged = [&announcements] { ++announcements; };

    // Turning shortcuts off with nothing selected hides the card at once and announces it.
    inspector.setShortcutsShown (false);
    CHECK (! inspector.isVisible());
    CHECK (announcements == 1);

    SchematicModel::Element resistor;
    resistor.id = 1;
    resistor.type = SchematicModel::ElementType::Resistor;

    inspector.setElement (&resistor);
    CHECK (inspector.isVisible());

    inspector.setElement (nullptr);
    CHECK (! inspector.isVisible());

    // Turned back on, the list returns.
    inspector.setShortcutsShown (true);
    CHECK (inspector.isVisible());
}

TEST_CASE ("The inspector is one width whatever it shows, and everything fits it", "[plugin][ui]")
{
    Celine::ElementInspector inspector;
    auto* viewport = dynamic_cast<juce::Viewport*> (inspector.getChildComponent (0));
    REQUIRE (viewport != nullptr);
    auto* fields = viewport->getViewedComponent();

    auto check = [&] (const juce::String& what)
    {
        INFO (what);
        const auto size = inspector.getPreferredSize();
        CHECK (size.x == Celine::Theme::inspectorWidth);

        // Nothing visible runs past the field column.
        inspector.setSize (size.x, size.y);

        for (auto* child : fields->getChildren())
        {
            if (! child->isVisible())
                continue;

            INFO (child->getName() << " at " << child->getBounds().toString());
            CHECK (child->getRight() <= fields->getWidth());
            CHECK (child->getX() >= 0);
        }
    };

    inspector.setElement (nullptr);
    check ("nothing selected");

    for (int i = 0; i < SchematicModel::numElementTypes; ++i)
    {
        SchematicModel::Element part;
        part.id = i + 1;
        part.type = static_cast<SchematicModel::ElementType> (i);

        inspector.setElement (&part);
        check (SchematicModel::getElementInfo (part.type).name);
        inspector.setElement (nullptr);
    }
}

TEST_CASE ("Input and output can read as calibration levels in dBu", "[plugin][ui]")
{
    // One volt peak per unit full scale: a full-scale sine is 0.707 V RMS against dBu's
    // 0.775 V.
    CHECK (PluginProcessor::fullScaleSineDbu() == Catch::Approx (-0.7918).margin (0.0001));
    CHECK (PluginProcessor::inputGainToDbu (6.0) == Catch::Approx (5.2082).margin (0.0001));
    CHECK (PluginProcessor::outputGainToDbu (-12.0) == Catch::Approx (11.2082).margin (0.0001));

    CHECK (Celine::ControlStrip::formatDbu (5.2082) == "+5.21 dBu");
    CHECK (Celine::ControlStrip::formatDbu (-0.7918) == "-0.79 dBu");
    CHECK (Celine::ControlStrip::formatDbu (-0.001) == "0.00 dBu");

    const auto findKnob = [] (juce::Component& editor, const juce::String& tooltipStart) -> juce::Slider*
    {
        for (auto* child : editor.getChildren())
            if (auto* slider = dynamic_cast<juce::Slider*> (child))
                if (slider->getTooltip().startsWith (tooltipStart))
                    return slider;
        return nullptr;
    };

    struct Restore
    {
        bool was = PluginEditor::levelsShownInDbu();
        ~Restore() { PluginEditor::setLevelsShownInDbu (was); }
    } restore;

    PluginEditor::setLevelsShownInDbu (true);
    {
        PluginProcessor plugin;
        std::unique_ptr<juce::AudioProcessorEditor> editor (plugin.createEditor());

        auto* input = findKnob (*editor, "Input level");
        auto* output = findKnob (*editor, "Output level");
        REQUIRE (input != nullptr);
        REQUIRE (output != nullptr);

        CHECK (input->getTextFromValue (0.0) == "-0.79 dBu");
        CHECK (output->getTextFromValue (-12.0) == "+11.21 dBu");

        // Typed in dBu, it sets the matching gain.
        CHECK (input->getValueFromText ("5.21 dBu") == Catch::Approx (6.0).margin (0.01));
        CHECK (output->getValueFromText ("11.21") == Catch::Approx (-12.0).margin (0.01));
    }

    PluginEditor::setLevelsShownInDbu (false);
    {
        PluginProcessor plugin;
        std::unique_ptr<juce::AudioProcessorEditor> editor (plugin.createEditor());
        auto* input = findKnob (*editor, "Input level");
        REQUIRE (input != nullptr);
        CHECK (input->getTextFromValue (0.0) == "0.0 dB");
    }
}

TEST_CASE ("Readings show two digits after the point", "[plugin][ui]")
{
    using Celine::ElementInspector;

    // A picoamp leak through 475k reads "-0.11pA", not "-1.14447e-13A".
    CHECK (ElementInspector::formatReading (-1.14447e-13, "A") == "-0.11pA");
    CHECK (ElementInspector::formatReading (-0.114447, "A") == "-114.45mA");
    CHECK (ElementInspector::formatReading (6.22162e-9, "W") == "6.22nW");
    CHECK (ElementInspector::formatReading (12.5, "V") == "12.50V");
    CHECK (ElementInspector::formatReading (0.0, "V") == "0.00V");

    // No sign on a rounded zero; a value that rounds up uses the next prefix.
    CHECK (ElementInspector::formatReading (-1.0e-18, "A") == "0.00A");
    CHECK (ElementInspector::formatReading (0.99999999, "V") == "1.00V");
    CHECK (ElementInspector::formatReading (0.996, "V") == "1.00V");
    CHECK (ElementInspector::formatReading (0.994, "V") == "994.00mV");

    // Always exactly two decimals.
    for (const double value : { 3.0e-15, 2.2e-11, 4.7e-7, 0.0331, 1.0, 47.0, 999.0, 12345.0, -8.2e5, 3.3e9 })
    {
        const auto text = ElementInspector::formatReading (value, "V");
        INFO (value << " -> " << text);
        const auto decimals = text.fromFirstOccurrenceOf (".", false, false)
                                  .initialSectionContainingOnly ("0123456789");
        CHECK (decimals.length() == 2);
        CHECK (! text.containsChar ('e'));
    }
}

#if JUCE_CELINE_TOOLTIP_FOR_COMPONENT
TEST_CASE ("A tooltip sits at its control's corner, not at the pointer", "[plugin][ui]")
{
    // Placement comes from the house kit (LookAndFeelBase::getTooltipBoundsForComponent),
    // via Céline's JUCE fork, checked through the editor's look and feel.
    runWithinPluginEditor ([] (PluginProcessor& plugin) {
        auto* editor = plugin.getActiveEditor();
        editor->setSize (1331, 929);

        Celine::ElementPalette* palette = nullptr;
        juce::Button* rebuild = nullptr;

        for (auto* child : editor->getChildren())
        {
            if (auto* p = dynamic_cast<Celine::ElementPalette*> (child)) palette = p;
            if (auto* b = dynamic_cast<juce::TextButton*> (child); b != nullptr && b->getButtonText() == "Rebuild") rebuild = b;
        }

        REQUIRE (palette != nullptr);
        REQUIRE (rebuild != nullptr);

        auto& lf = editor->getLookAndFeel();
        const auto area = editor->getLocalBounds();
        const juce::String tip ("Resistor");

        // Not empty: an empty result would fall back to stock JUCE placement.
        auto* resistor = PaletteParts (*palette).entryNamed ("Resistor");
        REQUIRE (resistor != nullptr);

        const auto entryArea = editor->getLocalArea (resistor, resistor->getLocalBounds());
        const auto forEntry = lf.getTooltipBoundsForComponent (tip, entryArea, area);

        REQUIRE (! forEntry.isEmpty());
        CHECK (forEntry.getBottomLeft() == entryArea.getTopRight());

        // The same size as the pointer-placed tip.
        const auto atPointer = lf.getTooltipBounds (tip, area.getCentre(), area);
        CHECK (forEntry.getWidth() == atPointer.getWidth());
        CHECK (forEntry.getHeight() == atPointer.getHeight());

        // A toolbar button has no room above, so the tip goes below, inside the window.
        const auto buttonArea = rebuild->getBounds();
        const auto forButton = lf.getTooltipBoundsForComponent (tip, buttonArea, area);
        CHECK (forButton.getY() == buttonArea.getBottom());
        CHECK (forButton.getX() == juce::jmin (buttonArea.getRight(), area.getRight() - forButton.getWidth()));
        CHECK (area.contains (forButton));
    });
}
#endif

TEST_CASE ("The console goes after a clean message and stays for a problem", "[plugin][ui]")
{
    Celine::MessageConsole console;

    // Nothing has happened yet.
    CHECK (! console.isVisible());

    SECTION ("a clean build shows, and starts the clock")
    {
        console.setMessages ("Built 15 parts", false, {});
        CHECK (console.isVisible());
        CHECK (! console.hasProblems());
        CHECK (console.isWaitingToHide());
    }

    SECTION ("a failed build stays until something clean replaces it")
    {
        console.setMessages ("Build failed", true, {});
        CHECK (console.isVisible());
        CHECK (console.hasProblems());
        CHECK (! console.isWaitingToHide());

        console.setMessages ("Built 15 parts", false, {});
        CHECK (console.isWaitingToHide());
    }

    SECTION ("a warning holds it up too, through a headline that only changes the top line")
    {
        SchematicModel::Diagnostic warning;
        warning.severity = SchematicModel::Diagnostic::Severity::Warning;
        warning.text = "Pin 2 touches nothing else.";

        console.setMessages ("Built 15 parts, 1 note", false, { warning });
        CHECK (! console.isWaitingToHide());

        // The warning is still listed beneath.
        console.setHeadline ("Circuit changed. Press \"Rebuild\" to load it.", false);
        CHECK (! console.isWaitingToHide());
    }

    SECTION ("a notice is not a problem")
    {
        SchematicModel::Diagnostic notice;
        notice.severity = SchematicModel::Diagnostic::Severity::Info;
        notice.text = "Something worth knowing.";

        console.setMessages ("Built 15 parts, 1 note", false, { notice });
        CHECK (console.isWaitingToHide());
    }
}

TEST_CASE ("A message arriving while the console fades out stays up", "[plugin][ui]")
{
    juce::Component parent;
    parent.setSize (800, 600);

    Celine::MessageConsole console;
    parent.addAndMakeVisible (console);
    console.setBounds (100, 100, 400, 60);

    console.setMessages ("Built 15 parts", false, {});
    REQUIRE (console.isVisible());

    // What timerCallback's fadeOut does on screen: animate a stand-in down to nothing
    // and hide the console. (fadeOut itself skips the animation off screen.)
    auto& animator = juce::Desktop::getInstance().getAnimator();
    animator.animateComponent (&console, console.getBounds(), 0.0f, 400, true, 1.0, 1.0);
    console.setVisible (false);

    // The next build fails inside those 400 ms.
    console.setMessages ("Build failed", true, {});

    juce::MessageManager::getInstance()->runDispatchLoopUntil (700);

    CHECK (! animator.isAnimating (&console));
    CHECK (console.isVisible());
    CHECK (console.getAlpha() == Catch::Approx (1.0f));
}

TEST_CASE ("The console is as big as its messages and wraps past its limit", "[plugin][ui]")
{
    Celine::MessageConsole console;

    console.setMessages ("Built 15 parts", false, {});
    const auto oneLine = console.getPreferredSize (2000);

    // The design's pill: one line, a little over 40 px tall.
    CHECK (oneLine.y < 60);
    CHECK (oneLine.x < 400);

    // A long message at a narrow limit wraps.
    console.setHeadline (juce::String::repeatedString ("This message is far too long to fit on one line. ", 6), false);
    const auto wrapped = console.getPreferredSize (400);
    CHECK (wrapped.x <= 400);
    CHECK (wrapped.y > oneLine.y);
}

// ==============================================================================
// Every model in the palette must reach the engine. The builder falls back to model 0
// via `default:`, so an unhandled index would silently behave as model 0.
// ==============================================================================

namespace
{
    /** Builds a minimal common-cathode stage around one valve and returns a settled output
        sample. Laid out by hand with coincident pins where possible, so no L-routed wire
        crosses another pin. */
    float probeValveModel (SchematicModel::ElementType type, int modelIndex)
    {
        using namespace SchematicModel;

        Schematic schematic;

        // Triode pins are plate/grid/cathode; pentode pins plate/screen/grid/cathode.
        const bool isPentode = type == ElementType::Pentode;
        const int gridPin = isPentode ? 2 : 1;
        const int cathodePin = isPentode ? 3 : 2;

        const auto valve = schematic.addElement (type, 20, 0);
        schematic.findElement (valve)->modelIndex = modelIndex;

        auto pin = [&schematic] (int id, int index) { return schematic.findElement (id)->getPinPosition (index); };

        const auto plate = pin (valve, 0);
        const auto grid = pin (valve, gridPin);
        const auto cathode = pin (valve, cathodePin);

        // Plate load and cathode resistor sit in line with their pins, so no wires.
        const auto plateLoad = schematic.addElement (ElementType::Resistor, plate.x, plate.y - 2);
        const auto cathodeR = schematic.addElement (ElementType::Resistor, cathode.x, cathode.y + 2);
        const auto gridLeak = schematic.addElement (ElementType::Resistor, 8, grid.y + 2);
        schematic.addElement (ElementType::Output, plate.x + 2, plate.y);
        schematic.addElement (ElementType::Input, 6, grid.y);

        schematic.findElement (plateLoad)->value = 100000.0;
        schematic.findElement (cathodeR)->value = 1500.0;
        schematic.findElement (gridLeak)->value = 1.0e6;

        // The supply lies flat above the stage, its terminals side by side.
        const auto supply = schematic.addElement (ElementType::VoltageSource, 0, -20);
        schematic.findElement (supply)->value = 300.0;
        schematic.findElement (supply)->orientation = 1;

        const auto rail = pin (supply, 0);

        // Two ground symbols, one node (a terminal names its net).
        schematic.addElement (ElementType::Ground, pin (supply, 1).x, pin (supply, 1).y + 2);
        const auto bottomGround = schematic.addElement (ElementType::Ground, -10, grid.y + 6);
        const auto gnd = pin (bottomGround, 0);

        schematic.addWire (pin (plateLoad, 0), rail);
        schematic.addWire (grid, pin (gridLeak, 0));
        schematic.addWire (pin (gridLeak, 1), gnd);
        schematic.addWire (pin (cathodeR, 1), gnd);

        // The screen goes to the rail.
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
    /** A shunt clipper with one diode model, probed both ways: forward separates silicon,
        germanium and LEDs; reverse separates the Zeners. Coincident pins where possible. */
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

        // Series resistor above the anode, ground below the cathode: pin on pin.
        const auto series = schematic.addElement (ElementType::Resistor, anode.x, anode.y - 2);
        schematic.findElement (series)->value = 10000.0;

        // Terminals placed by their own pin offsets, which differ per terminal.
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

    // Driven past every clamp both ways: +/-15 V clears the 9 V Zener.
    const auto signature = [] (int index)
    { return probeDiodeModel (index, 15.0f) + probeDiodeModel (index, -15.0f); };

    const float first = signature (0);

    for (int i = 1; i < choices.size(); ++i)
    {
        // The 1N914 is the same die as the 1N4148 and must match it. Named, so nothing
        // appended later inherits the exemption.
        if (choices[i] == "1N914")
        {
            INFO ("the 1N914 is listed as an alias of model 0 and must match it");
            CHECK (std::abs (signature (i) - first) < 1.0e-6f);
            continue;
        }

        // Identical to model 0: either the builder fell through to `default:`, or two entries
        // name the same part.
        INFO ("model " << i << " (" << choices[i] << ") behaves as model 0");
        CHECK (std::abs (signature (i) - first) > 1.0e-6f);
    }
}

TEST_CASE ("Model ids are unique and well formed", "[plugin][schematic]")
{
    using namespace SchematicModel;

    // The id is the file format. Duplicates can't be caught at load (the first match
    // wins), so they are checked here.
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
            CHECK (! id.containsChar (' '));   // ids go in files; keep them terse
            CHECK (id == id.toLowerCase());    // one spelling

            // Shipped models live under a reserved prefix, so user models can't collide.
            CHECK (id.startsWith ("celine:"));
            CHECK (getModelAuthor (type, model) == juce::String::fromUTF8 ("Céline"));

            CHECK (! seen.contains (id));
            seen.add (id);

            // And the round trip.
            CHECK (findModelById (type, id) == model);
        }

        // An unknown name is -1, not 0.
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

    // The index isn't written to the file.
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

    // Falls back to model 0; the rest of the sheet is fine.
    CHECK (reloaded.getElements()[0].modelIndex == 0);

    // And reports it.
    const auto warnings = reloaded.getLoadWarnings();
    REQUIRE (warnings.size() == 1);
    INFO (warnings[0]);
    // Names only the missing model; the fallback shows in the inspector and on the sheet.
    CHECK (warnings[0] == "\"triode-12ax7-from-the-future\" model not found in this version.");

    // Drained after the first report.
    CHECK (reloaded.takeLoadWarnings().size() == 1);
    CHECK (reloaded.getLoadWarnings().isEmpty());
}

TEST_CASE ("An unresolved model id survives a save", "[plugin][schematic]")
{
    using namespace SchematicModel;

    // Hosts save sessions on their own, so the missing id must survive a save, or it would
    // be lost for good.
    Schematic sheet;
    sheet.addElement (ElementType::Triode, 0, 0);

    auto tree = sheet.toValueTree();
    tree.getChild (0).setProperty ("modelId", "lucas:mullard-clone:7f3a", nullptr);

    Schematic opened;
    opened.restoreFromValueTree (tree);

    REQUIRE (opened.getElements().size() == 1);
    CHECK (opened.getElements()[0].modelIndex == 0);                            // builds as something real
    CHECK (opened.getElements()[0].unresolvedModelId == "lucas:mullard-clone:7f3a");

    // Saved again, the id is still the one the sheet asked for.
    const auto resaved = opened.toValueTree();
    CHECK (resaved.getChild (0).getProperty ("modelId").toString() == "lucas:mullard-clone:7f3a");

    // A second round trip is lossless too.
    Schematic again;
    again.restoreFromValueTree (resaved);
    CHECK (again.getElements()[0].unresolvedModelId == "lucas:mullard-clone:7f3a");
    CHECK (again.getLoadWarnings().size() == 1);

    // Choosing a model settles it, as the inspector does.
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

    // copyPropertiesTo defines "the same part" (middle-click cloning uses it); every
    // member of Element must be listed there.
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

    // A sheet without model ids loads with every model reset, and says so.
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

    // The head of each list is pinned: it is the `default:` fallback and the substitute
    // for unknown ids. And every model belongs to exactly one heading.
    struct Expected { ElementType type; int index; const char* name; };

    const Expected heads[] = {
        { ElementType::Diode,       0, "1N4148" },
        { ElementType::Diode,       1, "1N34A" },
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

        // The menu is built group by group, so every model needs a heading.
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
            // Identical to model 0 means the builder fell through to `default:`.
            INFO ("model " << i << " (" << choices[i] << ") behaves as model 0");
            CHECK (std::abs (probeValveModel (type, i) - first) > 1.0e-6f);
        }
    }
}

namespace
{
    /** A common-emitter stage driven through 100k, so the Miller pole lands in the audio
        band. Coincident pins where possible; wires route across then down, clear of pins. */
    float probeTransistorStage (double frequency)
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

        // Emitter on a ground pin; collector load and output terminal on the collector pin.
        schematic.addElement (ElementType::Ground, emitter.x, emitter.y + 2);
        const auto collectorLoad = schematic.addElement (ElementType::Resistor, collector.x, collector.y - 2);
        schematic.addElement (ElementType::Output, collector.x + 2, collector.y);

        // Supply flat at the top, with its own ground symbol (the same node).
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

        // Rotated parts put pin 0 right and pin 1 left, so the short runs are pin 1 to pin 1
        // and pin 0 to pin 0. A straight input wire would cross the capacitor's far pin.
        schematic.addWire (pin (collectorLoad, 0), pin (supply, 0));
        schematic.addWire (pin (bias, 0), pin (supply, 0));
        schematic.addWire (pin (bias, 1), base);
        schematic.addWire (pin (input, 0), pin (coupling, 1));
        schematic.addWire (pin (coupling, 0), pin (source, 1));
        schematic.addWire (pin (source, 0), base);

        auto result = buildCircuits (schematic, 48000.0, 1);
        INFO (result.error);
        REQUIRE (result.isValid());

        auto& circuit = *result.circuits[0];
        REQUIRE (circuit.foundOperatingPoint());

        // Peak over the second half: the settled amplitude.
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

TEST_CASE ("A built transistor stage carries its junction capacitance", "[plugin][schematic]")
{
    // Without junction capacitances this stage is flat to 20 kHz, so the rolloff is the
    // Miller capacitance (a pole near 9 kHz).
    const float at1k = probeTransistorStage (1000.0);
    const float at20k = probeTransistorStage (20000.0);

    INFO ("1 kHz: " << at1k << ", 20 kHz: " << at20k);
    CHECK (at20k < at1k * 0.7f);
}

TEST_CASE ("Sheets saved with the old accuracy switches open on the full model", "[plugin]")
{
    // Sheets may carry removed options (capacitance, Early effect, Newton prediction
    // switches). They are ignored and dropped on the next save.
    PluginProcessor plugin;
    auto old = plugin.createDocument();
    old.setProperty ("interelectrodeCapacitance", false, nullptr);
    old.setProperty ("transistorJunctionCapacitance", false, nullptr);
    old.setProperty ("transistorEarlyEffect", false, nullptr);
    old.setProperty ("predictNewtonSeed", true, nullptr);

    PluginProcessor loaded;
    REQUIRE (loaded.restoreDocument (old));

    const auto saved = loaded.createDocument();

    for (const auto* name : { "interelectrodeCapacitance", "transistorJunctionCapacitance",
                              "transistorEarlyEffect", "predictNewtonSeed" })
    {
        INFO (name);
        CHECK (! saved.hasProperty (name));
    }
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

    // No pins, so net extraction never sees it, even placed over the drawing.
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

    // Selectable: a pinless element's hit box comes from its text.
    const auto bounds = schematic.getElementBounds (*schematic.findElement (note));
    CHECK (bounds.getWidth() > 3);
    CHECK (schematic.findElementAt ({ 4, 12 }) == note);

    // And it survives a save/load round trip.
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

    // Drawn around the existing circuit, as a group box is used.
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

    // No pins, so net extraction never sees it.
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

    // --------------------------------------------------------------------------
    // Grabbed by its edge; everything inside stays clickable.
    const auto bounds = group->getRectangleBounds();

    CHECK (schematic.findElementAt (bounds.getTopLeft()) == box);
    CHECK (schematic.findElementAt (bounds.getBottomRight()) == box);

    // The middle doesn't select it.
    CHECK (schematic.findElementAt (bounds.getCentre()) != box);

    // Nor does any part inside, though the box was placed last.
    for (const auto& e : schematic.getElements())
    {
        if (e.type == ElementType::Rectangle)
            continue;

        INFO (getElementInfo (e.type).name << " at " << e.x << "," << e.y);
        CHECK (schematic.findElementAt ({ e.x, e.y }) != box);
    }

    // --------------------------------------------------------------------------
    // Size, colour and title survive a round trip through the document (the path presets
    // and host state take).
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

    // Nothing else gains a size.
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

    // --------------------------------------------------------------------------
    // Dragging one corner keeps the other three in place, as SchematicCanvas::resizeTo
    // does. Walked a square at a time, so drift would accumulate.
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

    // --------------------------------------------------------------------------
    // Sizes too small to grab are clamped.
    group->width = 0;
    group->height = -5;

    const auto clamped = group->getRectangleBounds();
    CHECK (clamped.getWidth() == minRectangleSize);
    CHECK (clamped.getHeight() == minRectangleSize);
    CHECK (clamped.getCentre() == juce::Point<int> (group->x, group->y));
}

namespace
{
    /** A transformer driven from a real source impedance into 8 ohms, mostly by coincident
        pins. */
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

        // Source: input -- Rs -- primary A, laid flat onto both.
        const auto rs = sheet.addElement (ElementType::Resistor, primaryA.x - 4, primaryA.y);
        sheet.findElement (rs)->orientation = 1;
        sheet.findElement (rs)->value = sourceResistance;
        sheet.addElement (ElementType::Input, primaryA.x - 8, primaryA.y);
        sheet.addWire (pin (rs, 0), primaryA);

        // Primary B and secondary B to ground, each by a symbol on the pin.
        sheet.addElement (ElementType::Ground, primaryB.x, primaryB.y + 2);
        sheet.addElement (ElementType::Ground, secondaryB.x, secondaryB.y + 2);

        // The 8 ohm load hangs off secondary A; the output reads across it.
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

        // Ideal has unlimited bandwidth.
        CHECK (std::abs (idealLow - idealMid) < idealMid * 0.02f);
        CHECK (std::abs (idealHigh - idealMid) < idealMid * 0.02f);

        // Real rolls off at both ends: magnetising inductance below, leakage above.
        CHECK (realLow < realMid * 0.9f);
        CHECK (realHigh < realMid * 0.9f);

        // And passes the midband at roughly the ideal level.
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

        // The magnetising inductance shorts DC; an ideal transformer doesn't.
        CHECK (std::abs (idealDC) > 0.001f);
        CHECK (std::abs (realDC) < std::abs (idealDC) * 0.01f);
    }

    SECTION ("1:8 is expressible, which a single ratio box could not manage")
    {
        // "1:8" must be accepted, and 0.125 must read "1:8".
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

TEST_CASE ("Every model record has a name", "[schematic]")
{
    using namespace SchematicModel;

    // A semicolon in a description would split the model list and add a nameless entry.
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

            // An empty name marks the tail of an accidental split. Names are drawn under the part,
            // so they must also stay short.
            CHECK (name.isNotEmpty());
            CHECK (name.length() <= 24);

            // No stray whitespace.
            CHECK (name == name.trim());
        }
    }
}

TEST_CASE ("Every knob in the pool reaches a drawn control", "[plugin]")
{
    using namespace SchematicModel;

    // One pot per slot, plus two past the pool to test the ceiling.
    const int drawn = PluginProcessor::maxLiveControls + 2;

    PluginProcessor plugin;
    plugin.prepareToPlay (48000.0, 512);

    auto& sheet = plugin.getSchematic();
    sheet.clear();

    // Above the pots' line, so the wire's corner misses the first pot's strap.
    const auto input = sheet.addElement (ElementType::Input, -4, -6);
    std::vector<int> pots;
    int x = 0;

    for (int i = 0; i < drawn; ++i)
    {
        // Pot pins are (top, wiper, bottom). Wiper strapped to bottom makes a variable
        // resistor, chained in series so every one is in the signal path.
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

    // The output gets its own loaded node (wired to ground it would be refused).
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

    // Every slot has a parameter, with distinct ids.
    juce::StringArray ids;

    for (int i = 0; i < PluginProcessor::maxLiveControls; ++i)
    {
        const auto id = PluginProcessor::getControlParameterId (i);
        INFO ("slot " << i << " is " << id);
        REQUIRE (plugin.apvts.getParameter (id) != nullptr);
        ids.addIfNotAlreadyThere (id);
    }

    CHECK (ids.size() == PluginProcessor::maxLiveControls);

    // Controls past the ceiling are reported by name.
    const auto warnings = joinDiagnostics (result);
    INFO (warnings);
    CHECK (warnings.contains ("no knob"));
    CHECK (warnings.contains (controls[static_cast<size_t> (PluginProcessor::maxLiveControls)].name));
}

TEST_CASE ("A freshly placed part has no value and refuses to build", "[schematic]")
{
    using namespace SchematicModel;

    // Every numeric part starts at zero, so an unset value is never mistaken for a choice.
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

    // An unfilled part stops the build by name (a zero-ohm resistor is a short).
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

    // The headline gives the count; the list names each part and where.
    INFO (joinDiagnostics (refused));
    CHECK (joinDiagnostics (refused).contains ("R99"));

    const auto named = std::find_if (refused.diagnostics.begin(), refused.diagnostics.end(),
                                     [] (const Diagnostic& d) { return d.subject == "R99"; });
    REQUIRE (named != refused.diagnostics.end());
    CHECK (named->isError());
    CHECK (named->hasPosition);
    CHECK (named->elementId == r);

    // Give it a value and it builds.
    stage.findElement (r)->value = 100000.0;
    const auto accepted = buildCircuits (stage, 48000.0, 1);
    INFO (accepted.error);
    CHECK (accepted.isValid());
}

TEST_CASE ("Switches sharing a label become one ganged control", "[schematic]")
{
    using namespace SchematicModel;

    // Three switches: two called "CHANNEL", one alone.
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

    // Wired in a chain so all are in the circuit.
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

    // Two controls: the pair became one.
    REQUIRE (result.controls.size() == 2);

    const auto channel = std::find_if (result.controls.begin(), result.controls.end(),
                                       [] (const LiveControl& c) { return c.name == "CHANNEL"; });
    REQUIRE (channel != result.controls.end());
    CHECK (channel->contacts.size() == 2);

    const auto bright = std::find_if (result.controls.begin(), result.controls.end(),
                                      [] (const LiveControl& c) { return c.name == "BRIGHT"; });
    REQUIRE (bright != result.controls.end());
    CHECK (bright->contacts.size() == 1);

    juce::ignoreUnused (a, b, lone);
}

TEST_CASE ("An SPDT sends the signal to one throw or the other", "[schematic]")
{
    using namespace SchematicModel;

    // Common in, two throws each through a different resistor to a shared load: the gain
    // says which throw is made. Every join is coincident pins.
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

    // The two non-coincident joins, both straight: across the throw tops, and down to
    // ground.
    auto pin = [&sheet] (int id, int p) { return sheet.findElement (id)->getPinPosition (p); };
    sheet.addWire (pin (viaA, 0), pin (viaB, 0));
    sheet.addWire (pin (load, 0), pin (ground, 0));

    const auto result = buildCircuits (sheet, 48000.0, 1);
    INFO (result.error);
    REQUIRE (result.isValid());
    REQUIRE (result.controls.size() == 1);

    // A dangling pin would leave the divider unloaded and both throws reading a gain of
    // one.
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

namespace
{
    /** One common fed from the input, each throw through its own resistor to an output
        loaded by 10k: the made throw reads as a gain. */
    struct ThrowBench
    {
        SchematicModel::Schematic sheet;
        int sw = 0;

        ThrowBench (SchematicModel::SwitchAction action, std::vector<double> viaOhms)
        {
            using namespace SchematicModel;

            sw = sheet.addElement (ElementType::Spdt, 0, 0);
            sheet.findElement (sw)->switchAction = action;
            sheet.addElement (ElementType::Input, -2, 2);   // pin lands on the common

            std::vector<juce::Point<int>> tops;

            for (size_t t = 0; t < viaOhms.size(); ++t)
            {
                const auto contact = sheet.findElement (sw)->getPinPosition (1 + (int) t);
                const auto via = sheet.addElement (ElementType::Resistor, contact.x, contact.y - 2);
                sheet.findElement (via)->value = viaOhms[t];
                tops.push_back (sheet.findElement (via)->getPinPosition (0));
            }

            for (size_t t = 1; t < tops.size(); ++t)
                sheet.addWire (tops[t - 1], tops[t]);

            // The output pin lands on the leftmost resistor's top; the load hangs from it to
            // ground.
            sheet.addElement (ElementType::Output, tops.front().x + 2, tops.front().y);
            const auto load = sheet.addElement (ElementType::Resistor, tops.front().x, tops.front().y - 2);
            sheet.findElement (load)->value = 10000.0;
            const auto ground = sheet.addElement (ElementType::Ground, tops.front().x - 4, tops.front().y - 6);
            sheet.addWire (sheet.findElement (load)->getPinPosition (0), sheet.findElement (ground)->getPinPosition (0));
        }

        /** The gain at each control value, settled. */
        std::vector<float> gains (std::initializer_list<float> values)
        {
            const auto result = SchematicModel::buildCircuits (sheet, 48000.0, 1);
            INFO (result.error);
            REQUIRE (result.isValid());
            REQUIRE (result.controls.size() == 1);

            for (const auto& diagnostic : result.diagnostics)
                INFO (diagnostic.text);

            CHECK (result.diagnostics.empty());

            auto& circuit = *result.circuits[0];
            std::vector<float> out;

            for (const float value : values)
            {
                result.controls[0].apply (circuit, value);
                float y = 0.0f;

                for (int i = 0; i < 64; ++i)
                    y = circuit.process (1.0f);

                out.push_back (y);
            }

            return out;
        }
    };
}

TEST_CASE ("An ON-ON-ON switch makes each of its three throws in turn", "[schematic]")
{
    // A is 1k, B 10k, C 100k, into 10k. Top position is A, middle B, bottom C, so the
    // strip reads C, B, A left to right.
    ThrowBench bench (SchematicModel::SwitchAction::OnOnOn, { 1000.0, 10000.0, 100000.0 });
    const auto g = bench.gains ({ 1.0f, 0.5f, 0.0f });

    CHECK (g[0] == Catch::Approx (10.0 / 11.0).margin (1.0e-3));
    CHECK (g[1] == Catch::Approx (10.0 / 20.0).margin (1.0e-3));
    CHECK (g[2] == Catch::Approx (10.0 / 110.0).margin (1.0e-3));
}

TEST_CASE ("An ON-OFF-ON switch connects nothing in the middle", "[schematic]")
{
    ThrowBench bench (SchematicModel::SwitchAction::OnOffOn, { 1000.0, 100000.0 });
    const auto g = bench.gains ({ 1.0f, 0.5f, 0.0f });

    CHECK (g[0] == Catch::Approx (10.0 / 11.0).margin (1.0e-3));
    CHECK (std::abs (g[1]) < 1.0e-3f);
    CHECK (g[2] == Catch::Approx (10.0 / 110.0).margin (1.0e-3));
}

TEST_CASE ("Each switch action makes one throw per position, or none", "[schematic]")
{
    using namespace SchematicModel;

    for (const auto action : { SwitchAction::OnOn, SwitchAction::OnOffOn, SwitchAction::OnOnOn })
    {
        Element sw;
        sw.type = ElementType::Spdt;
        sw.switchAction = action;

        const int throws = sw.getSwitchThrows();
        const int positions = sw.getSwitchPositions();
        INFO (getSwitchActionNames()[(int) action]);

        // A common and its throws, each on its own grid point.
        REQUIRE (sw.getPinCount() == 1 + throws);
        std::set<std::pair<int, int>> seen;

        for (int pin = 0; pin < sw.getPinCount(); ++pin)
        {
            const auto p = sw.getPinPosition (pin);
            CHECK (seen.insert ({ p.x, p.y }).second);
        }

        // The input on the common, each throw to ground through 10k, the output on throw A.
        Schematic sheet;
        const auto id = sheet.addElement (ElementType::Spdt, 0, 0);
        sw.copyPropertiesTo (*sheet.findElement (id));

        const auto common = sheet.findElement (id)->getPinPosition (0);
        sheet.addElement (ElementType::Input, common.x - 2, common.y);

        for (int t = 0; t < throws; ++t)
        {
            const auto contact = sheet.findElement (id)->getPinPosition (1 + t);
            const auto r = sheet.addElement (ElementType::Resistor, contact.x, contact.y - 2);
            sheet.findElement (r)->value = 10000.0;
            sheet.addElement (ElementType::Ground, contact.x, contact.y - 2);
        }

        const auto a = sheet.findElement (id)->getPinPosition (1);
        sheet.addElement (ElementType::Output, a.x + 2, a.y);

        const auto result = buildCircuits (sheet, 48000.0, 1);
        INFO (result.error);
        REQUIRE (result.controls.size() == 1);
        const auto& contacts = result.controls[0].contacts;
        REQUIRE ((int) contacts.size() == throws);
        CHECK (result.controls[0].positions == positions);

        // Each position makes exactly the throw getThrowAt names, never two.
        for (int k = 0; k < positions; ++k)
        {
            const float value = (float) k / (float) (positions - 1);
            int made = 0;

            for (int t = 0; t < throws; ++t)
            {
                const bool closed = contacts[(size_t) t].isClosedAt (value);
                made += closed ? 1 : 0;
                CHECK (closed == (sw.getThrowAt (k) == t));
            }

            CHECK (made == (sw.getThrowAt (k) >= 0 ? 1 : 0));
        }
    }
}

TEST_CASE ("A switch's action and position survive a save", "[schematic]")
{
    using namespace SchematicModel;

    Schematic sheet;
    const auto id = sheet.addElement (ElementType::Spdt, 0, 0);
    auto* sw = sheet.findElement (id);
    sw->switchAction = SwitchAction::OnOffOn;
    sw->setControlPosition (0.5f);

    Schematic reloaded;
    reloaded.restoreFromValueTree (sheet.toValueTree());
    const auto* back = reloaded.findElement (id);
    REQUIRE (back != nullptr);
    CHECK (back->switchAction == SwitchAction::OnOffOn);
    CHECK (back->switchPosition == 1);
    CHECK (back->getControlPosition() == Catch::Approx (0.5f));

    // A sheet without an action is ON-ON, on the throw `closed` says.
    for (const bool closed : { true, false })
    {
        auto tree = sheet.toValueTree();
        auto node = tree.getChildWithName ("ELEMENT");
        node.removeProperty ("action", nullptr);
        node.removeProperty ("switchPosition", nullptr);
        node.setProperty ("closed", closed, nullptr);

        Schematic old;
        old.restoreFromValueTree (tree);
        const auto* part = old.findElement (id);
        REQUIRE (part != nullptr);
        CHECK (part->switchAction == SwitchAction::OnOn);
        CHECK (part->getPinCount() == 3);
        CHECK (part->getThrowAt (part->switchPosition) == (closed ? 0 : 1));
    }
}

TEST_CASE ("An SPDT gangs with a plain switch sharing its label", "[schematic]")
{
    using namespace SchematicModel;

    // Switch contacts of both kinds gang with each other.
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

    // One control holding both kinds: the SPDT's two throws and the plain switch's one.
    REQUIRE (result.controls.size() == 1);
    CHECK (result.controls[0].name == "CHANNEL");
    CHECK (result.controls[0].contacts.size() == 3);
}

namespace
{
    /** Plays `seconds` of a 220 Hz sine through the plugin as it stands; returns the
        output's peak. */
    float playThrough (PluginProcessor& plugin, double seconds)
    {
        constexpr int blockSize = 256;
        plugin.prepareToPlay (48000.0, blockSize);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        double phase = 0.0;
        float peak = 0.0f;

        for (int block = 0; block < juce::roundToInt (seconds * 48000.0 / blockSize); ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto sample = 0.5f * (float) std::sin (phase);
                phase += juce::MathConstants<double>::twoPi * 220.0 / 48000.0;
                buffer.setSample (0, i, sample);
                buffer.setSample (1, i, sample);
            }

            plugin.processBlock (buffer, midi);
            peak = juce::jmax (peak, buffer.getMagnitude (0, 0, blockSize));
        }

        return peak;
    }
}

TEST_CASE ("Every factory example runs without holding a sample", "[plugin]")
{
    // The console warns about held samples, so a factory sheet that held any would warn
    // on its own the moment it played.
    const auto names = SchematicModel::Examples::getNames();

    for (int i = 0; i < names.size(); ++i)
    {
        INFO (names[i]);
        PluginProcessor plugin;
        SchematicModel::Examples::load (plugin.getSchematic(), i);
        REQUIRE (plugin.rebuild().isValid());

        CHECK (playThrough (plugin, 0.5) > 1.0e-4f); // it ran
        CHECK (plugin.getHeldSampleTotal() == 0);
    }
}

TEST_CASE ("Held samples reach the processor's count", "[plugin]")
{
    using namespace SchematicModel;

    // An op-amp astable drawn on the sheet: positive feedback through a divider, an RC on
    // the inverting input. The comparator's flip is a fold the solver cannot cross, so
    // samples are held (see "A sample that does not converge is held" in CelineEngine.cpp)
    // and the editor has something to warn about.
    PluginProcessor plugin;
    auto& sheet = plugin.getSchematic();
    sheet.clear();

    const auto opamp = sheet.addElement (ElementType::OpAmp, 0, 0);
    sheet.findElement (opamp)->value = 15.0;  // Supply +
    sheet.findElement (opamp)->valueB = -15.0; // Supply -

    auto resistor = [&sheet] (int x, int y, double ohms)
    {
        const auto id = sheet.addElement (ElementType::Resistor, x, y); // pins (x, y-2), (x, y+2)
        sheet.findElement (id)->value = ohms;
        return id;
    };

    // Straight runs only, none crossing a pin it is not meant for. The op-amp's pins:
    // in+ (-4,-2), in- (-4,2), out (4,0). Parts sitting on each other's pins connect.
    sheet.addElement (ElementType::Input, -32, -22);       // pin (-30,-22)
    resistor (-30, -20, 1.0e6);                             // input to in+
    sheet.addWire ({ -30, -18 }, { -30, -2 });
    sheet.addWire ({ -4, -2 }, { -40, -2 });                // the in+ bus

    resistor (-36, 6, 1.0e4);                               // in+ to ground
    sheet.addWire ({ -36, 4 }, { -36, -2 });
    sheet.addElement (ElementType::Ground, -36, 10);        // pin (-36,8)

    resistor (10, -10, 1.0e4);                              // out to in+
    sheet.addWire ({ 4, 0 }, { 10, 0 });
    sheet.addWire ({ 10, 0 }, { 10, -8 });
    sheet.addWire ({ 10, -12 }, { 10, -16 });
    sheet.addWire ({ 10, -16 }, { -12, -16 });
    sheet.addWire ({ -12, -16 }, { -12, -2 });

    sheet.addWire ({ -4, 2 }, { -8, 2 });                   // the in- bus
    sheet.addWire ({ -8, 2 }, { -8, 14 });
    const auto cap = sheet.addElement (ElementType::Capacitor, -8, 16); // in- to ground
    sheet.findElement (cap)->value = 100.0e-9;
    sheet.addElement (ElementType::Ground, -8, 20);         // pin (-8,18)

    resistor (20, 10, 1.0e4);                               // out to in-
    sheet.addWire ({ 10, 0 }, { 20, 0 });
    sheet.addWire ({ 20, 0 }, { 20, 8 });
    sheet.addWire ({ 20, 12 }, { -8, 12 });

    sheet.addWire ({ 20, 0 }, { 30, 0 });
    sheet.addElement (ElementType::Output, 32, 0);          // pin (30,0)

    const auto built = plugin.rebuild();
    juce::String report = built.error;

    for (const auto& diagnostic : built.diagnostics)
        report << "\n" << diagnostic.subject << ": " << diagnostic.text;

    INFO (report);
    REQUIRE (built.isValid());

    playThrough (plugin, 0.5);
    CHECK (plugin.getHeldSampleTotal() > 0);
}

TEST_CASE ("A warning headline keeps the console up and the build's messages under it", "[editor]")
{
    using namespace SchematicModel;

    Celine::MessageConsole console;

    Diagnostic note;
    note.severity = Diagnostic::Severity::Info;
    note.text = "Built.";
    console.setMessages ("Rebuilt", false, { note });
    REQUIRE (! console.hasProblems());

    console.setWarningHeadline ("The circuit could not be solved on 12 samples.");
    CHECK (console.isVisible());
    CHECK (console.hasProblems());        // held up, not faded out
    CHECK (! console.isWaitingToHide());

    // An ordinary status line afterwards replaces it, and the console may fade again.
    console.setHeadline ("Saved.", false);
    CHECK (! console.hasProblems());
}

TEST_CASE ("A lever's middle is unlit only when nothing ganged on it connects there", "[schematic]")
{
    using namespace SchematicModel;

    // Commons on the input, throws on the output through a load: every pin has a path.
    const auto build = [] (std::vector<SwitchAction> actions)
    {
        Schematic sheet;
        const auto input = sheet.addElement (ElementType::Input, -8, 0);
        const auto output = sheet.addElement (ElementType::Output, 40, 0);
        const auto ground = sheet.addElement (ElementType::Ground, 40, 12);
        const auto load = sheet.addElement (ElementType::Resistor, 40, 6);
        sheet.findElement (load)->value = 10000.0;

        auto pin = [&sheet] (int id, int p) { return sheet.findElement (id)->getPinPosition (p); };
        sheet.addWire (pin (output, 0), pin (load, 0));
        sheet.addWire (pin (load, 1), pin (ground, 0));

        int x = 0;

        for (const auto action : actions)
        {
            const auto sw = sheet.addElement (ElementType::Spdt, x, 0);
            sheet.findElement (sw)->label = "MODE";
            sheet.findElement (sw)->switchAction = action;
            sheet.addWire (pin (input, 0), pin (sw, 0));

            for (int t = 1; t < sheet.findElement (sw)->getPinCount(); ++t)
                sheet.addWire (pin (sw, t), pin (output, 0));

            x += 12;
        }

        auto result = buildCircuits (sheet, 48000.0, 1);
        INFO (result.error);
        REQUIRE (result.isValid());
        REQUIRE (result.controls.size() == 1);
        return result.controls[0];
    };

    // Alone, an ON-OFF-ON's middle connects nothing.
    CHECK (build ({ SwitchAction::OnOffOn }).offPosition == 1);
    CHECK (build ({ SwitchAction::OnOnOn }).offPosition == -1);

    // Ganged with an ON-ON-ON, the middle makes its throw B, so it is not off -- in either
    // drawing order. It used to follow whichever part was drawn first.
    CHECK (build ({ SwitchAction::OnOffOn, SwitchAction::OnOnOn }).offPosition == -1);
    CHECK (build ({ SwitchAction::OnOnOn, SwitchAction::OnOffOn }).offPosition == -1);

    // Two ON-OFF-ONs: both open in the middle.
    CHECK (build ({ SwitchAction::OnOffOn, SwitchAction::OnOffOn }).offPosition == 1);
}

TEST_CASE ("A switch value lands on the same position in the circuit and on screen", "[schematic]")
{
    using namespace SchematicModel;

    // The knob parameters start at 0.5, so a halfway value is the common case, not an edge.
    for (const float value : { 0.0f, 0.49f, 0.5f, 0.51f, 1.0f })
    {
        INFO ("value " << value);
        const bool up = value >= 0.5f;

        // A plain switch: the contact, the part and the strip toggle agree.
        Element plain;
        plain.type = ElementType::Switch;
        plain.setControlPosition (value);

        LiveControl::Contact contact;
        contact.positions = 2;
        contact.closedIn = 1u << 1; // closed in its upper position
        CHECK (contact.isClosedAt (value) == up);
        CHECK (plain.closed == up);
        CHECK ((Element::positionForValue (value, 2) == 1) == up);

        // An ON-ON changeover: the throw drawn is the throw made.
        Element changeover;
        changeover.type = ElementType::Spdt;
        changeover.switchAction = SwitchAction::OnOn;
        changeover.setControlPosition (value);
        CHECK (changeover.switchPosition == LiveControl::Contact::positionFor (value, 2));
        CHECK (changeover.closed == up);
    }

    // Three positions: thirds of the travel, the middle stop centred on 0.5.
    CHECK (Element::positionForValue (0.0f, 3) == 0);
    CHECK (Element::positionForValue (0.24f, 3) == 0);
    CHECK (Element::positionForValue (0.25f, 3) == 1);
    CHECK (Element::positionForValue (0.5f, 3) == 1);
    CHECK (Element::positionForValue (0.75f, 3) == 2);
    CHECK (Element::positionForValue (1.0f, 3) == 2);
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
    // A fundamental exactly on bin 641 (prime, FFT length a power of two), so no harmonic
    // below the 641st aliases onto another harmonic's bin, and the fundamental's skirt
    // doesn't leak into the alias bins.
    constexpr int order = 14;
    constexpr int size = 1 << order;
    constexpr double frequency = 641.0 * 48000.0 / size; // 1877.9 Hz

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
                // 4-term Blackman-Harris: sidelobes under -92 dB.
                const double x = 2.0 * juce::MathConstants<double>::pi * written / size;
                const double w = 0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2.0 * x)
                               - 0.01168 * std::cos (3.0 * x);
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

    std::vector<double> alias;
    std::vector<int> latency;
    juce::String figures;

    for (const int factor : PluginProcessor::oversamplingFactors)
    {
        PluginProcessor plugin;
        slammed (plugin);
        REQUIRE (plugin.setOversamplingFactor (factor).isValid());
        REQUIRE (plugin.oversamplingFactor == factor);

        alias.push_back (aliasToHarmonicDb (plugin));
        latency.push_back (plugin.getLatencySamples());
        figures << factor << "x " << juce::String (alias.back(), 1) << " dB, latency " << latency.back() << "; ";
    }

    INFO (figures);
    UNSCOPED_INFO ("Alias to harmonic: " << figures);

    // About 20 dB measured; asserted loosely.
    CHECK (alias[1] < alias[0] - 10.0);

    // Every doubling must improve aliasing. Measured -23.7 / -43.7 / -59.8 / -93.1 / -99.1
    // dB from 1x to 16x.
    for (size_t i = 2; i < alias.size(); ++i)
        CHECK (alias[i] < alias[i - 1] - 3.0);

    // The host is told the halfband latency; none may exceed the bypass delay line.
    CHECK (latency[0] == 0);

    for (size_t i = 1; i < latency.size(); ++i)
    {
        CHECK (latency[i] >= latency[i - 1]);
        CHECK (latency[i] > 0);
        CHECK (latency[i] <= PluginProcessor::maximumOversamplingLatency);
    }
}

TEST_CASE ("An oversampling factor from a document is a power of two in range", "[plugin]")
{
    // Any factor is rounded down to a power of two in range; circuits run at the filters'
    // rate.
    const std::pair<int, int> cases[] = { { -3, 1 }, { 0, 1 }, { 1, 1 }, { 3, 2 }, { 4, 4 }, { 6, 4 },
                                          { 8, 8 }, { 12, 8 }, { 16, 16 }, { 17, 16 }, { 64, 16 } };

    for (const auto& [asked, expected] : cases)
    {
        PluginProcessor plugin;
        plugin.prepareToPlay (48000.0, 256);
        REQUIRE (plugin.setOversamplingFactor (asked).isValid());

        INFO ("asked for " << asked);
        CHECK (plugin.oversamplingFactor == expected);

        auto document = plugin.createDocument();
        document.setProperty ("oversampling", asked, nullptr);

        PluginProcessor loaded;
        loaded.prepareToPlay (48000.0, 256);
        REQUIRE (loaded.restoreDocument (document));
        CHECK (loaded.oversamplingFactor == expected);
    }
}

TEST_CASE ("Fast exp / log / pow does not change the output", "[plugin][schematic]")
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

    // Fast maths is accurate to 3e-10 relative, four orders inside the Newton tolerance.
    BuildOptions fast;
    fast.fastMath = true;
    const auto withFastMath = render (fast);

    REQUIRE (withFastMath.size() == reference.size());

    for (size_t i = 0; i < reference.size(); ++i)
        REQUIRE (std::abs (withFastMath[i] - reference[i]) < 1.0e-5f);

    CircuitComponents::FastMath::setEnabled (false);
}

TEST_CASE ("Performance settings travel with the document", "[plugin]")
{
    PluginProcessor plugin;

    CHECK (plugin.oversamplingFactor == 1);
    CHECK (! plugin.buildOptions.fastMath);

    // An untouched sheet writes none of these properties.
    CHECK (! plugin.createDocument().hasProperty ("oversampling"));

    plugin.buildOptions.fastMath = true;
    plugin.oversamplingFactor = 16;

    const auto document = plugin.createDocument();

    PluginProcessor loaded;
    REQUIRE (loaded.restoreDocument (document));
    CHECK (loaded.oversamplingFactor == 16);
    CHECK (loaded.buildOptions.fastMath);

    CircuitComponents::FastMath::setEnabled (false);
}

TEST_CASE ("Changing oversampling while audio is running is safe", "[plugin]")
{
    // Swapping the oversampler must be safe while the audio thread runs. Two real threads,
    // as in a host.
    PluginProcessor plugin;
    plugin.prepareToPlay (48000.0, 256);

    setParameter (plugin, "input", 0.0f);
    setParameter (plugin, "output", 0.0f);
    setParameter (plugin, "bypass", 0.0f);

    std::atomic<bool> running { true };
    std::atomic<int> blocksProcessed { 0 };

    // Catch2 isn't thread safe: the audio thread only sets a flag, checked after join.
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

    // Change the factor repeatedly from this thread, as the Settings menu does.
    for (int pass = 0; pass < 40; ++pass)
        for (const int factor : { 2, 4, 16, 1, 8, 4, 2, 16, 1 })
            REQUIRE (plugin.setOversamplingFactor (factor).isValid());

    running.store (false);
    audio.join();

    INFO ("processed " << blocksProcessed.load() << " blocks while switching");
    CHECK (blocksProcessed.load() > 0);
    CHECK (! sawNonFinite.load());

    // The plugin still works afterwards.
    REQUIRE (plugin.setOversamplingFactor (1).isValid());
    CHECK (measurePeak (plugin, 0.5f) > 0.0f);
}

TEST_CASE ("Build messages name the part and where it is", "[schematic]")
{
    using namespace SchematicModel;

    // Each problem names its part and where, one row per part.
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

    // Labelled parts by label, unlabelled by type.
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

    // An output shorted to ground is reported by name (the net takes ground's name, which
    // would otherwise leave "out" an empty node and a singular matrix).
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

    // The same with the Ground drawn first: net naming must still give the shared net
    // ground's name.
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

    // Two terminals shorted together are reported, not left as a singular matrix.
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

    // An input shorted to ground: the net keeps ground's name, so the input node is empty.
    // Nothing is singular and every sample would be zero, so the build must refuse it.
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

    // The headline reaches the console too.
    CHECK (! result.diagnostics.empty());
}

TEST_CASE ("The same sheet with the ground moved off the input builds", "[schematic]")
{
    using namespace SchematicModel;

    // The control: the same drawing with ground on the output side must build.
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

    // And it passes half its input.
    auto& circuit = *result.circuits[0];
    float peak = 0.0f;

    for (int i = 0; i < 480; ++i)
        peak = juce::jmax (peak, std::abs (circuit.process (1.0f)));

    CHECK (peak == Catch::Approx (0.5f).margin (0.01));
}

TEST_CASE ("A bare letter names a part", "[plugin]")
{
    // A bare letter arms a part; a modified one acts. D is the diode, so Delete is X.
    runWithinPluginEditor ([] (PluginProcessor& plugin) {
        using namespace SchematicModel;
        using Celine::SchematicCanvas;

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

        // S picks select, W the wire.
        canvas->setTool (SchematicCanvas::Tool::Delete);
        CHECK (canvas->keyPressed (juce::KeyPress ('s')));
        CHECK (canvas->getTool() == SchematicCanvas::Tool::Select);

        CHECK (canvas->keyPressed (juce::KeyPress ('w')));
        CHECK (canvas->getTool() == SchematicCanvas::Tool::Wire);

        // X, not D.
        CHECK (canvas->keyPressed (juce::KeyPress ('x')));
        CHECK (canvas->getTool() == SchematicCanvas::Tool::Delete);

        // F frames the drawing without changing the tool or arming a part.
        CHECK (canvas->keyPressed (juce::KeyPress ('f')));
        CHECK (canvas->getTool() == SchematicCanvas::Tool::Delete);
    });
}

TEST_CASE ("Cloning a part carries its properties", "[schematic]")
{
    using namespace SchematicModel;

    // Middle-click clones value, model, label and settings, not just the type.
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

    // Taken by value, as cloneElement does: adding can reallocate the element list.
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

    // ...but not its id or position.
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

    // Clamped rather than refused.
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

    // Parts without a control say so.
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

    // A clone carries its knob position.
    Element clone;
    reloaded.getElements()[0].copyPropertiesTo (clone);
    CHECK (clone.controlPosition == Catch::Approx (0.31));

    // A sheet without stored positions reads pots as centred.
    auto tree = sheet.toValueTree();
    tree.getChild (0).removeProperty ("controlPosition", nullptr);

    Schematic older;
    older.restoreFromValueTree (tree);
    CHECK (older.getElements()[0].controlPosition == Catch::Approx (0.5));
}

TEST_CASE ("A knob follows its part when the control slots shift", "[editor]")
{
    using namespace SchematicModel;

    // Controls map onto knob slots in order, so inserting one that sorts first shifts the
    // others. Each slot must follow its part, not keep the old value.
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

        // Move them, as the strip does.
        knob (0)->setValueNotifyingHost (0.2f);
        knob (1)->setValueNotifyingHost (0.8f);

        // The parts remember the positions.
        plugin.adoptControlPositions();
        CHECK (sheet.findElement (volume)->controlPosition == Catch::Approx (0.2).margin (1.0e-3));
        CHECK (sheet.findElement (tone)->controlPosition == Catch::Approx (0.8).margin (1.0e-3));

        // Add one that sorts ahead of both.
        const auto gain = addPot ("Gain", -1);
        sheet.findElement (gain)->controlPosition = 0.65;
        sheet.addWire (pin (gain, 1), pin (gain, 2));

        REQUIRE (plugin.rebuild().isValid());

        const auto controls = plugin.getLiveControls();
        REQUIRE (controls.size() == 3);
        CHECK (controls[0].name == "Gain");
        CHECK (controls[1].name == "Volume");
        CHECK (controls[2].name == "Tone");

        // The editor rebuilds the strip via this async callback, so run the loop before
        // checking.
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

    // A dual-gang volume: two cascaded pots, the second 1000x the first so it barely loads
    // it. The gain is about the product of the two positions, so it shows both moved.
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

    // Both fully open: everything through (one track alone would read about 0.5).
    CHECK (gainWith (1.0f) == Catch::Approx (1.0).margin (0.01));

    // Both at half: a quarter.
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

        // Both parts store the position.
        plugin.adoptControlPositions();
        CHECK (sheet.findElement (a)->controlPosition == Catch::Approx (0.3).margin (1.0e-3));
        CHECK (sheet.findElement (b)->controlPosition == Catch::Approx (0.3).margin (1.0e-3));
    });
}

TEST_CASE ("A rebuilt switch shows its throw straight away", "[editor]")
{
    using namespace SchematicModel;

    // The strip's switches have no attachment, so refresh() must set them from the
    // parameter at once (otherwise they flicker until the next timer tick).
    runWithinPluginEditor ([] (PluginProcessor& plugin)
    {
        auto& sheet = plugin.getSchematic();
        sheet.clear();

        // No wire's corner lands on another pin or centre (that would connect them).
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

        Celine::ControlStrip strip (plugin);
        strip.refresh();

        // Whatever refresh() left on screen, before any timer runs.
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


TEST_CASE ("A new switch action keeps the throw the drawing shows", "[editor]")
{
    using namespace SchematicModel;

    // An ON-ON-ON on throw B sits at 0.5. Changed to ON-ON in the inspector, the part is
    // kept on B -- but 0.5 is throw A of an ON-ON. After Rebuild the strip and the
    // circuit must be on B too, not on whatever the old number now means.
    runWithinPluginEditor ([] (PluginProcessor& plugin)
    {
        auto& sheet = plugin.getSchematic();
        sheet.clear();

        const auto sw = sheet.addElement (ElementType::Spdt, 0, 0);
        sheet.findElement (sw)->label = "Voice";
        sheet.findElement (sw)->switchAction = SwitchAction::OnOnOn;
        sheet.findElement (sw)->switchPosition = 1; // throw B
        sheet.findElement (sw)->closed = false;

        // A divider, input to output, joined by coincident pins; the switch's common hangs
        // off the input and its throws are left open, so they may move when the action
        // changes without breaking the build.
        sheet.addElement (ElementType::Input, -20, 0);                    // pin at (-18, 0)
        const auto feed = sheet.addElement (ElementType::Resistor, -18, 2); // (-18, 0)-(-18, 4)
        sheet.addElement (ElementType::Output, -16, 4);                   // pin at (-18, 4)
        const auto load = sheet.addElement (ElementType::Resistor, -18, 6); // (-18, 4)-(-18, 8)
        sheet.addElement (ElementType::Ground, -18, 10);                  // pin at (-18, 8)
        sheet.findElement (feed)->value = 10000.0;
        sheet.findElement (load)->value = 10000.0;

        const auto common = sheet.findElement (sw)->getPinPosition (0);
        sheet.addWire ({ -18, 0 }, { -4, 0 });
        sheet.addWire ({ -4, 0 }, { -4, common.y });
        sheet.addWire ({ -4, common.y }, common);

        {
            const auto built = plugin.rebuild();
            INFO (built.error);
            REQUIRE (built.isValid());
        }
        REQUIRE (plugin.getLiveControls().size() == 1);

        Celine::ControlStrip strip (plugin);
        strip.refresh();

        auto* parameter = plugin.apvts.getParameter (PluginProcessor::getControlParameterId (0));
        REQUIRE (parameter != nullptr);
        REQUIRE (Element::positionForValue (parameter->getValue(), 3) == 1);

        // Through the inspector's own Action box, as a user would.
        Celine::ElementInspector inspector;
        inspector.setSize (Celine::Theme::inspectorWidth, 600);
        inspector.setElement (sheet.findElement (sw));

        std::function<juce::ComboBox* (juce::Component&)> findActionBox =
            [&findActionBox] (juce::Component& c) -> juce::ComboBox*
        {
            for (auto* child : c.getChildren())
            {
                if (auto* box = dynamic_cast<juce::ComboBox*> (child); box != nullptr
                    && box->getNumItems() == numSwitchActions && box->getItemText (0) == "ON-ON")
                    return box;

                if (auto* found = findActionBox (*child))
                    return found;
            }

            return nullptr;
        };

        auto* actionBox = findActionBox (inspector);
        REQUIRE (actionBox != nullptr);
        actionBox->setSelectedId (static_cast<int> (SwitchAction::OnOn) + 1, juce::sendNotificationSync);

        const auto* part = sheet.findElement (sw);
        REQUIRE (part->switchAction == SwitchAction::OnOn);
        REQUIRE (part->getThrowAt (part->switchPosition) == 1); // still drawn on B

        REQUIRE (plugin.rebuild().isValid());
        strip.refresh();

        const auto controls = plugin.getLiveControls();
        REQUIRE (controls.size() == 1);
        const float value = parameter->getValue();

        CHECK (Element::positionForValue (value, part->getSwitchPositions()) == part->switchPosition);

        // And the circuit makes the throw the drawing shows: throw B's contact is the
        // second, and it alone is closed at this value.
        REQUIRE (controls[0].contacts.size() == 2);
        CHECK (! controls[0].contacts[0].isClosedAt (value));
        CHECK (controls[0].contacts[1].isClosedAt (value));
    });
}

TEST_CASE ("A pot's wiper is drawn where its knob is", "[schematic]")
{
    using namespace SchematicModel;

    // The pot symbol's wiper follows the knob.
    auto render = [] (double position)
    {
        Element element;
        element.type = ElementType::Potentiometer;
        element.value = 100000.0;
        element.controlPosition = position;

        juce::Image image (juce::Image::ARGB, 120, 120, true);
        juce::Graphics g (image);

        Celine::SymbolPainter painter;
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

    // A divider whose halves are joined only by two labelled nodes, twenty squares apart.
    // The ratio proves the label joins them.
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

    // The other half, joined only by name.
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

    // Two equal resistors: half out, only if MID joined them.
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

    // An unlabelled node must be reported, not silently ignored.
    Schematic sheet;

    auto place = [&sheet] (ElementType type, int x, int y, double value = 0.0)
    {
        const auto id = sheet.addElement (type, x, y);
        sheet.findElement (id)->value = value;
        return id;
    };

    auto pin = [&sheet] (int id, int p) { return sheet.findElement (id)->getPinPosition (p); };

    // A divider: input through R to the output, with a load to ground.
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
    REQUIRE (result.isValid());   // a warning, not an error: it still builds

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

        // Ids, not indices: deleting the first doesn't renumber the second.
        const int first = sheet.getWires()[0].id;
        const int second = sheet.getWires()[1].id;
        CHECK (first != second);
        CHECK (sheet.findWire (first) != nullptr);

        Celine::SchematicCanvas canvas (sheet);
        canvas.setSize (600, 400);
        canvas.zoomToFit();

        // A click on the wire selects it; one well clear doesn't.
        const auto onWire = canvas.pixelAt ({ 5.0f, 0.0f });
        const auto offWire = canvas.pixelAt ({ 5.0f, 3.0f });

        CHECK (canvas.wireAt (onWire) == first);
        CHECK (canvas.wireAt (offWire) == -1);

        // Moving takes both ends.
        canvas.setSelectedWires ({ second });
        sheet.moveWire (second, { 4, -2 });
        const auto* moved = sheet.findWire (second);
        REQUIRE (moved != nullptr);
        CHECK (moved->a == juce::Point<int> (4, 4));
        CHECK (moved->b == juce::Point<int> (14, 4));

        // Deleting the selection removes that wire only.
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

        // Two resistors with a long wire between them.
        const auto left = sheet.addElement (ElementType::Resistor, 0, 0);
        const auto right = sheet.addElement (ElementType::Resistor, 16, 0);
        sheet.findElement (left)->value = 1000.0;
        sheet.findElement (right)->value = 1000.0;

        auto pin = [&sheet] (int id, int p) { return sheet.findElement (id)->getPinPosition (p); };
        sheet.addWire (pin (left, 1), pin (right, 1));
        const int wire = sheet.getWires().front().id;

        Celine::SchematicCanvas canvas (sheet);
        canvas.setSize (700, 420);
        canvas.zoomToFit();

        // Every point along the middle finds the wire, never a resistor.
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
        // Perpendicular wires don't merge.
        Schematic sheet;
        sheet.addWire ({ 0, 0 }, { 6, 0 });
        sheet.addWire ({ 6, 0 }, { 6, 4 });

        CHECK (sheet.mergeCollinearWires() == 0);
        CHECK (sheet.getWires().size() == 2);
    }

    SECTION ("a T keeps its stem, and keeps its dot")
    {
        // A bar drawn as two halves with a stem at the seam: the halves join, the stem stays
        // separate, and the junction dot remains (findJunctions also counts ends inside
        // another wire).
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
        // Three in a row merge regardless of order.
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

    // Only the coordinate along the wire is taken, so it can't become diagonal.
    REQUIRE (sheet.resizeWireEnd (id, 1, { 9, 5 }));
    CHECK (sheet.findWire (id)->b == juce::Point<int> (9, 0));
    CHECK (sheet.findWire (id)->a == juce::Point<int> (0, 0));

    // Onto the other end is refused: zero length.
    CHECK (! sheet.resizeWireEnd (id, 1, { 0, 4 }));
    CHECK (sheet.findWire (id)->b == juce::Point<int> (9, 0));

    // No change reports false.
    CHECK (! sheet.resizeWireEnd (id, 1, { 9, 0 }));

    // The other end moves independently.
    REQUIRE (sheet.resizeWireEnd (id, 0, { -3, 0 }));
    CHECK (sheet.findWire (id)->a == juce::Point<int> (-3, 0));

    // A vertical wire takes y from the pointer and keeps x.
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

    // A fixed height (3 grid lines; the drawn tag is two squares); it only grows sideways.
    CHECK (narrow.getHeight() == 3);
    CHECK (wide.getHeight() == 3);
    CHECK (sheet.findElement (longName)->getNodeBounds().getHeight() == 2);

    // The tag's point lands on the pin, and the tip is clickable.
    const auto pin = sheet.findElement (longName)->getPinPosition (0);
    CHECK (pin == juce::Point<int> (2, 20));
    CHECK (wide.contains (pin));

    // Clickable along the whole name.
    CHECK (sheet.findElementAt ({ -4, 20 }) == longName);
    CHECK (sheet.findElementAt ({ -4, 0 }) != shortName);

    // Rotated, the tag turns with the part.
    sheet.findElement (longName)->orientation = 1;
    const auto turned = sheet.getElementBounds (*sheet.findElement (longName));

    INFO ("turned " << turned.toString());
    CHECK (turned.getHeight() == wide.getWidth());
    CHECK (turned.getWidth() == wide.getHeight());
}

TEST_CASE ("A wire stays clickable when the sheet is zoomed out", "[editor]")
{
    using namespace SchematicModel;

    // A big sheet zoomed to fit, a few pixels per square: the wire tolerance must hold in
    // pixels.
    Schematic sheet;
    sheet.addWire ({ 0, 0 }, { 120, 0 });
    sheet.addWire ({ 0, 0 }, { 0, 80 });
    const int wire = sheet.getWires().front().id;

    Celine::SchematicCanvas canvas (sheet);
    canvas.setSize (600, 400);
    canvas.zoomToFit();
    canvas.setTool (Celine::SchematicCanvas::Tool::Select);

    const auto centre = canvas.pixelAt ({ 60.0f, 0.0f });
    const auto pixelsPerSquare = canvas.pixelAt ({ 61.0f, 0.0f }).x - centre.x;

    // A few pixels off the line still finds it.
    INFO ("pixels per grid square: " << pixelsPerSquare);
    CHECK (pixelsPerSquare < 10.0f);            // genuinely zoomed out
    CHECK (canvas.wireAt (centre) == wire);
    CHECK (canvas.wireAt (centre.translated (0.0f, 4.0f)) == wire);

    // And the click lands through the real handler.
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

    Celine::SchematicCanvas canvas (sheet);
    canvas.setSize (600, 400);
    canvas.zoomToFit();
    canvas.setTool (Celine::SchematicCanvas::Tool::Select);

    // Drags a box between two grid points and returns what it caught.
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
        // Through the middle of the horizontal wire, missing both ends: a crossing box takes
        // it.
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
        // Around the horizontal one only; the vertical is crossed, not enclosed, so it is
        // left.
        const auto caught = dragBox ({ -4.0f, -4.0f }, { 24.0f, 4.0f });

        REQUIRE (caught.size() == 1);
        CHECK (caught.front() == across);
    }
}

TEST_CASE ("A centre-tapped transformer's strays are sized the right way up", "[schematic]")
{
    using namespace SchematicModel;

    // A push-pull output transformer puts the plates on the tapped secondary and the
    // speaker on the primary, so the primary is the low-impedance winding. Stray sizing
    // must follow the actual winding impedances, or the amp loses 17 dB. Its own bench,
    // since makeTransformerBench drives the primary.
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

        // Mirrored so the tapped winding faces left, towards the drive.
        transformer->mirrored = true;

        auto pin = [&sheet] (int id, int index) { return sheet.findElement (id)->getPinPosition (index); };

        const auto speakerA = pin (xf, 0), speakerB = pin (xf, 1);
        const auto plateA = pin (xf, 2), tap = pin (xf, 3), plateB = pin (xf, 4);

        // Driven across the whole tapped winding from a matched source.
        const auto rs = sheet.addElement (ElementType::Resistor, plateA.x - 4, plateA.y);
        sheet.findElement (rs)->orientation = 1;
        sheet.findElement (rs)->value = plateLoad;
        sheet.addElement (ElementType::Input, plateA.x - 8, plateA.y);
        sheet.addWire (pin (rs, 0), plateA);
        sheet.addElement (ElementType::Ground, plateB.x, plateB.y + 2);

        // The tap (B+ in an amp, an AC ground) needs a path to ground or the matrix is
        // singular; 1M does that without loading.
        const auto bleeder = sheet.addElement (ElementType::Resistor, tap.x - 6, tap.y);
        sheet.findElement (bleeder)->orientation = 1;
        sheet.findElement (bleeder)->value = 1.0e6;
        sheet.addWire (tap, pin (bleeder, 0));
        sheet.addElement (ElementType::Ground, pin (bleeder, 1).x, pin (bleeder, 1).y + 2);

        // The speaker hangs off the primary; the output reads across it.
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

    // A little copper loss in the midband, nowhere near 17 dB.
    CHECK (realMid > idealMid * 0.80f);
    CHECK (realMid < idealMid * 1.02f);

    // And bass rolls off where the magnetising inductance gives up.
    const float realLow = gainAt (*real.circuits[0], 25.0);
    INFO ("real 25 Hz " << realLow << " against 1 kHz " << realMid);
    CHECK (realLow < realMid * 0.95f);
}

TEST_CASE ("The wheel scrolls the control strip rather than turning a knob", "[editor]")
{
    // The wheel doesn't turn knobs in the strip (including Input and Output): it scrolls
    // the strip instead. With the wheel disabled, Slider passes the event up to the
    // Viewport.
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

    // Two wires connect only where a registered point (a pin or a wire end) sits on both.
    // Merging collinear wires removes ends, so a four-way junction whose horizontals and
    // verticals both merge would become an unconnected crossing. Merges must not do that.
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

        // A resistor on each leg, so the nets are observable.
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
        // Nothing else at the join: two wires end to end become one.
        Schematic sheet;
        sheet.addWire ({ 0, 0 }, { 4, 0 });
        sheet.addWire ({ 4, 0 }, { 8, 0 });

        CHECK (sheet.mergeCollinearWires() == 1);
        REQUIRE (sheet.getWires().size() == 1);
        CHECK (sheet.getWires().front().contains ({ 4, 0 }));
    }

    SECTION ("a wire running through the seam blocks the merge")
    {
        // A vertical passing through the seam (not ending there) would be stranded by a merge.
        Schematic sheet;
        sheet.addWire ({ 0, 0 }, { 4, 0 });
        sheet.addWire ({ 4, 0 }, { 8, 0 });
        sheet.addWire ({ 4, -4 }, { 4, 4 });

        CHECK (sheet.mergeCollinearWires() == 0);
        CHECK (sheet.getWires().size() == 3);
    }

    SECTION ("a pin on the seam does not block it")
    {
        // A pin registers the point itself, so the bar can merge.
        Schematic sheet;
        sheet.addWire ({ 0, 0 }, { 4, 0 });
        sheet.addWire ({ 4, 0 }, { 8, 0 });

        const auto id = sheet.addElement (ElementType::Resistor, 4, 2);
        sheet.findElement (id)->value = 1000.0;
        REQUIRE (sheet.findElement (id)->getPinPosition (0) == juce::Point<int> { 4, 0 });

        CHECK (sheet.mergeCollinearWires() == 1);
        REQUIRE (sheet.getWires().size() == 1);

        // And the part is still on the wire.
        const auto nets = sheet.extractNets();
        CHECK (netOf (sheet, nets, id, 0) >= 0);
    }
}

TEST_CASE ("A wire's hit target is the same size however far you are zoomed in", "[editor]")
{
    using namespace SchematicModel;

    // The hit tolerance is in pixels, so it doesn't change with zoom. Measured, so the test
    // checks behaviour.
    auto reachAt = [] (int extent)
    {
        Schematic sheet;
        sheet.addWire ({ 0, 0 }, { extent, 0 });
        const int wire = sheet.getWires().front().id;

        Celine::SchematicCanvas canvas (sheet);
        canvas.setSize (900, 600);
        canvas.setTool (Celine::SchematicCanvas::Tool::Select);
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

    // Genuinely different zooms.
    REQUIRE (zoomedInSquare > zoomedOutSquare * 4.0f);

    // The target is the same size on screen.
    CHECK (std::abs (zoomedInReach - zoomedOutReach) <= 1.0f);

    // Wide enough to hit, narrow enough to miss beside it.
    CHECK (zoomedInReach >= 5.0f);
    CHECK (zoomedInReach <= 9.0f);
}

TEST_CASE ("A selected wire's handles do not guard the space around it", "[editor]")
{
    using namespace SchematicModel;

    // Clicking beside a selected wire must deselect, not grab an end handle: the handle's
    // hit area matches the drawn diamond.
    auto stillSelectedAfterClickAt = [] (float dx, float dy)
    {
        Schematic sheet;
        sheet.addWire ({ 0, 0 }, { 10, 0 });
        const int id = sheet.getWires().front().id;

        Celine::SchematicCanvas canvas (sheet);
        canvas.setSize (900, 600);
        canvas.setTool (Celine::SchematicCanvas::Tool::Select);
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

    // Well inside a grid square in every direction.
    CHECK (along < square * 0.5f);
    CHECK (beside < square * 0.5f);
    CHECK (diagonal < square * 0.5f);

    // A diamond: the diagonal reach is shorter than along an axis.
    CHECK (diagonal <= along);
}

TEST_CASE ("Undo restores the drawing and leaves the knobs alone", "[editor]")
{
    // Undo restores the drawing, but pot positions follow the parameters (the live truth),
    // so they must not be rewound.
    runWithinPluginEditor ([] (PluginProcessor& plugin)
    {
        auto* editor = plugin.getActiveEditor();
        REQUIRE (editor != nullptr);

        // The canvas, for the editor's undo callbacks.
        Celine::SchematicCanvas* canvas = nullptr;

        const std::function<void (juce::Component&)> walk = [&] (juce::Component& parent)
        {
            for (auto* child : parent.getChildren())
            {
                if (auto* found = dynamic_cast<Celine::SchematicCanvas*> (child))
                    canvas = found;

                walk (*child);
            }
        };

        walk (*editor);
        REQUIRE (canvas != nullptr);
        REQUIRE (canvas->onSchematicChanged != nullptr);
        REQUIRE (canvas->onUndoRequested != nullptr);

        auto& sheet = plugin.getSchematic();

        // The first drawn control and its parameter.
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

        // A distinctive value, agreed by both.
        plugin.apvts.getParameter (PluginProcessor::getControlParameterId (0))->setValueNotifyingHost (0.25f);
        plugin.adoptControlPositions();

        // An edit to undo, recorded as the canvas records one.
        const auto moved = sheet.addElement (SchematicModel::ElementType::Resistor, 40, 40);
        sheet.findElement (moved)->value = 1000.0;
        canvas->onSchematicChanged();

        // Turn the knob: the parameter moves and the drawing follows, with no history step.
        plugin.apvts.getParameter (PluginProcessor::getControlParameterId (0))->setValueNotifyingHost (0.80f);
        plugin.adoptControlPositions();

        canvas->onUndoRequested();

        const auto* control = sheet.findElement (controlId);
        REQUIRE (control != nullptr);

        INFO ("parameter " << knob->load() << ", drawing " << control->getControlPosition());

        // The drawing came back...
        CHECK (sheet.findElement (moved) == nullptr);

        // ...and the knob didn't move.
        CHECK (knob->load() == Catch::Approx (0.80f).margin (0.001));
        CHECK (control->getControlPosition() == Catch::Approx (knob->load()).margin (0.001));
    });
}

//==============================================================================
TEST_CASE ("A sheet with duplicated or missing ids loads with every part reachable", "[schematic]")
{
    using namespace SchematicModel;

    // Every lookup returns the first match, so duplicated ids would leave a part
    // unreachable, and id 0 collides with the "nothing inspected" sentinel. Hand-edited or
    // truncated files can carry either; loading must repair them.
    Schematic sheet;
    const int r1 = sheet.addElement (ElementType::Resistor, 0, 0);
    const int r2 = sheet.addElement (ElementType::Resistor, 8, 0);
    const int r3 = sheet.addElement (ElementType::Capacitor, 16, 0);
    sheet.addWire ({ 0, 8 }, { 8, 8 });
    sheet.addWire ({ 8, 8 }, { 16, 8 });

    REQUIRE (r1 != r2);
    REQUIRE (r2 != r3);

    auto tree = sheet.toValueTree();

    // Corrupt it two ways: a duplicate, and a missing id (which reads as 0).
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
            CHECK (seen.insert (item.id).second); // no two share one
            CHECK (lookup (item.id) == &item);    // each is found by its id
        }
    };

    idsAreSoundAndReachable (reloaded.getElements(),
                             [&] (int id) { return reloaded.findElement (id); });
    idsAreSoundAndReachable (reloaded.getWires(),
                             [&] (int id) { return reloaded.findWire (id); });

    // New parts must not collide with repaired ids: the counter is advanced too.
    const int fresh = reloaded.addElement (ElementType::Resistor, 24, 0);

    for (const auto& element : reloaded.getElements())
        if (&element != reloaded.findElement (fresh))
            CHECK (element.id != fresh);
}

//==============================================================================
TEST_CASE ("A healthy sheet keeps its ids through a save and load", "[schematic]")
{
    using namespace SchematicModel;

    // The repair must not renumber ids that are already unique and positive.
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

    // Always present, unlike build options (omitted at their defaults).
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

    // Numeric: "0.10.0" is newer than "0.9.79".
    CHECK (cmp ("0.10.0", "0.9.79") > 0);
    CHECK (cmp ("0.9.79", "0.10.0") < 0);

    CHECK (cmp ("2.0.0", "1.99.99") > 0);
    CHECK (cmp ("1.2.3-rc1", "1.2.3") == 0);  // a suffix is not a version bump
    CHECK (cmp ("", "0.0.0") == 0);           // nothing is not newer than anything
}

TEST_CASE ("A sheet from a newer build is flagged at import", "[plugin]")
{
    // Set by the import itself; the editor reads it in schematicChangedExternally().
    PluginProcessor plugin;

    auto document = plugin.createDocument();
    document.setProperty (PluginProcessor::documentVersionProperty, "99.0.0", nullptr);

    PluginProcessor loaded;
    CHECK (! loaded.wasDocumentFromNewerBuild());     // nothing restored yet

    REQUIRE (loaded.restoreDocument (document));
    CHECK (loaded.wasDocumentFromNewerBuild());

    // Restoring an older document clears it.
    auto ordinary = plugin.createDocument();
    REQUIRE (loaded.restoreDocument (ordinary));
    CHECK (! loaded.wasDocumentFromNewerBuild());
}

TEST_CASE ("An older or unversioned sheet is not flagged", "[plugin]")
{
    // A sheet without a version predates the field, so it's older: no warning.
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

// The version notice is not a build message; the console stays clear of it.
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
    // restoreDocument() rebuilds internally and discards the BuildResult, so load warnings
    // must survive for the editor's following rebuild to report. Checked with a missing
    // model.
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

    // Reported once, not on every Rebuild.
    bool again = false;

    for (const auto& d : loaded.rebuild().diagnostics)
        if (d.text.contains ("lucas:not-in-this-build:7f3a"))
            again = true;

    CHECK (! again);
}


TEST_CASE ("A tube added in a newer build degrades safely", "[plugin][schematic]")
{
    using namespace SchematicModel;

    // A sheet with a valve from a newer build: it builds with a fallback, names the
    // missing model, and keeps the reference through the next save.
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

TEST_CASE ("The editor opens at the size it was left at", "[ui]")
{
    // The stored size must be read before setResizeLimits, which fires resized() and
    // overwrites it with the minimum.
    juce::ScopedJuceInitialiser_GUI gui;

    PluginProcessor plugin;
    plugin.editorWidth = 1280;
    plugin.editorHeight = 820;

    std::unique_ptr<juce::AudioProcessorEditor> editor (plugin.createEditor());
    REQUIRE (editor != nullptr);

    CHECK (editor->getWidth() == 1280);
    CHECK (editor->getHeight() == 820);
}
