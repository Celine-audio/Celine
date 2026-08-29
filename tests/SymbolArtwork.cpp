#include <Schematic/Element.h>
#include <UI/SchematicSymbols.h>
#include <UI/EmbeddedAssets.h>
#include <UI/SymbolArtwork.h>
#include <UI/Theme.h>
#include <catch2/catch_test_macros.hpp>

using namespace SchematicModel;
using namespace SchematicUI;

TEST_CASE ("Every part resolves to artwork that exists", "[artwork][gui]")
{
    // The failure this guards against is silent and looks like a bug in the
    // drawing rather than a missing file: a part whose asset cannot be found
    // draws as nothing but its pins. It happened once already, to every file
    // with a hyphen in its name, because the C++ identifier JUCE derives from
    // "capacitor-polarised.svg" drops the hyphen rather than replacing it.
    for (const auto& name : getAllSymbolAssetNames())
    {
        Element probe;
        probe.type = ElementType::Resistor;

        INFO ("asset " << name);

        // Reached through an element so the lookup under test is the same one
        // the canvas uses, not a private back door.
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

                    if (getSymbolAssetName (probe) == name)
                    {
                        artwork = getSymbolArtwork (probe);
                        break;
                    }
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

        // Text is its own text, Rectangle a dragged frame, and Node a tag as
        // wide as the name written inside it -- so none of the three has
        // artwork, and none may accidentally acquire a name that resolves to
        // nothing.
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
    // Geometry comes back in grid squares, so a file drawn to some other
    // convention -- a plain 24x24 icon, say -- lands a fifth of the size with
    // its terminals nowhere near the pins. That reads as "the symbols look
    // wrong" rather than as "this file is in the wrong units", so it is worth a
    // check that says which file and why.
    for (int i = 0; i < numElementTypes; ++i)
    {
        Element element;
        element.type = static_cast<ElementType> (i);

        const auto* artwork = getSymbolArtwork (element);

        if (artwork == nullptr)
            continue;

        // Min and max by hand rather than Rectangle::getUnion, for exactly the
        // reason Schematic::getElementBounds spells out: a horizontal line has
        // zero height, JUCE counts a zero-sized rectangle as empty, and getUnion
        // returns the *other* operand when either side is empty. A symbol made
        // only of straight lines therefore unions down to nothing at all.
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

        // Big enough to see, and inside the sheet's idea of one part. The
        // widest thing here is a pentode at six squares from its centre.
        CHECK (juce::jmax (bounds.getWidth(), bounds.getHeight()) >= 1.0f);
        CHECK (bounds.getX() >= -8.0f);
        CHECK (bounds.getY() >= -8.0f);
        CHECK (bounds.getRight() <= 8.0f);
        CHECK (bounds.getBottom() <= 8.0f);

        // Centred on the part, not tucked into a corner of the viewBox -- that
        // is the other way a foreign file goes wrong, and it puts every pin off
        // the symbol.
        CHECK (std::abs (bounds.getCentreX()) <= 3.0f);
        CHECK (std::abs (bounds.getCentreY()) <= 3.0f);
    }
}

TEST_CASE ("Stroke weights survive the round trip through SVG", "[artwork][gui]")
{
    // The schematic's line weight is a multiplier on the canvas's own stroke,
    // not a width baked into the file -- that is what keeps lines visible when
    // the sheet is zoomed out. A capacitor's plates are the 1.3x case.
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
        sawHeavy = sawHeavy || std::abs (shape.strokeScale - 1.3f) < 0.02f;
    }

    CHECK (sawNormal);
    CHECK (sawHeavy);

    // And a filled shape reads as filled rather than as a hairline stroke.
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
    // The palette draws each symbol clipped to a square swatch, so a symbol
    // scaled too large is not an error -- it is silently sliced, and what you see
    // is a valve with no top to its envelope or a transformer missing its outer
    // windings. That shipped once, because the scale was a single number picked
    // to look right against the parts that happened to be checked.
    //
    // Ink on the swatch's border is the signature of it, so that is what this
    // looks for: render every type into a swatch the size of a palette row and
    // insist the outermost ring of pixels is untouched.
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

        // And it has to have drawn *something*: a swatch that fits because it is
        // empty would pass the check above and tell you nothing.
        int total = 0;

        for (int y = 0; y < swatch; ++y)
            for (int x = 0; x < swatch; ++x)
                total += inked (x, y) ? 1 : 0;

        CHECK (total > 8);
    }
}

TEST_CASE ("The VST Compatible Logo is embedded and findable", "[artwork][gui]")
{
    // Steinberg's usage guidelines ask for this logo in a product's About box,
    // so it is a compliance asset rather than decoration -- and the way it is
    // looked up is the one that fails quietly. Assets::drawable() matches on the
    // *original filename*, not on the identifier JUCE derives from it, and a
    // miss returns null: the About box would simply draw nothing, with the
    // trademark attribution it carries going with it.
    //
    // Not tinted anywhere, unlike every other drawable here. It is someone
    // else's registered mark and is shown as supplied.
    // All four format marks, for the same reason: the About box shows them, and
    // one that fails to load takes a trademark attribution down with it.
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
