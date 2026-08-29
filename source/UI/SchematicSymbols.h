#pragma once

#include "../Schematic/Element.h"

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace SchematicUI
{
    //==========================================================================
    /** One scope's picture, copied out of wherever it is being written.

        A plain snapshot rather than a pointer into the live data. The trace is
        filled in by the audio thread, and the drawing code runs on the message
        thread -- copying two hundred and fifty-six floats once per repaint is
        nothing, and it means the renderer never sees a column change underneath
        it halfway through drawing the line that joins it to the next one.
    */
    struct ScopeReading
    {
        static constexpr int columns = 128;

        float minimum[columns] {};
        float maximum[columns] {};

        /** The column the writer is filling; the picture is drawn from here
            round to here, so the newest sample is always at the right. */
        int writeColumn = 0;

        float dcAverage = 0.0f;
        float peakToPeak = 0.0f;

        /** How wide a slice of time the columns cover. Carried with the reading
            rather than reached for from the processor, so the axis a panel
            labels is the one the trace was actually built from. */
        float windowSeconds = 0.0f;

        /** The vertical range to draw against, and whether to ignore it and fit
            the data instead.

            Carried here, alongside the samples, for the same reason the trace
            renderer is shared at all: the sheet and the inspector draw the same
            probe, and a scope pinned to ±10 V in one place and auto-ranging in
            the other would be two different instruments wearing one symbol. */
        bool autoScale = true;
        float rangeMin = -5.0f;
        float rangeMax = 5.0f;

        /** False when the probe has no circuit behind it -- pins touching
            nothing, or a build that failed. Drawn differently, because a flat
            line at zero and "not connected to anything" look identical and mean
            very different things. */
        bool live = false;
    };

    /** Asked for a scope's picture by element id. False if there isn't one. */
    using ScopeReader = std::function<bool (int elementId, ScopeReading& out)>;

    /** The vertical range a trace was drawn against. Returned rather than
        recomputed by anything that wants to label the axis: the scaling is
        chosen from the data, so a caller working it out a second time is a
        second implementation of it, and a graph whose numbers do not match its
        picture is worse than one with no numbers on it. */
    struct ScopeScale
    {
        float lowest = 0.0f;
        float highest = 0.0f;
        bool valid = false;
    };

    /** Draws a trace into `area`, scaled to fit whatever it contains.

        Shared by the sheet and the inspector so the two cannot drift into
        drawing the same probe two different ways -- which, since the whole point
        of the part is reading a number off it, would be worse than either one
        being wrong on its own. */
    ScopeScale drawScopeTrace (juce::Graphics& g, juce::Rectangle<float> area,
                               const ScopeReading& reading, juce::Colour trace,
                               juce::Colour axis, juce::Colour background);

    //==========================================================================
    /**
        Draws the parts.

        Symbols are drawn in grid space and mapped to pixels by the view, so the
        same code serves the canvas at any zoom and the palette at its own fixed
        size. Nothing here knows about selection, editing or nets -- it draws one
        element, in one place, in whatever colour it is told.
    */
    struct SymbolPainter
    {
        /** Pixels per grid unit. */
        float gridSize = 10.0f;

        /** Pixel position of grid origin. */
        juce::Point<float> origin{0.0f, 0.0f};

        juce::Point<float> toPixel(juce::Point<int> gridPoint) const noexcept
        {
            return origin + juce::Point<float>(static_cast<float>(gridPoint.x) * gridSize,
                                               static_cast<float>(gridPoint.y) * gridSize);
        }

        juce::Point<float> toPixel(float gridX, float gridY) const noexcept
        {
            return origin + juce::Point<float>(gridX * gridSize, gridY * gridSize);
        }

        /** Unrounded, for hit testing. toGrid() below snaps to the nearest
            intersection, which is right for placing a part and wrong for asking
            what is under the cursor. */
        juce::Point<float> toGridExact(juce::Point<float> pixel) const noexcept
        {
            return { (pixel.x - origin.x) / gridSize, (pixel.y - origin.y) / gridSize };
        }

        juce::Point<int> toGrid(juce::Point<float> pixel) const noexcept
        {
            return {juce::roundToInt((pixel.x - origin.x) / gridSize),
                    juce::roundToInt((pixel.y - origin.y) / gridSize)};
        }

        /** The thinnest a line may be drawn, whatever the zoom. One pixel on the
            sheet, where a symbol has room and the grid is the reference; heavier
            in a palette swatch, where the same rule lands on a half pixel and
            renders as grey rather than ink. See drawPreview. */
        float minStrokeWidth = 1.0f;

        /** Whether an Input or Output terminal gets its IN/OUT lettering.

            On the sheet it does, because that is the only thing saying which end
            of the circuit you are looking at. In a palette swatch it does not:
            the row already reads "Input", and the lettering is drawn well outside
            the flag it labels -- five and a half grid squares out against the
            flag's two -- so keeping it would either shrink every terminal to a
            third of its neighbours or run off the edge of the pill. */
        bool terminalLettering = true;

        /** Whether a Text note draws what it says.

            On the sheet it does -- a note *is* its words, and drawing anything
            else would be drawing the wrong thing. In a palette swatch it draws a
            capital **T** instead: the row is offering you the type, not an
            instance of it, and the placeholder word "Text" rendered small enough
            to fit a swatch is an illegible grey smudge next to twenty crisp
            symbols. */
        bool textShowsContents = true;

        /** Whether a part's pins are marked with a dot.

            On the sheet they are, because that is what says where a wire has to
            land. In a palette swatch there is nothing to land on: the row is
            offering you a type, not a placed part, and at swatch size the dots
            are a large fraction of the symbol -- a resistor is a rectangle a
            square and a half across with two dots very nearly as wide bolted to
            its ends, which reads as a dumbbell rather than as a resistor. */
        bool connectionDots = true;

        /** Line weight that keeps symbols legible as the view zooms. */
        float strokeWidth() const noexcept
        {
            return juce::jmax(minStrokeWidth, gridSize * 0.12f);
        }

        //======================================================================
        /** What a part covers on the sheet, in grid units.

            Taken from the artwork -- the thing you can actually see and aim at
            -- rather than from the span of its pins. Pin positions are unioned
            in so a terminal stays grabbable, but they no longer *define* the
            box: a ground's glyph hangs off the side of its only pin, and a
            triode's envelope has no particular relation to where its pins ended
            up, so a pin-derived box was too small on some parts and far too big
            on others.

            Float, and deliberately: the caller used to round the mouse to the
            nearest grid intersection before testing, which put half a grid
            square of slop around everything however tight the box was. */
        juce::Rectangle<float> elementBounds(const SchematicModel::Element& element) const;

        /** Draws one element's symbol, its pins, and its value caption. */
        void draw(juce::Graphics& g, const SchematicModel::Element& element, juce::Colour colour) const;

        /** The symbol and its pins, without the caption -- what a palette entry
            wants, since the list already gives the name. */
        void drawSymbolOnly(juce::Graphics& g, const SchematicModel::Element& element, juce::Colour colour) const;

        /** Draws a symbol centred in a rectangle, ignoring where the element
            actually sits. For the palette. */
        static void drawPreview(juce::Graphics& g, SchematicModel::ElementType type, juce::Rectangle<float> area,
                                juce::Colour colour);

        /** The grid rectangle a scope's screen occupies, relative to the part's
            centre -- the frame drawn in `scope.svg`, stated once here so the
            trace lands inside it rather than beside it. Artwork and trace have
            to agree, and only one of them can be measured from the other. */
        static juce::Rectangle<float> getScopeScreen() noexcept
        {
            return { -2.4f, -2.2f, 4.8f, 3.4f };
        }

        /** 4700 -> "4.7k", 1e-7 -> "100n". Schematic notation, because "1e-07 F"
            is unreadable on a drawing. */
        static juce::String formatValue(double value, const juce::String& unit);

        /** A turns ratio, written the way a schematic writes one: "10:1", "1:8".
            No engineering prefixes -- a ratio is dimensionless, so the milli in
            "100m:1" is meaningless, and that is what a single-box ratio field
            produced for every step-up transformer. */
        static juce::String formatRatio(double primary, double secondary);

        /** Parses what formatValue produces, plus anything close enough: "4k7",
            "100n", "2.2M", "1e3". Returns false if it can't. */
        static bool parseValue(const juce::String& text, double& valueOut);

        /** A potentiometer, written the way one is actually marked: "B10K".

            The taper is the half of a pot's specification you cannot see on the
            drawing otherwise -- two 250k pots with different tapers are
            different controls, and the symbol for them is identical. Putting
            the letter in front of the value is how every catalogue, every
            schematic and the part itself says which, so it needs no explaining
            to anyone who has bought one.

            Upper case throughout, unlike the rest of the sheet's engineering
            notation: "B10K" is the marking, and "B10k" is a half-translation of
            it into a convention pots do not use. */
        static juce::String formatPotValue(double value, SchematicModel::Taper taper);

        /** Reads back what formatPotValue writes, and plain values too.

            `taperOut` is only written when the text actually named a taper, so
            typing a bare "250k" changes the resistance and leaves the taper
            alone -- which is what someone correcting a value meant, and losing
            their taper to it would be a nasty surprise. */
        static bool parsePotValue(const juce::String& text, double& valueOut,
                                  SchematicModel::Taper& taperOut, bool& taperGiven);
    };
} // namespace SchematicUI
