#include <Schematic/Element.h>
#include <ui/SchematicSymbols.h>
#include <CelineUI/EmbeddedAssets.h>
#include <ui/SymbolArtwork.h>
#include <CelineUI/Theme.h>
#include <catch2/catch_test_macros.hpp>

using namespace SchematicModel;
using namespace Celine;

TEST_CASE ("Every part resolves to artwork that exists", "[artwork][gui]")
{
    // A missing asset silently draws a part as just its pins. Hyphenated names are the
    // risk: JUCE's derived identifier drops the hyphen.
    for (const auto& name : getAllSymbolAssetNames())
    {
        Element probe;
        probe.type = ElementType::Resistor;

        INFO ("asset " << name);

        // Through an element, so this tests the canvas's own lookup.
        const SymbolArtwork* artwork = nullptr;

        for (int i = 0; i < numElementTypes && artwork == nullptr; ++i)
        {
            probe.type = static_cast<ElementType> (i);

            for (int model = 0; model < 8; ++model)
            {
                probe.modelIndex = model;

                for (const bool flag : { false, true })
                {
                    probe.polarised = flag;
                    probe.closed = flag;

                    // A changeover's drawing follows its action and lever.
                    for (int action = 0; action < numSwitchActions && artwork == nullptr; ++action)
                        for (int position = 0; position < 3 && artwork == nullptr; ++position)
                        {
                            probe.switchAction = static_cast<SwitchAction> (action);
                            probe.switchPosition = position;

                            if (getSymbolAssetName (probe) == name)
                                artwork = getSymbolArtwork (probe);
                        }

                    if (artwork != nullptr)
                        break;
                }

                if (artwork != nullptr)
                    break;
            }
        }

        REQUIRE (artwork != nullptr);
        CHECK (! artwork->isEmpty());
    }
}

TEST_CASE ("Every element type draws something", "[artwork][gui]")
{
    for (int i = 0; i < numElementTypes; ++i)
    {
        const auto type = static_cast<ElementType> (i);

        Element element;
        element.type = type;

        INFO (getElementInfo (type).name);

        // Text, Rectangle and Node have no artwork, and no name may resolve to nothing.
        if (type == ElementType::Text || type == ElementType::Rectangle
            || type == ElementType::Node)
        {
            CHECK (getSymbolAssetName (element).isEmpty());
            CHECK (getSymbolArtwork (element) == nullptr);
            continue;
        }

        CHECK (getSymbolAssetName (element).isNotEmpty());

        const auto* artwork = getSymbolArtwork (element);
        REQUIRE (artwork != nullptr);
        REQUIRE (! artwork->isEmpty());
    }
}

TEST_CASE ("Artwork is authored at the schematic's scale", "[artwork][gui]")
{
    // Geometry is in grid squares, so a file drawn to another convention lands the wrong
    // size with misplaced terminals. Says which file.
    for (int i = 0; i < numElementTypes; ++i)
    {
        Element element;
        element.type = static_cast<ElementType> (i);

        const auto* artwork = getSymbolArtwork (element);

        if (artwork == nullptr)
            continue;

        // Min and max by hand: Rectangle::getUnion drops zero-height lines (see
        // Schematic::getElementBounds).
        float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
        bool first = true;

        for (const auto& shape : artwork->shapes)
        {
            const auto b = shape.path.getBounds();

            minX = first ? b.getX() : juce::jmin (minX, b.getX());
            minY = first ? b.getY() : juce::jmin (minY, b.getY());
            maxX = first ? b.getRight() : juce::jmax (maxX, b.getRight());
            maxY = first ? b.getBottom() : juce::jmax (maxY, b.getBottom());
            first = false;
        }

        REQUIRE (! first);
        const juce::Rectangle<float> bounds (minX, minY, maxX - minX, maxY - minY);

        INFO (getSymbolAssetName (element) << ".svg spans " << bounds.toString() << " grid squares");

        // Visible, and within one part's reach (a pentode spans six squares from its centre).
        CHECK (juce::jmax (bounds.getWidth(), bounds.getHeight()) >= 1.0f);
        CHECK (bounds.getX() >= -8.0f);
        CHECK (bounds.getY() >= -8.0f);
        CHECK (bounds.getRight() <= 8.0f);
        CHECK (bounds.getBottom() <= 8.0f);

        // Centred on the part.
        CHECK (std::abs (bounds.getCentreX()) <= 3.0f);
        CHECK (std::abs (bounds.getCentreY()) <= 3.0f);
    }
}

TEST_CASE ("Stroke weights survive the round trip through SVG", "[artwork][gui]")
{
    // Line weight is a multiplier on the canvas stroke: 1.2 is normal, so a capacitor's
    // 1.6 plates read 1.6 / 1.2.
    Element capacitor;
    capacitor.type = ElementType::Capacitor;

    const auto* artwork = getSymbolArtwork (capacitor);
    REQUIRE (artwork != nullptr);

    bool sawNormal = false;
    bool sawHeavy = false;

    for (const auto& shape : artwork->shapes)
    {
        if (shape.filled)
            continue;

        sawNormal = sawNormal || std::abs (shape.strokeScale - 1.0f) < 0.02f;
        sawHeavy = sawHeavy || std::abs (shape.strokeScale - 1.6f / 1.2f) < 0.02f;
    }

    CHECK (sawNormal);
    CHECK (sawHeavy);

    // A filled shape reads as filled.
    Element diode;
    diode.type = ElementType::Diode;

    const auto* diodeArt = getSymbolArtwork (diode);
    REQUIRE (diodeArt != nullptr);

    int filled = 0;

    for (const auto& shape : diodeArt->shapes)
        filled += shape.filled ? 1 : 0;

    INFO ("the diode's triangle is the filled one");
    CHECK (filled == 1);
}

TEST_CASE ("Every part's palette swatch fits inside its row", "[artwork][gui]")
{
    // The palette clips each symbol to a square swatch, so an oversized symbol is silently
    // cut off. Render every type into a palette-sized swatch and require its outer ring of
    // pixels to be empty.
    constexpr int swatch = Theme::paletteRowHeight - 4;

    for (int i = 0; i < numElementTypes; ++i)
    {
        const auto type = static_cast<ElementType> (i);

        INFO ("part " << getElementInfo (type).name);

        juce::Image image (juce::Image::ARGB, swatch, swatch, true);

        {
            juce::Graphics g (image);
            SymbolPainter::drawPreview (g, type,
                                        juce::Rectangle<float> (0.0f, 0.0f, (float) swatch,
                                                                (float) swatch),
                                        juce::Colours::white);
        }

        const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);

        auto inked = [&] (int x, int y) { return pixels.getPixelColour (x, y).getAlpha() > 40; };

        int onBorder = 0;

        for (int x = 0; x < swatch; ++x)
        {
            onBorder += inked (x, 0) ? 1 : 0;
            onBorder += inked (x, swatch - 1) ? 1 : 0;
        }

        for (int y = 0; y < swatch; ++y)
        {
            onBorder += inked (0, y) ? 1 : 0;
            onBorder += inked (swatch - 1, y) ? 1 : 0;
        }

        CHECK (onBorder == 0);

        // And something must have been drawn.
        int total = 0;

        for (int y = 0; y < swatch; ++y)
            for (int x = 0; x < swatch; ++x)
                total += inked (x, y) ? 1 : 0;

        CHECK (total > 8);
    }
}

TEST_CASE ("The VST Compatible Logo is embedded and findable", "[artwork][gui]")
{
    // The About box's format marks (Steinberg's guidelines require the VST logo there).
    // Assets::drawable() matches on the original filename and returns null on a miss.
    // The VST logo is shown as supplied, never tinted.
    for (const auto* name : { "vst-compatible.png", "format-au.svg",
                              "format-clap.png", "format-lv2.svg" })
    {
        INFO ("asset: " << name);

        const auto logo = Assets::drawable (name);
        REQUIRE (logo != nullptr);

        const auto bounds = logo->getDrawableBounds();
        INFO ("  " << name << " is " << bounds.getWidth() << " x " << bounds.getHeight());

        // Real artwork, not an empty drawable.
        CHECK (bounds.getWidth() > 20.0f);
        CHECK (bounds.getHeight() > 20.0f);
    }
}

TEST_CASE ("A diode's symbol follows its heading, not its name", "[artwork][gui]")
{
    // The dropdown group picks the diode drawing: Zener, Schottky and LED have their own,
    // silicon and germanium share the plain one.
    const auto artworkFor = [] (int model)
    {
        Element diode;
        diode.type = ElementType::Diode;
        diode.modelIndex = model;
        return getSymbolArtwork (diode);
    };

    const auto choices = getModelChoices (ElementType::Diode);
    std::map<juce::String, const SymbolArtwork*> byGroup;

    for (int model = 0; model < choices.size(); ++model)
    {
        const auto group = getModelGroup (ElementType::Diode, model);
        const auto* artwork = artworkFor (model);
        REQUIRE (artwork != nullptr);

        INFO (choices[model] << " under " << group);

        if (const auto it = byGroup.find (group); it != byGroup.end())
            CHECK (artwork == it->second);
        else
            byGroup[group] = artwork;
    }

    REQUIRE (byGroup.count ("Silicon") == 1);
    REQUIRE (byGroup.count ("Germanium") == 1);
    REQUIRE (byGroup.count ("Schottky") == 1);
    REQUIRE (byGroup.count ("LED") == 1);
    REQUIRE (byGroup.count ("Zener") == 1);

    CHECK (byGroup["Germanium"] == byGroup["Silicon"]);

    const std::set<const SymbolArtwork*> distinct { byGroup["Silicon"], byGroup["Schottky"],
                                                    byGroup["LED"], byGroup["Zener"] };
    CHECK (distinct.size() == 4);
}

TEST_CASE ("Every symbol is drawn as authored, from its ink", "[artwork]")
{
    // Every file becomes ink covering what its paths cover (give or take stroke caps),
    // with its viewBox in grid units, centred on the part.

    Element resistor;
    resistor.type = ElementType::Resistor;
    const auto* artwork = getSymbolArtwork (resistor);
    REQUIRE (artwork != nullptr);
    CHECK (artwork->viewBox == juce::Rectangle<float> (-6.0f, -6.0f, 12.0f, 12.0f));
    CHECK (! artwork->isEmpty());
    REQUIRE (! artwork->ink.empty());

    // The body's 0.07 fill-opacity becomes a faint run; the outline stays solid.
    bool faint = false, solid = false;
    for (const auto& run : artwork->ink)
    {
        faint = faint || (run.alpha > 0.05f && run.alpha < 0.1f);
        solid = solid || juce::approximatelyEqual (run.alpha, 1.0f);
    }
    CHECK (faint);
    CHECK (solid);

    for (int t = 0; t <= (int) ElementType::PowerAmp; ++t)
    {
        Element e;
        e.type = (ElementType) t;
        const auto* art = getSymbolArtwork (e);

        if (art == nullptr)
            continue;

        INFO (getSymbolAssetName (e));
        REQUIRE (! art->ink.empty());

        juce::Rectangle<float> ink;
        for (const auto& run : art->ink)
            ink = ink.getUnion (run.path.getBounds());

        CHECK (ink.expanded (0.2f).contains (art->bounds.reduced (0.2f)));
        CHECK (art->bounds.expanded (0.2f).contains (ink));
    }
}
