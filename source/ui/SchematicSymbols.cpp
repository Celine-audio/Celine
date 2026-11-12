#include "SchematicSymbols.h"

#include <limits>

#include <CelineUI/Fonts.h>
#include "SymbolArtwork.h"
#include <CelineUI/Theme.h>

namespace Celine
{
    using namespace SchematicModel;

    namespace
    {
        // ======================================================================
        // Caption colours: name, value, and unset value.

        struct CaptionRun
        {
            juce::String text;
            juce::Colour colour;
        };

        /** Splits a part's caption into coloured runs. Returns how many. */
        int buildCaptionRuns(const Element& element, CaptionRun* out)
        {
            const auto& info = getElementInfo(element.type);
            int count = 0;

            if (element.label.isNotEmpty())
                out[count++] = {element.label, Theme::captionName()};

            if (element.hasModelChoice())
            {
                // An unknown model shows its saved id in the unset colour.
                if (element.unresolvedModelId.isNotEmpty())
                {
                    out[count++] = {element.unresolvedModelId, Theme::captionUnset()};
                }
                else
                {
                    const auto choices = getModelChoices(element.type);

                    if (juce::isPositiveAndBelow(element.modelIndex, choices.size()))
                        out[count++] = {choices[element.modelIndex], Theme::captionName()};
                }
            }

            if (element.isSecondValueRail())
            {
                // "9V" for a single supply, "±15V" for a split one.
                out[count++] = {SymbolPainter::formatSupply(element.value, element.valueB, info.unit),
                                element.value > 0.0 ? Theme::captionValue() : Theme::captionUnset()};
            }
            else if (element.hasSecondValue())
            {
                const bool set = element.value > 0.0 && element.valueB > 0.0;
                out[count++] = {SymbolPainter::formatRatio(element.value, element.valueB),
                                set ? Theme::captionValue() : Theme::captionUnset()};
            }
            else if (element.type == ElementType::Potentiometer)
            {
                // Pots show taper and value together, e.g. "B10K".
                out[count++] = {SymbolPainter::formatPotValue(element.value, element.taper),
                                element.value > 0.0 ? Theme::captionValue() : Theme::captionUnset()};
            }
            else if (element.hasNumericValue())
            {
                out[count++] = {SymbolPainter::formatValue(element.value, info.unit),
                                element.value > 0.0 ? Theme::captionValue() : Theme::captionUnset()};
            }

            return count;
        }

        /** A group box's colour, from the theme. */
        juce::Colour rectangleColour(int modelIndex)
        {
            return Theme::groupBox(modelIndex);
        }

        /** Default caption side for a type, facing its default way: 0 right, 1 below, 2 left,
            3 above (the order a quarter turn moves them). The side its wires don't leave from. */
        int naturalCaptionSide(ElementType type)
        {
            switch (type)
            {
                case ElementType::Potentiometer:
                    return 2;

                case ElementType::OpAmp:
                case ElementType::PowerAmp:
                case ElementType::Transformer:
                case ElementType::CenterTapTransformer:
                case ElementType::Ground:
                case ElementType::Input:
                case ElementType::Output:
                    return 1;

                case ElementType::Scope:
                    return 3;

                case ElementType::Resistor:
                case ElementType::Capacitor:
                case ElementType::Inductor:
                case ElementType::Switch:
                case ElementType::Spdt:
                case ElementType::VoltageSource:
                case ElementType::Diode:
                case ElementType::Transistor:
                case ElementType::Jfet:
                case ElementType::Mosfet:
                case ElementType::Triode:
                case ElementType::Pentode:
                case ElementType::VacuumDiode:
                case ElementType::Text:
                case ElementType::Rectangle:
                case ElementType::Node:
                    break;
            }

            return 0;
        }

        /** Where a caption of `size` goes on `side` of `body`, in grid units: half a square
            off the body, centred on the part. */
        juce::Rectangle<float> captionBox(juce::Rectangle<float> body, juce::Point<float> centre,
                                          int side, juce::Point<float> size)
        {
            constexpr float gap = 0.5f;

            switch (side & 3)
            {
                case 0:  return { body.getRight() + gap, centre.y - size.y * 0.5f, size.x, size.y };
                case 2:  return { body.getX() - gap - size.x, centre.y - size.y * 0.5f, size.x, size.y };
                case 1:  return { centre.x - size.x * 0.5f, body.getBottom() + gap, size.x, size.y };
                default: return { centre.x - size.x * 0.5f, body.getY() - gap - size.y, size.x, size.y };
            }
        }

        /** Rotates a point about the origin by an element's orientation, in grid units. */
        juce::Point<float> rotate(float x, float y, int orientation)
        {
            for (int turn = 0; turn < (orientation & 3); ++turn)
            {
                const float rotatedX = -y;
                y = x;
                x = rotatedX;
            }

            return {x, y};
        }

        /** Maps an element's local grid coordinates to pixels. */
        struct LocalSpace
        {
            const SymbolPainter& painter;
            juce::Point<int> centre;
            int orientation;
            bool mirrored;

            juce::Point<float> operator()(float x, float y) const
            {
                // Mirror then rotate, as Element::getPinPosition does.
                const auto rotated = rotate(mirrored ? -x : x, y, orientation);
                return painter.toPixel(static_cast<float>(centre.x) + rotated.x,
                                       static_cast<float>(centre.y) + rotated.y);
            }

            void line(juce::Graphics& g, float x1, float y1, float x2, float y2, float thickness) const
            {
                g.drawLine(juce::Line<float>((*this)(x1, y1), (*this)(x2, y2)), thickness);
            }
        };


        /** How far a part's symbol reaches, in grid units about its centre: artwork bounds
            plus pins. Min/max by hand (Rectangle::getUnion ignores empty rectangles). */
        juce::Rectangle<float> symbolExtent(const Element& element)
        {
            // Pinless parts without artwork use their own geometry.
            if (element.type == ElementType::Rectangle)
            {
                const auto box = element.getRectangleBounds();
                return {static_cast<float>(box.getX() - element.x),
                        static_cast<float>(box.getY() - element.y),
                        static_cast<float>(box.getWidth()), static_cast<float>(box.getHeight())};
            }

            // The palette's "T" for a Text note.
            if (element.type == ElementType::Text)
                return {-1.2f, -1.2f, 2.4f, 2.4f};

            // An empty node tag, as the palette draws it.
            if (element.type == ElementType::Node)
                return element.getNodeLocalBounds().toFloat();

            // Seeded at the origin.
            float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;

            auto include = [&](juce::Rectangle<float> box)
            {
                left = juce::jmin(left, box.getX());
                top = juce::jmin(top, box.getY());
                right = juce::jmax(right, box.getRight());
                bottom = juce::jmax(bottom, box.getBottom());
            };

            if (const auto* artwork = getSymbolArtwork(element))
                for (const auto& shape : artwork->shapes)
                    include(shape.path.getBounds());

            for (int pin = 0; pin < element.getPinCount(); ++pin)
            {
                const auto p = element.getPinPosition(pin);
                include({static_cast<float>(p.x - element.x),
                         static_cast<float>(p.y - element.y), 0.0f, 0.0f});
            }

            // Fallback for a part with neither artwork nor pins.
            if (right - left <= 0.0f || bottom - top <= 0.0f)
                return {-1.5f, -1.5f, 3.0f, 3.0f};

            return {left, top, right - left, bottom - top};
        }

        /** Grid squares a palette swatch represents: smaller parts draw at this scale (so
            their relative sizes are kept), larger ones are scaled down to fit. */
        constexpr float previewSquares = 6.0f;


        void drawArrow(juce::Graphics& g, const SymbolPainter& painter,
                       juce::Point<float> from, juce::Point<float> tip)
        {
            const auto direction = (tip - from);
            const auto length = direction.getDistanceFromOrigin();

            if (length < 0.0001f)
                return;

            const float grid = painter.gridSize;
            const float headLength = grid * 0.5f;
            const auto unit = direction / length;
            const juce::Point<float> normal(-unit.y, unit.x);
            const auto base = tip - unit * headLength;

            juce::Path shaft;
            shaft.startNewSubPath(from);
            shaft.lineTo(base);
            g.strokePath(shaft, juce::PathStrokeType(painter.strokeWidth(), juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));

            juce::Path head;
            head.startNewSubPath(tip);
            head.lineTo(base + normal * grid * 0.25f);
            head.lineTo(base - normal * grid * 0.25f);
            head.closeSubPath();

            g.fillPath(head);
            g.strokePath(head, juce::PathStrokeType(grid * 0.1f, juce::PathStrokeType::curved));
        }
    } // namespace

    ScopeScale drawScopeTrace (juce::Graphics& g, juce::Rectangle<float> area,
                               const ScopeReading& reading, juce::Colour trace,
                               juce::Colour axis, juce::Colour background)
    {
        if (area.getWidth() < 4.0f || area.getHeight() < 4.0f)
            return {};

        // Opaque, covering the placeholder waveform in scope.svg.
        g.setColour (background);
        g.fillRect (area);

        if (! reading.live)
        {
            // No circuit behind the probe: say so in words.
            g.setColour (axis);
            g.drawLine (area.getX(), area.getCentreY(), area.getRight(), area.getCentreY(), 1.0f);
            return {};
        }

        // Auto-scale by default; a fixed range is for comparing levels.
        float centre = 0.0f;
        float span = 1.0f;

        if (reading.autoScale || ! (reading.rangeMax > reading.rangeMin))
        {
            float lowest = reading.minimum[0], highest = reading.maximum[0];

            for (int c = 1; c < ScopeReading::columns; ++c)
            {
                lowest = juce::jmin (lowest, reading.minimum[c]);
                highest = juce::jmax (highest, reading.maximum[c]);
            }

            // A flat trace: give it a range rather than divide by zero.
            centre = 0.5f * (lowest + highest);
            span = juce::jmax (1.0e-6f, highest - lowest) * 1.15f;
        }
        else
        {
            // A typed range is used exactly, without headroom.
            centre = 0.5f * (reading.rangeMin + reading.rangeMax);
            span = reading.rangeMax - reading.rangeMin;
        }

        // The edges of the drawn window, for labelling the axis.
        const ScopeScale scale { centre - 0.5f * span, centre + 0.5f * span, true };

        auto yFor = [&] (float value)
        {
            return area.getCentreY() - (value - centre) / span * area.getHeight();
        };

        // The DC level.
        g.setColour (axis);
        const auto dcY = yFor (reading.dcAverage);

        if (area.getY() <= dcY && dcY <= area.getBottom())
        {
            const float dashes[] = { 3.0f, 3.0f };
            g.drawDashedLine ({ area.getX(), dcY, area.getRight(), dcY }, dashes, 2, 1.0f);
        }

        // Oldest to newest, left to right, starting after the column being written.
        const float step = area.getWidth() / static_cast<float> (ScopeReading::columns);

        juce::Path envelope;
        bool started = false;

        for (int i = 0; i < ScopeReading::columns; ++i)
        {
            const int c = (reading.writeColumn + 1 + i) % ScopeReading::columns;
            const float x = area.getX() + static_cast<float> (i) * step;

            const auto top = juce::jlimit (area.getY(), area.getBottom(), yFor (reading.maximum[c]));
            const auto bottom = juce::jlimit (area.getY(), area.getBottom(), yFor (reading.minimum[c]));

            if (! started)
            {
                envelope.startNewSubPath (x, top);
                started = true;
            }
            else
            {
                envelope.lineTo (x, top);
            }

            // A column's min-max span, so short transients stay visible.
            envelope.lineTo (x, bottom);
        }

        g.setColour (trace);
        g.strokePath (envelope, juce::PathStrokeType (1.0f));

        return scale;
    }

    //==========================================================================

    void SymbolPainter::draw(juce::Graphics& g, const Element& element, juce::Colour colour) const
    {
        drawSymbolOnly(g, element, colour);

        // Text, Rectangle and Node draw their own label in drawSymbolOnly, so no caption here.
        if (element.type == ElementType::Text || element.type == ElementType::Rectangle
            || element.type == ElementType::Node)
            return;

        if (gridSize <= 6.0f)
            return;

        // The caption in coloured runs: name (yellow), value (green), unset value (red).
        CaptionRun runs[3];
        const int count = buildCaptionRuns(element, runs);

        if (count == 0)
            return;

        const juce::Font font(Fonts::light(gridSize * 0.92f));
        g.setFont(font);

        // In grid units from here on.
        const float lineHeight = 1.25f;
        const float space = 0.45f;
        float widths[3];
        float widest = 0.0f, inLine = 0.0f;

        for (int i = 0; i < count; ++i)
        {
            widths[i] = juce::GlyphArrangement::getStringWidth(font, runs[i].text) / gridSize;
            widest = juce::jmax(widest, widths[i]);
            inLine += widths[i] + (i > 0 ? space : 0.0f);
        }

        // Beside a part the runs stack, name over value; above or below they share a line.
        const auto sizeOn = [&](int side)
        {
            return (side & 1) == 0 ? juce::Point<float>(widest, lineHeight * (float) count)
                                   : juce::Point<float>(inLine, lineHeight);
        };

        const auto body = elementBounds(element);
        const juce::Point<float> centre((float) element.x, (float) element.y);

        // Try the part's natural side, then the opposite, then the two across. The first
        // clear side wins, else the least cluttered (earlier sides win ties).
        const int mirroredSide = element.mirrored && (naturalCaptionSide(element.type) & 1) == 0
                                     ? naturalCaptionSide(element.type) + 2
                                     : naturalCaptionSide(element.type);
        const int natural = (mirroredSide + element.orientation) & 3;
        int side = natural;

        if (captionClutter)
        {
            float best = std::numeric_limits<float>::max();

            for (const int candidate : { natural, natural + 2, natural + 1, natural + 3 })
            {
                const float clutter = captionClutter(element, captionBox(body, centre, candidate, sizeOn(candidate)));

                if (clutter < best)
                {
                    best = clutter;
                    side = candidate & 3;
                }

                if (clutter <= 0.0f)
                    break;
            }
        }

        const auto box = captionBox(body, centre, side, sizeOn(side));

        if (onCaptionPlaced)
            onCaptionPlaced(box);

        const auto topLeft = toPixel(box.getX(), box.getY());

        for (int i = 0; i < count; ++i)
        {
            juce::Rectangle<float> line;

            if ((side & 1) == 0)
            {
                // Stacked runs align against the part.
                const float x = side == 0 ? 0.0f : widest - widths[i];
                line = { x, lineHeight * (float) i, widths[i], lineHeight };
            }
            else
            {
                float x = 0.0f;
                for (int j = 0; j < i; ++j)
                    x += widths[j] + space;
                line = { x, 0.0f, widths[i], lineHeight };
            }

            // Keep the caller's alpha, so a dimmed part has a dimmed caption.
            g.setColour(runs[i].colour.withMultipliedAlpha(colour.getFloatAlpha()));
            g.drawText(runs[i].text, (line * gridSize) + topLeft, juce::Justification::centredLeft, false);
        }
    }

    namespace
    {
        /** Draws a part's body from its artwork's ink: one fill per opacity level, mirrored,
            rotated and placed in the same order as Element::getPinPosition. */
        void drawArtwork(juce::Graphics& g, const SymbolPainter& painter, const Element& element,
                         juce::Colour colour)
        {
            const auto* artwork = getSymbolArtwork(element);

            if (artwork == nullptr)
                return;

            const auto placement =
                juce::AffineTransform::scale(element.mirrored ? -painter.gridSize : painter.gridSize, painter.gridSize)
                    .rotated(static_cast<float>(element.orientation & 3)
                             * juce::MathConstants<float>::halfPi)
                    .translated(painter.toPixel(juce::Point<int>(element.x, element.y)));

            if (! artwork->ink.empty())
            {
                for (const auto& run : artwork->ink)
                {
                    g.setColour(colour.withMultipliedAlpha(run.alpha));
                    g.fillPath(run.path, placement);
                }

                // Restore the caller's colour for what follows.
                g.setColour(colour);
                return;
            }

            // A file with no viewBox: stroke its paths at the canvas weight.
            for (const auto& shape : artwork->shapes)
            {
                if (shape.filled)
                    g.fillPath(shape.path, placement);
                else
                    g.strokePath(shape.path,
                                 juce::PathStrokeType(painter.strokeWidth() * shape.strokeScale),
                                 placement);
            }
        }

        void drawTerminalLettering(juce::Graphics& g, const SymbolPainter& painter,
                                       const Element& element, const LocalSpace& s)
        {
            // IN/OUT is drawn with a font, so it stays upright.
            const bool isInput = element.type == ElementType::Input;
            const float backX = isInput ? -1.6f : 1.6f;

            g.setFont(Fonts::light(painter.gridSize * 1.0f));
            g.drawText(isInput ? "IN" : "OUT",
                       juce::Rectangle<float>(painter.gridSize * 4.0f, painter.gridSize * 1.4f)
                           .withCentre(s(backX * 2.2f, 0.0f)),
                       juce::Justification::centred, false);
        }

        void drawWiper(juce::Graphics& g, const SymbolPainter& painter,
                           const Element& element, const LocalSpace& s)
        {
            // The wiper follows the knob position. Position 1 taps pin 0, the top (see
            // Potentiometer::setPosition).
            const float tap = 1.0f - 2.0f * element.getControlPosition();
            const auto tip = s(0.83f, tap);
            const auto tail = s(2.6f, 0.0f);
            drawArrow(g, painter, tail, tip);
        }

        void drawNodeTag(juce::Graphics& g, const SymbolPainter& painter,
                             const Element& element, const LocalSpace& s, juce::Colour colour)
        {
            // A tag with the name inside, pointing at its pin; its width follows the name.
            const auto box = element.getNodeLocalBounds().toFloat();

            // Tip on the pin at {2, 0}. Corners go through LocalSpace so the tag turns with the
            // pin.
            juce::Path tag;
            tag.startNewSubPath(s(box.getRight(), 0.0f));
            tag.lineTo(s(box.getRight() - 1.0f, box.getY()));
            tag.lineTo(s(box.getX(), box.getY()));
            tag.lineTo(s(box.getX(), box.getBottom()));
            tag.lineTo(s(box.getRight() - 1.0f, box.getBottom()));
            tag.closeSubPath();

            g.strokePath(tag, juce::PathStrokeType(painter.strokeWidth(), juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

            // Palette swatches show the empty tag.
            if (painter.textShowsContents)
            {
                // Centred on the body, not the whole tag including its point.
                const auto centre = s(box.getCentreX() - 0.5f, 0.0f);

                juce::Graphics::ScopedSaveState state(g);

                // Text turns with the tag for a quarter turn, but never upside down.
                if ((element.orientation & 1) != 0)
                    g.addTransform(juce::AffineTransform::rotation(
                        juce::MathConstants<float>::halfPi, centre.x, centre.y));

                g.setFont(Fonts::light(painter.gridSize * 1.1f));

                // Red when unnamed.
                if (element.label.isEmpty())
                    g.setColour(Theme::captionUnset().withMultipliedAlpha(colour.getFloatAlpha()));

                g.drawText(element.getNodeDisplayText(),
                           juce::Rectangle<float>(box.getWidth() * painter.gridSize,
                                                  painter.gridSize * 1.6f).withCentre(centre),
                           juce::Justification::centred, false);
            }
        }

        void drawTextNote(juce::Graphics& g, const SymbolPainter& painter,
                              const Element& element, juce::Colour colour)
        {
            // Drawn upright, ignoring orientation.
            const auto centre = painter.toPixel({element.x, element.y});

            if (! painter.textShowsContents)
            {
                // A "T" drawn as two strokes, so it matches the other swatches' line weight. The
                // crossbar is 1.3x the stroke, like other emphasised strokes.
                const float halfBar = painter.gridSize * 1.05f;
                const float top = centre.y - painter.gridSize;
                const float bottom = centre.y + painter.gridSize;

                g.drawLine(centre.x - halfBar, top, centre.x + halfBar, top, painter.strokeWidth() * 1.3f);
                g.drawLine(centre.x, top, centre.x, bottom, painter.strokeWidth());
            }
            else
            {
                const auto text = element.getDisplayText();
                const auto halfWidth =
                    static_cast<float>(element.getTextHalfWidth()) * painter.gridSize;

                g.setFont(Fonts::light(painter.gridSize * 1.6f));

                // Dimmed while it is the placeholder.
                if (element.label.isEmpty())
                    g.setColour(colour.withAlpha(0.45f));

                g.drawText(text,
                           juce::Rectangle<float>(halfWidth * 2.0f, painter.gridSize * 2.0f)
                               .withCentre(centre),
                           juce::Justification::centred, false);
            }
        }

        void drawGroupBox(juce::Graphics& g, const SymbolPainter& painter,
                              const Element& element, juce::Colour colour)
        {
            // In sheet space: a box has no orientation.
            const auto box = element.getRectangleBounds();
            const juce::Rectangle<float> pixels(painter.toPixel(box.getTopLeft()),
                                                painter.toPixel(box.getBottomRight()));

            const float alpha = colour.getFloatAlpha();
            const auto tint = rectangleColour(element.modelIndex);
            const float corner = juce::jmin(painter.gridSize * 0.7f, pixels.getWidth() * 0.2f,
                                            pixels.getHeight() * 0.2f);

            g.setColour(tint.withAlpha(0.07f * alpha));
            g.fillRoundedRectangle(pixels, corner);

            g.setColour(tint.withAlpha(0.75f * alpha));
            g.drawRoundedRectangle(pixels, corner, painter.strokeWidth());

            if (element.label.isNotEmpty() && painter.gridSize > 6.0f)
            {
                g.setFont(Fonts::light(painter.gridSize * 1.25f));
                g.setColour(tint.withAlpha(0.95f * alpha));

                juce::Graphics::ScopedSaveState state(g);
                g.reduceClipRegion(pixels.toNearestInt());
                g.drawText(element.label, pixels.reduced(painter.gridSize * 0.55f, painter.gridSize * 0.35f),
                           juce::Justification::topLeft, false);
            }
        }

    } // namespace

    void SymbolPainter::drawSymbolOnly(juce::Graphics& g, const Element& element,
                                       juce::Colour colour) const
    {
        g.setColour(colour);
        drawArtwork(g, *this, element, colour);

        // The following parts are drawn in code because they show the part's state.
        const LocalSpace s{*this, {element.x, element.y}, element.orientation, element.mirrored};

        if (terminalLettering
            && (element.type == ElementType::Input || element.type == ElementType::Output))
            drawTerminalLettering(g, *this, element, s);
        else if (element.type == ElementType::Potentiometer)
            drawWiper(g, *this, element, s);
        else if (element.type == ElementType::Node)
            drawNodeTag(g, *this, element, s, colour);
        else if (element.type == ElementType::Text)
            drawTextNote(g, *this, element, colour);
        else if (element.type == ElementType::Rectangle)
            drawGroupBox(g, *this, element, colour);

        if (! connectionDots)
            return;

        // Pin dots.
        g.setColour(colour.withAlpha(0.9f));
        const float pinRadius = juce::jmax(1.5f, gridSize * 0.18f);

        for (int pin = 0; pin < element.getPinCount(); ++pin)
        {
            const auto p = toPixel(element.getPinPosition(pin));
            g.fillEllipse(juce::Rectangle<float>(pinRadius * 2.0f, pinRadius * 2.0f).withCentre(p));
        }
    }

    //==========================================================================

    juce::Rectangle<float> SymbolPainter::elementBounds(const Element& element) const
    {
        const juce::Point<float> centre(static_cast<float>(element.x), static_cast<float>(element.y));

        // Text, Rectangle and Node have their own bounds.
        if (element.type == ElementType::Text)
        {
            const auto halfWidth = static_cast<float>(element.getTextHalfWidth());
            return { centre.x - halfWidth, centre.y - 1.0f, halfWidth * 2.0f, 2.0f };
        }

        if (element.type == ElementType::Rectangle)
            return element.getRectangleBounds().toFloat();

        if (element.type == ElementType::Node)
            return element.getNodeBounds().toFloat();

        bool first = true;
        float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;

        const auto include = [&](juce::Rectangle<float> box)
        {
            if (first)
            {
                minX = box.getX(); minY = box.getY();
                maxX = box.getRight(); maxY = box.getBottom();
                first = false;
                return;
            }

            minX = juce::jmin(minX, box.getX());
            minY = juce::jmin(minY, box.getY());
            maxX = juce::jmax(maxX, box.getRight());
            maxY = juce::jmax(maxY, box.getBottom());
        };

        if (const auto* artwork = getSymbolArtwork(element); artwork != nullptr && ! artwork->isEmpty())
        {
            // Mirror, rotate, place: same order as the drawing and Element::getPinPosition.
            auto box = artwork->bounds;

            if (element.mirrored)
                box.setX(-box.getRight());

            for (int turn = 0; turn < (element.orientation & 3); ++turn)
                box = { -box.getBottom(), box.getX(), box.getHeight(), box.getWidth() };

            include(box + centre);
        }

        // Pins included, so a part is grabbable even without artwork.
        for (int pin = 0; pin < element.getPinCount(); ++pin)
            include(juce::Rectangle<float>(0.4f, 0.4f).withCentre(element.getPinPosition(pin).toFloat()));

        if (first)
            include({ centre.x - 0.5f, centre.y - 0.5f, 1.0f, 1.0f });

        juce::Rectangle<float> box(minX, minY, maxX - minX, maxY - minY);

        // Minimum span, so thin symbols (a switch) are still clickable.
        constexpr float minimumSpan = 1.2f;

        if (box.getWidth() < minimumSpan)
            box = box.withWidth(minimumSpan).withX(box.getCentreX() - minimumSpan * 0.5f);

        if (box.getHeight() < minimumSpan)
            box = box.withHeight(minimumSpan).withY(box.getCentreY() - minimumSpan * 0.5f);

        return box;
    }

    float SymbolPainter::previewLineWidth(juce::Rectangle<float> area)
    {
        // For a part no larger than previewSquares.
        constexpr float pinDotAllowance = 5.0f;
        const float grid = juce::jmax(0.5f, (juce::jmin(area.getWidth(), area.getHeight()) - pinDotAllowance)
                                                / previewSquares);
        return grid * 0.12f;
    }

    void SymbolPainter::drawPreview(juce::Graphics& g, ElementType type, juce::Rectangle<float> area,
                                    juce::Colour colour)
    {
        // A temporary element at the origin.
        Element element;
        element.type = type;
        element.x = 0;
        element.y = 0;

        // Box size for its swatch.
        element.width = 7;
        element.height = 5;

        SymbolPainter painter;

        // Scale to previewSquares, or to fit a larger part. Leave room for the pin dots,
        // which stick out past the artwork.
        const auto extent = symbolExtent(element);
        const float swatch = juce::jmin(area.getWidth(), area.getHeight());
        constexpr float pinDotAllowance = 5.0f;

        const float reach = juce::jmax(previewSquares, extent.getWidth(), extent.getHeight());
        painter.gridSize = juce::jmax(0.5f, (swatch - pinDotAllowance) / reach);

        // Centre the drawn extent, not the element's origin.
        painter.origin = area.getCentre()
                       - juce::Point<float>(extent.getCentreX(), extent.getCentreY())
                             * painter.gridSize;

        // No minimum line width, so code-drawn lines match the artwork's weight.
        painter.minStrokeWidth = 0.0f;

        // No IN/OUT lettering in the palette.
        painter.terminalLettering = false;

        // A "T" instead of text.
        painter.textShowsContents = false;

        // No caption in a preview.
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(area.toNearestInt());
        painter.drawSymbolOnly(g, element, colour);
    }

    //==========================================================================


    juce::String SymbolPainter::formatSupply(double positive, double negative, const juce::String& unit)
    {
        if (negative == 0.0)
            return formatValue(positive, unit);

        // Symmetric rails: "±15V".
        if (juce::approximatelyEqual(positive, -negative))
            return juce::String(juce::CharPointer_UTF8("\xc2\xb1")) + formatValue(positive, unit);

        return "+" + formatValue(positive, unit) + " / " + formatValue(negative, unit);
    }

    juce::String SymbolPainter::formatRatio(double primary, double secondary)
    {
        // No engineering prefixes for a ratio.
        auto number = [](double v)
        {
            juce::String text(v, 3);

            while (text.contains(".") && text.endsWithChar('0'))
                text = text.dropLastCharacters(1);

            if (text.endsWithChar('.'))
                text = text.dropLastCharacters(1);

            return text;
        };

        return number(primary) + ":" + number(secondary);
    }

    juce::String SymbolPainter::formatValue(double value, const juce::String& unit)
    {
        if (value == 0.0)
            return "0" + unit;

        struct Prefix
        {
            double scale;
            const char* suffix;
        };

        // Engineering prefixes.
        static const Prefix prefixes[] = {
            {1.0e12, "T"}, {1.0e9, "G"}, {1.0e6, "M"}, {1.0e3, "k"}, {1.0, ""},
            {1.0e-3, "m"}, {1.0e-6, "u"}, {1.0e-9, "n"}, {1.0e-12, "p"},
        };

        const double magnitude = std::abs(value);

        for (const auto& prefix : prefixes)
        {
            if (magnitude >= prefix.scale * 0.999)
            {
                const double scaled = value / prefix.scale;

                // At most two decimals, no trailing zeros: "4.7k".
                juce::String text(scaled, scaled < 10.0 ? 2 : 1);

                while (text.contains(".") && (text.endsWithChar('0')))
                    text = text.dropLastCharacters(1);

                if (text.endsWithChar('.'))
                    text = text.dropLastCharacters(1);

                return text + prefix.suffix + unit;
            }
        }

        return juce::String(value) + unit;
    }

    juce::String SymbolPainter::formatPotValue(double value, SchematicModel::Taper taper)
    {
        const auto code = SchematicModel::getTaperCode (taper);

        // No value yet: plain "0", so it shows as unset.
        if (value <= 0.0)
            return "0";

        // A space only after numbered taper codes ("10A 250K").
        const auto separator = code.length() > 1 ? " " : "";

        return code + separator + formatValue (value, "").replaceCharacter ('k', 'K');
    }

    bool SymbolPainter::parsePotValue (const juce::String& text, double& valueOut,
                                       SchematicModel::Taper& taperOut, bool& taperGiven)
    {
        auto trimmed = text.trim();
        taperGiven = false;

        // An optional taper code (digits then one letter, like "10A") is peeled off only when
        // something follows it; otherwise the whole text goes to parseValue ("M2" stays 2
        // megohms).
        int split = 0;

        while (split < trimmed.length() && juce::CharacterFunctions::isDigit (trimmed[split]))
            ++split;

        if (split < trimmed.length() - 1
            && SchematicModel::taperFromCode (trimmed.substring (0, split + 1), taperOut))
        {
            taperGiven = true;
            trimmed = trimmed.substring (split + 1).trim();
        }

        return parseValue (trimmed, valueOut);
    }

    bool SymbolPainter::parseValue(const juce::String& text, double& valueOut)
    {
        auto source = text.trim();

        // Upper-case M is mega and lower-case m is milli: handle M before lower-casing.
        if (! source.containsIgnoreCase("meg"))
            source = source.replace("M", "meg", false);

        auto trimmed = source.toLowerCase();

        if (trimmed.isEmpty())
            return false;

        // Strip a trailing unit ("ohm", "F", "V", "s"...). "s" comes after "ohms".
        for (const auto* unit : {"ohm", "ohms", ":1", "f", "h", "v", "s"})
        {
            if (trimmed.endsWith(unit) && trimmed.length() > juce::String(unit).length())
            {
                trimmed = trimmed.dropLastCharacters(juce::String(unit).length()).trim();
                break;
            }
        }

        struct Suffix
        {
            const char* letter;
            double scale;
        };

        static const Suffix suffixes[] = {
            {"t", 1.0e12}, {"g", 1.0e9}, {"meg", 1.0e6}, {"m", 1.0e-3}, {"k", 1.0e3},
            {"u", 1.0e-6}, {"n", 1.0e-9}, {"p", 1.0e-12},
        };

        // "meg" before "m" (SPICE: m is milli, meg is mega).
        for (const auto& suffix : suffixes)
        {
            const juce::String letter(suffix.letter);

            if (! trimmed.contains(letter))
                continue;

            const auto before = trimmed.upToFirstOccurrenceOf(letter, false, false);
            const auto after = trimmed.fromFirstOccurrenceOf(letter, false, false);

            if (before.isEmpty() && after.isEmpty())
                continue;

            // "4k7" means 4.7k.
            const auto digits = after.isEmpty() ? before : before + "." + after;

            if (! digits.containsOnly("0123456789.-+"))
                continue;

            valueOut = digits.getDoubleValue() * suffix.scale;
            return true;
        }

        if (! trimmed.containsOnly("0123456789.-+e"))
            return false;

        valueOut = trimmed.getDoubleValue();
        return true;
    }
} // namespace Celine
