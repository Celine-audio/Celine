#pragma once

#include "../Schematic/Element.h"

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace Celine
{
    //==========================================================================
    /** A copy of one scope's trace, so drawing never reads data the audio thread is
        writing. */
    struct ScopeReading
    {
        static constexpr int columns = 128;

        float minimum[columns] {};
        float maximum[columns] {};

        /** The column being written; drawn from here round to here so the newest sample is
            on the right. */
        int writeColumn = 0;

        float dcAverage = 0.0f;
        float peakToPeak = 0.0f;

        /** Time span the columns cover. */
        float windowSeconds = 0.0f;

        /** The vertical range, or fit the data when autoScale is set. */
        bool autoScale = true;
        float rangeMin = -5.0f;
        float rangeMax = 5.0f;

        /** False when the probe has no circuit behind it (unconnected or failed build). */
        bool live = false;
    };

    /** Fetches a scope's trace by element id. False if there is none. */
    using ScopeReader = std::function<bool (int elementId, ScopeReading& out)>;

    /** The vertical range a trace was drawn with, returned so callers label the axis with
        the same numbers. */
    struct ScopeScale
    {
        float lowest = 0.0f;
        float highest = 0.0f;
        bool valid = false;
    };

    /** Draws a trace into `area`. Used by both the sheet and the inspector. */
    ScopeScale drawScopeTrace (juce::Graphics& g, juce::Rectangle<float> area,
                               const ScopeReading& reading, juce::Colour trace,
                               juce::Colour axis, juce::Colour background);

    //==========================================================================
    /** Draws parts in grid space; the view maps grid to pixels. Draws one element, in the
        given colour, and knows nothing about selection or nets. */
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

        /** Unrounded, for hit testing. toGrid() rounds to the nearest grid point. */
        juce::Point<float> toGridExact(juce::Point<float> pixel) const noexcept
        {
            return { (pixel.x - origin.x) / gridSize, (pixel.y - origin.y) / gridSize };
        }

        juce::Point<int> toGrid(juce::Point<float> pixel) const noexcept
        {
            return {juce::roundToInt((pixel.x - origin.x) / gridSize),
                    juce::roundToInt((pixel.y - origin.y) / gridSize)};
        }

        /** Minimum line width in pixels. Zero in palette previews, so code-drawn lines match
            the artwork. */
        float minStrokeWidth = 1.0f;

        /** Whether Input/Output terminals get their IN/OUT lettering (off in the palette). */
        bool terminalLettering = true;

        /** Whether a Text note draws its text; in the palette it draws a "T" instead. */
        bool textShowsContents = true;

        /** Whether pins are marked with a dot. */
        bool connectionDots = false;

        /** How much a caption would cover if placed in this box, in grid units; zero is
            clear. Supplied by the canvas. draw() picks the first clear side, else the least
            cluttered. Unset: captions go on the part's natural side. */
        std::function<float(const SchematicModel::Element&, juce::Rectangle<float>)> captionClutter;

        /** Called with each caption's box, so the next caption can avoid it. */
        std::function<void(juce::Rectangle<float>)> onCaptionPlaced;

        /** Line width for the current zoom. */
        float strokeWidth() const noexcept
        {
            return juce::jmax(minStrokeWidth, gridSize * 0.12f);
        }

        //======================================================================
        /** What a part covers, in grid units: its artwork plus its pins. */
        juce::Rectangle<float> elementBounds(const SchematicModel::Element& element) const;

        /** Draws a part's symbol, pins and caption. */
        void draw(juce::Graphics& g, const SchematicModel::Element& element, juce::Colour colour) const;

        /** The symbol and pins, without the caption. */
        void drawSymbolOnly(juce::Graphics& g, const SchematicModel::Element& element, juce::Colour colour) const;

        /** Draws a type's symbol centred in `area`. For the palette. */
        static void drawPreview(juce::Graphics& g, SchematicModel::ElementType type, juce::Rectangle<float> area,
                                juce::Colour colour);

        /** The line width drawPreview uses in `area`, for things drawn beside the previews
            (the palette's wire). */
        static float previewLineWidth(juce::Rectangle<float> area);

        /** The scope screen's rectangle in grid units about the part's centre, matching the
            frame in `scope.svg`. */
        static juce::Rectangle<float> getScopeScreen() noexcept
        {
            return { -2.4f, -2.2f, 4.8f, 3.4f };
        }

        /** Engineering notation: 4700 -> "4.7k", 1e-7 -> "100n". */
        static juce::String formatValue(double value, const juce::String& unit);

        /** Supply rails: "9V", "±15V", or "+12V / -9V". */
        static juce::String formatSupply(double positive, double negative, const juce::String& unit);

        /** A turns ratio: "10:1", "1:8". No prefixes. */
        static juce::String formatRatio(double primary, double secondary);

        /** Parses formatValue's output and similar: "4k7", "100n", "2.2M", "1e3". False on
            failure. */
        static bool parseValue(const juce::String& text, double& valueOut);

        /** A pot's marking: taper letter then value, upper case, e.g. "B10K". */
        static juce::String formatPotValue(double value, SchematicModel::Taper taper);

        /** Parses formatPotValue's output or a plain value. `taperOut` is only written when
            the text names a taper. */
        static bool parsePotValue(const juce::String& text, double& valueOut,
                                  SchematicModel::Taper& taperOut, bool& taperGiven);
    };
} // namespace Celine
