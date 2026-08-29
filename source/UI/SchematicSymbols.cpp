#include "SchematicSymbols.h"

#include "Fonts.h"
#include "SymbolArtwork.h"
#include "Theme.h"

namespace SchematicUI
{
    using namespace SchematicModel;

    namespace
    {
        //======================================================================
        // Caption colours. Three meanings, three colours -- what it is called,
        // what it is set to, and what nobody has set yet.

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
                // A model this build could not find draws in the unset colour,
                // like a value still at zero: both mean "not what it looks
                // like". The name shown is the *missing* one rather than the
                // fallback being simulated, since the discrepancy is what is
                // worth seeing from across the sheet.
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

            if (element.hasSecondValue())
            {
                const bool set = element.value > 0.0 && element.valueB > 0.0;
                out[count++] = {SymbolPainter::formatRatio(element.value, element.valueB),
                                set ? Theme::captionValue() : Theme::captionUnset()};
            }
            else if (element.type == ElementType::Potentiometer)
            {
                // "B10K" -- the taper belongs with the value because on a pot it
                // *is* half the value. Two 250k pots of different taper are
                // different controls and draw as the same symbol, so without
                // this the sheet cannot say which one it means.
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

        /** What a Rectangle's "model" picks: the list in Element.h is colours,
            since which section you are looking at is the whole of what a group
            box has to say. Indexed positionally, so a colour added there needs
            one added here; out of range falls back to the first. */
        juce::Colour rectangleColour(int modelIndex)
        {
            static const juce::Colour colours[] = {
                juce::Colour{0xff8b93a1}, // Grey
                juce::Colour{0xff5b9bd5}, // Blue
                juce::Colour{0xff5fb87a}, // Green
                juce::Colour{0xffd9a441}, // Amber
                juce::Colour{0xffd06666}, // Red
                juce::Colour{0xffa77fd0}, // Violet
            };

            constexpr int count = static_cast<int>(std::size(colours));
            return colours[juce::isPositiveAndBelow(modelIndex, count) ? modelIndex : 0];
        }

        /** How far below its centre an element's lowest pin sits, in grid
            squares. Never less than two, so a one-pin terminal still gets its
            caption clear of the symbol. */
        float lowestPinBelowCentre(const Element& element)
        {
            float lowest = 2.0f;

            for (int pin = 0; pin < getElementInfo(element.type).pinCount; ++pin)
                lowest = juce::jmax(lowest,
                                    static_cast<float>(element.getPinPosition(pin).y - element.y));

            return lowest;
        }

        /** How far below its centre an element's *drawn body* reaches, for the
            few whose symbol hangs lower than any of their pins -- an op-amp's
            triangle, a transformer's core. Only the exceptions are listed:
            saying so for twenty parts would be twenty chances to disagree with
            the drawing code. Rotation is ignored, since the caption is always
            written under the part and upright. */
        float bodyDepthBelowCentre(const Element& element)
        {
            // Deliberately not a switch: the build asks for every enumerator to
            // be named in one, and listing all twenty to say "nothing" nineteen
            // times would bury the two that matter.
            if (element.type == ElementType::OpAmp)
                return 3.0f;

            if (element.type == ElementType::Transformer
                || element.type == ElementType::CenterTapTransformer)
                return 3.2f;

            return 0.0f;
        }

        /** Rotates a point about the origin by an element's orientation, in grid
            units, so symbol geometry can be written once facing one way. */
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

        /** A little drawing context: knows where the element is and which way
            round, and turns symbol-local grid coordinates into pixels. */
        struct LocalSpace
        {
            const SymbolPainter& painter;
            juce::Point<int> centre;
            int orientation;
            bool mirrored;

            juce::Point<float> operator()(float x, float y) const
            {
                // Mirror then rotate, matching Element::getPinPosition -- if the
                // two disagreed, the pins would stop landing on the symbol.
                const auto rotated = rotate(mirrored ? -x : x, y, orientation);
                return painter.toPixel(static_cast<float>(centre.x) + rotated.x,
                                       static_cast<float>(centre.y) + rotated.y);
            }

            void line(juce::Graphics& g, float x1, float y1, float x2, float y2, float thickness) const
            {
                g.drawLine(juce::Line<float>((*this)(x1, y1), (*this)(x2, y2)), thickness);
            }
        };


        /** How far a part's symbol reaches, in grid squares about its centre:
            its artwork's bounds together with its pins.

            Measured rather than stated, because which part is biggest changes
            whenever a symbol is redrawn -- the whole point of symbols being
            files. A number written here would quietly stop being true, and what
            that looks like is a valve with its envelope sliced off.

            Min/max by hand rather than Rectangle::getUnion, which returns the
            other operand when either side is empty and calls a zero-width
            rectangle empty. Same trap as Schematic::getElementBounds. */
        juce::Rectangle<float> symbolExtent(const Element& element)
        {
            // The two pinless parts have neither artwork nor pins, so they come
            // from the same geometry the renderer and the hit test use. Guessing
            // a symmetric box was off by half a square, since
            // getRectangleBounds centres an odd width by truncating.
            if (element.type == ElementType::Rectangle)
            {
                const auto box = element.getRectangleBounds();
                return {static_cast<float>(box.getX() - element.x),
                        static_cast<float>(box.getY() - element.y),
                        static_cast<float>(box.getWidth()), static_cast<float>(box.getHeight())};
            }

            // The single capital T a swatch draws for a Text note, not the note's
            // own words -- this is measured for the palette and the palette shows
            // the letter. Squarish, so it centres like a glyph rather than like
            // the line of text it stands in for.
            if (element.type == ElementType::Text)
                return {-1.2f, -1.2f, 2.4f, 2.4f};

            // A node's tag, in its own frame -- the palette scales this, and the
            // swatch draws an empty one, so the width is whatever an unnamed tag
            // comes to rather than anything a placed node happens to say.
            if (element.type == ElementType::Node)
                return element.getNodeLocalBounds().toFloat();

            // Seeded at the origin, which every symbol is drawn about, so the box
            // always contains the point the pins are measured from.
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

            for (int pin = 0; pin < getElementInfo(element.type).pinCount; ++pin)
            {
                const auto p = element.getPinPosition(pin);
                include({static_cast<float>(p.x - element.x),
                         static_cast<float>(p.y - element.y), 0.0f, 0.0f});
            }

            // A part with no artwork and no pins that isn't one of the two above
            // would otherwise scale by zero. There isn't one today; this is so
            // that adding one is a small symbol rather than a division.
            if (right - left <= 0.0f || bottom - top <= 0.0f)
                return {-1.5f, -1.5f, 3.0f, 3.0f};

            return {left, top, right - left, bottom - top};
        }

        /** How many grid squares a palette swatch is worth. A smaller part is
            drawn at this scale and comes out small; a bigger one is scaled down
            to fit.

            Both halves matter. Fitting every part to the swatch would draw a
            resistor as tall as a pentode, which is a lie -- a power valve really
            is three times a resistor. One scale for all is worse: the pentode is
            the largest by half again, so a resistor ends up eleven pixels tall
            in a thirty-one pixel row.

            Six is what the design draws, to within a pixel. */
        constexpr float previewSquares = 6.0f;

        void drawArrowHead(juce::Graphics& g, juce::Point<float> tip, juce::Point<float> from, float size)
        {
            const auto direction = (tip - from);
            const auto length = direction.getDistanceFromOrigin();

            if (length < 0.0001f)
                return;

            const auto unit = direction / length;
            const juce::Point<float> normal(-unit.y, unit.x);

            juce::Path head;
            head.startNewSubPath(tip);
            head.lineTo(tip - unit * size + normal * size * 0.45f);
            head.lineTo(tip - unit * size - normal * size * 0.45f);
            head.closeSubPath();

            g.fillPath(head);
        }
    } // namespace

    ScopeScale drawScopeTrace (juce::Graphics& g, juce::Rectangle<float> area,
                               const ScopeReading& reading, juce::Colour trace,
                               juce::Colour axis, juce::Colour background)
    {
        if (area.getWidth() < 4.0f || area.getHeight() < 4.0f)
            return {};

        // Painted rather than left transparent: on the sheet this covers the
        // static waveform drawn into scope.svg, which is there so an unbuilt
        // probe still looks like a scope rather than an empty box.
        g.setColour (background);
        g.fillRect (area);

        if (! reading.live)
        {
            // No circuit behind it. Said in words, because a flat line at zero
            // is what a working probe on a grounded node looks like and the two
            // must not be confusable.
            g.setColour (axis);
            g.drawLine (area.getX(), area.getCentreY(), area.getRight(), area.getCentreY(), 1.0f);
            return {};
        }

        // Auto by default, because an amplifier's nodes run from millivolts at
        // the input to hundreds of volts at a plate and no one scale shows both.
        // But auto-ranging is exactly wrong when you are comparing two things:
        // a window that resizes itself to its contents always looks full, so a
        // stage that lost 20 dB and one that did not draw the same picture.
        // Pinning the range is how you tell them apart, so the drawing can say.
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

            // A dead flat trace has no range to scale to, so give it one rather
            // than dividing by zero and drawing a line of infinities.
            centre = 0.5f * (lowest + highest);
            span = juce::jmax (1.0e-6f, highest - lowest) * 1.15f;
        }
        else
        {
            // Taken as given, with no 1.15 headroom: the point of typing a range
            // is that the gridline you asked for lands on the edge of the
            // picture, and quietly widening it by 15% would put it inside.
            centre = 0.5f * (reading.rangeMin + reading.rangeMax);
            span = reading.rangeMax - reading.rangeMin;
        }

        // What a caller has to know to label the vertical axis: the edges of the
        // drawn window, not of the data, since the 1.15 headroom above is part
        // of where the trace actually lands.
        const ScopeScale scale { centre - 0.5f * span, centre + 0.5f * span, true };

        auto yFor = [&] (float value)
        {
            return area.getCentreY() - (value - centre) / span * area.getHeight();
        };

        // The DC level, which is the line you actually bias against.
        g.setColour (axis);
        const auto dcY = yFor (reading.dcAverage);

        if (area.getY() <= dcY && dcY <= area.getBottom())
        {
            const float dashes[] = { 3.0f, 3.0f };
            g.drawDashedLine ({ area.getX(), dcY, area.getRight(), dcY }, dashes, 2, 1.0f);
        }

        // Drawn oldest to newest left to right, starting one past the column
        // being written -- so the newest reading is always at the right edge and
        // the picture scrolls the way every scope does.
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

            // A column whose min and max differ is drawn as a vertical span, not
            // as a point: that is what keeps a transient that lived for one
            // sample visible at a hundred and twenty-eight columns per window.
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

        // A Text note *is* its caption, a Rectangle carries its title inside its
        // own frame, and a Node's name is written inside its tag -- all three
        // drawn in place by drawSymbolOnly. None wants the second copy the
        // caption below would give it, and a box would put that copy an
        // arbitrary distance below its bottom edge, since the clearance below is
        // measured from pins it hasn't got.
        //
        // The node is the strictest of the three: its label is its *only*
        // caption run, so leaving this on would print the name twice and nothing
        // else -- once in the tag and once underneath it.
        if (element.type == ElementType::Text || element.type == ElementType::Rectangle
            || element.type == ElementType::Node)
            return;

        if (gridSize <= 6.0f)
            return;

        // The caption goes down in pieces rather than as one string, because
        // what a part is *called* and what it is *set to* are different kinds of
        // fact and reading them apart at a glance is most of what a schematic is
        // for. Names are yellow; a value someone has entered is green; a value
        // still sitting at zero is red, because that is a part nobody has
        // filled in yet and it will refuse to build.
        CaptionRun runs[3];
        const int count = buildCaptionRuns(element, runs);

        if (count == 0)
            return;

        const juce::Font font(Fonts::light(gridSize * 0.92f));
        g.setFont(font);

        const float captionHeight = gridSize * 1.25f;

        const float space = gridSize * 0.45f;
        float widths[3];
        float total = 0.0f;

        for (int i = 0; i < count; ++i)
        {
            widths[i] = juce::GlyphArrangement::getStringWidth(font, runs[i].text);
            total += widths[i] + (i > 0 ? space : 0.0f);
        }

        // Clear of the symbol rather than a fixed distance below its centre. A
        // valve reaches five grid squares down where a resistor reaches two, so
        // a single offset either buries the caption in the bottle or leaves it
        // floating well away from the small parts.
        const float reach = juce::jmax(lowestPinBelowCentre(element), bodyDepthBelowCentre(element));

        const auto anchor = toPixel(juce::Point<int>(element.x, element.y));

        // One square of air under whatever the symbol actually reaches. It used
        // to be nearly two, measured from the pins alone -- which had to be
        // generous enough to clear the few bodies that hang below their lowest
        // pin, and so left every other part's caption adrift. Measuring the body
        // as well buys that clearance back for the parts that never needed it.
        const float top = anchor.y + gridSize * (reach + 1.0f) - captionHeight * 0.5f;
        float x = anchor.x - total * 0.5f;

        for (int i = 0; i < count; ++i)
        {
            // The alpha rides along so a dimmed element keeps a dimmed caption,
            // while the colour still says what the text means.
            g.setColour(runs[i].colour.withMultipliedAlpha(colour.getFloatAlpha()));
            g.drawText(runs[i].text, juce::Rectangle<float>(x, top, widths[i], captionHeight),
                       juce::Justification::centredLeft, false);
            x += widths[i] + space;
        }
    }

    namespace
    {
        /** The part's body, from assets/icons/elements. Mirror, then rotate, then
            place -- the same order as Element::getPinPosition, or the pins stop
            landing on the terminals. */
        void drawArtwork(juce::Graphics& g, const SymbolPainter& painter, const Element& element)
        {
            const auto* artwork = getSymbolArtwork(element);

            if (artwork == nullptr)
                return;

            const auto placement =
                juce::AffineTransform::scale(element.mirrored ? -painter.gridSize : painter.gridSize, painter.gridSize)
                    .rotated(static_cast<float>(element.orientation & 3)
                             * juce::MathConstants<float>::halfPi)
                    .translated(painter.toPixel(juce::Point<int>(element.x, element.y)));

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
            // Lettering, not artwork: it is read, so it stays upright and
            // gets drawn with a font rather than baked into the flag as
            // outlines that would rotate with the part.
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
            // The wiper is drawn where the knob actually is, rather than
            // parked mid-track: turning a knob in the strip writes back to
            // the part, so the tap slides up and down the body and the
            // sheet shows what you are hearing. A continuous position is
            // not something an asset can carry.
            //
            // Fully clockwise taps pin 0, which is the top of the body --
            // see Potentiometer::setPosition, where position 1 sends the
            // resistance above the wiper to its minimum.
            const float tap = 1.0f - 2.0f * element.getControlPosition();
            const auto tip = s(0.85f, tap);
            const auto tail = s(2.6f, 0.0f);
            g.drawLine(juce::Line<float>(tail, tip), painter.strokeWidth());
            drawArrowHead(g, tip, tail, painter.gridSize * 0.55f);
        }

        void drawNodeTag(juce::Graphics& g, const SymbolPainter& painter,
                             const Element& element, const LocalSpace& s, juce::Colour colour)
        {
            // A tag with the name written inside it, pointing at its pin.
            // The same pentagon the Input terminal has always been, so the
            // two read as one family -- but drawn here rather than loaded,
            // because its width is the width of what it says.
            const auto box = element.getNodeLocalBounds().toFloat();

            // Tip on the pin at {2, 0}, shoulders one square back. Corners
            // through LocalSpace so the tag mirrors and turns exactly as the
            // pin does.
            juce::Path tag;
            tag.startNewSubPath(s(box.getRight(), 0.0f));
            tag.lineTo(s(box.getRight() - 1.0f, box.getY()));
            tag.lineTo(s(box.getX(), box.getY()));
            tag.lineTo(s(box.getX(), box.getBottom()));
            tag.lineTo(s(box.getRight() - 1.0f, box.getBottom()));
            tag.closeSubPath();

            g.strokePath(tag, juce::PathStrokeType(painter.strokeWidth(), juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

            // The palette shows the shape, not a name: the row beside it
            // already reads "Node", and a placeholder set small enough for a
            // swatch is an illegible smudge -- the same call painter.textShowsContents
            // makes for a Text note. A conditional rather than an early
            // return, so the pin dots below still get drawn whatever
            // combination of the painter's flags is set.
            if (painter.textShowsContents)
            {
                // The body's middle, in the part's own frame, then placed.
                // The point is not part of it: centring on the whole tag
                // would push every name a half square towards its own tip.
                const auto centre = s(box.getCentreX() - 0.5f, 0.0f);

                juce::Graphics::ScopedSaveState state(g);

                // A quarter turn, or none. The text turns *with* the tag,
                // unlike IN/OUT -- it has to, because it is inside
                // something: a tag on end is two squares across, and a name
                // left horizontal would run straight out of its own box and
                // over the wire.
                //
                // Never a half turn, though. At 180 degrees the tag points
                // left and the name still reads left to right, because text
                // upside down is not a reading of anything. So the angle
                // comes from whether the part is on end, not from which way
                // it faces.
                if ((element.orientation & 1) != 0)
                    g.addTransform(juce::AffineTransform::rotation(
                        juce::MathConstants<float>::halfPi, centre.x, centre.y));

                g.setFont(Fonts::light(painter.gridSize * 1.1f));

                // Red for an unnamed one, which is the sheet's existing word
                // for "nobody has filled this in" -- and it is the same fact
                // here, since the label is the whole of what a node carries.
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
            // Drawn where it sits rather than through LocalSpace: a note is
            // read, so it stays upright whatever the sheet is doing. That is
            // also why the inspector hides rotate and flip for it.
            const auto centre = painter.toPixel({element.x, element.y});

            if (! painter.textShowsContents)
            {
                // A capital T standing for the type -- see painter.textShowsContents.
                //
                // Two strokes rather than a letter set in a font, which is
                // the one thing that makes it belong: every other swatch in
                // the list is stroked at the schematic's line weight, and a
                // glyph at the size this has to be comes out visibly finer
                // than its neighbours however bold a face it is set in. Drawn
                // it also stays in step as the palette's row height changes,
                // where a font size would have to be re-guessed.
                //
                // The crossbar takes the 1.3x the artwork convention already
                // uses for capacitor plates and valve electrodes, which is
                // what the design draws: two pixels of bar over one and a
                // half of stem.
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

                // Dimmed when it is still the placeholder, so an empty note
                // reads as "type something here" rather than as a part called
                // Text.
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
            // A frame the user drags the corner of, so its size is data and
            // not a drawing. Also in sheet space, like a Text note: an
            // axis-aligned box has nothing for an orientation to do, which
            // is why the inspector hides rotate and flip for it.
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
        drawArtwork(g, *this, element);

        // Everything below is drawn in code rather than loaded, and always for
        // the same reason: it is a *reading* of the part's state rather than a
        // picture of the part, so no file could hold it.
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

        // Pins, so it is obvious where a wire has to land.
        g.setColour(colour.withAlpha(0.9f));
        const float pinRadius = juce::jmax(1.5f, gridSize * 0.18f);

        for (int pin = 0; pin < getElementInfo(element.type).pinCount; ++pin)
        {
            const auto p = toPixel(element.getPinPosition(pin));
            g.fillEllipse(juce::Rectangle<float>(pinRadius * 2.0f, pinRadius * 2.0f).withCentre(p));
        }
    }

    //==========================================================================

    juce::Rectangle<float> SymbolPainter::elementBounds(const Element& element) const
    {
        const juce::Point<float> centre(static_cast<float>(element.x), static_cast<float>(element.y));

        // The three with no artwork keep the rules they already had: a note
        // spans what it reads, a group box *is* its bounds, and a tag is as wide
        // as the name in it.
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
            // Mirror, then rotate, then place -- the same order the symbol is
            // drawn in, and the same order Element::getPinPosition uses. Any
            // other order and the box stops covering the part.
            auto box = artwork->bounds;

            if (element.mirrored)
                box.setX(-box.getRight());

            for (int turn = 0; turn < (element.orientation & 3); ++turn)
                box = { -box.getBottom(), box.getX(), box.getHeight(), box.getWidth() };

            include(box + centre);
        }

        // The pins, so a part is always grabbable by its terminals even if its
        // artwork is missing entirely.
        for (int pin = 0; pin < element.getPinCount(); ++pin)
            include(juce::Rectangle<float>(0.4f, 0.4f).withCentre(element.getPinPosition(pin).toFloat()));

        if (first)
            include({ centre.x - 0.5f, centre.y - 0.5f, 1.0f, 1.0f });

        juce::Rectangle<float> box(minX, minY, maxX - minX, maxY - minY);

        // A floor, because some symbols are genuinely thin: a switch is a line
        // and two dots, which measures 0.4 of a square across. Tracing that
        // exactly would be correct and unusable. One rule for every part rather
        // than a nudge per type, so it cannot drift from the artwork.
        constexpr float minimumSpan = 1.2f;

        if (box.getWidth() < minimumSpan)
            box = box.withWidth(minimumSpan).withX(box.getCentreX() - minimumSpan * 0.5f);

        if (box.getHeight() < minimumSpan)
            box = box.withHeight(minimumSpan).withY(box.getCentreY() - minimumSpan * 0.5f);

        return box;
    }

    void SymbolPainter::drawPreview(juce::Graphics& g, ElementType type, juce::Rectangle<float> area,
                                    juce::Colour colour)
    {
        // A throwaway element at the origin, drawn with the painter's origin
        // moved so it lands in the middle of `area`.
        Element element;
        element.type = type;
        element.x = 0;
        element.y = 0;

        // Small enough to fit the nine grid squares a preview is scaled to. A
        // Rectangle is the one part whose size is up to the user, so it is also
        // the one whose real size means nothing in a palette swatch.
        element.width = 7;
        element.height = 5;

        SymbolPainter painter;

        // Six squares' worth of swatch, or the part's own reach if it is bigger
        // than that -- see previewSquares. The `jmax` is what guarantees nothing
        // is ever cut off: a part at or over six squares is scaled to exactly the
        // room available, and a smaller one to less.
        //
        // The pin dots are drawn on top of the artwork and stick out past it, so
        // the room for them comes out of the swatch rather than out of the symbol.
        // Without that the op-amp's inputs and both transformers' outer windings
        // land on the clip edge and lose a pixel to it.
        const auto extent = symbolExtent(element);
        const float swatch = juce::jmin(area.getWidth(), area.getHeight());
        constexpr float pinDotAllowance = 5.0f;

        const float reach = juce::jmax(previewSquares, extent.getWidth(), extent.getHeight());
        painter.gridSize = juce::jmax(0.5f, (swatch - pinDotAllowance) / reach);

        // Centred on what is actually *drawn* rather than on the element's own
        // origin. An op-amp's triangle sits to the right of its centre and a
        // valve's envelope above it; centring the origin instead leaves those
        // visibly off to one side of the pill, and pushes the far edge out past
        // the clip even when the symbol would have fitted.
        painter.origin = area.getCentre()
                       - juce::Point<float>(extent.getCentreX(), extent.getCentreY())
                             * painter.gridSize;

        // A floor under the line weight, which is what makes these read as
        // symbols rather than as smudges. At the swatch's scale the canvas's own
        // `gridSize * 0.12` comes out at half a pixel, and a half-pixel stroke is
        // rendered as two rows of grey rather than one of ink -- so the whole
        // palette came out pale and thin next to the mockup, which draws it at
        // full contrast.
        painter.minStrokeWidth = 1.5f;

        // See SymbolPainter::terminalLettering -- the row says "Input" already,
        // and the lettering is drawn far enough outside the flag to run off the
        // pill.
        painter.terminalLettering = false;

        // A capital T rather than the word "Text" -- see
        // SymbolPainter::textShowsContents.
        painter.textShowsContents = false;

        // No pin dots -- see SymbolPainter::connectionDots.
        painter.connectionDots = false;

        // No caption in a preview -- the palette already says the name.
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(area.toNearestInt());
        painter.drawSymbolOnly(g, element, colour);
    }

    //==========================================================================


    juce::String SymbolPainter::formatRatio(double primary, double secondary)
    {
        // Plainly, with no engineering prefixes: a turns ratio is dimensionless,
        // so "100m:1" is nonsense where "1:10" is what anyone would write.
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

        // Engineering notation, the way a schematic writes it.
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

                // Two significant decimals at most, and no trailing zeros:
                // "4.7k", not "4.70k".
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

        // Nothing to mark if there is no value yet. The bare "0" keeps the
        // unset-value colouring meaning what it means everywhere else, and a
        // "B0" reads like a part number rather than like something missing.
        if (value <= 0.0)
            return "0";

        // A space only for the numbered tapers. "A250K" is how the part is
        // marked and stays exactly that; "10A250K" is not a marking of
        // anything, and run together it reads as one long number.
        const auto separator = code.length() > 1 ? " " : "";

        return code + separator + formatValue (value, "").replaceCharacter ('k', 'K');
    }

    bool SymbolPainter::parsePotValue (const juce::String& text, double& valueOut,
                                       SchematicModel::Taper& taperOut, bool& taperGiven)
    {
        auto trimmed = text.trim();
        taperGiven = false;

        // The code is an optional run of digits followed by one letter --
        // "A", "C", "10A". Peeled only when something follows it, so "A" on its
        // own stays what it always was: not a value, and refused.
        //
        // Taking the digits first is what keeps "10A250K" from being read as
        // the number 10: a leading number is part of the code only when a taper
        // letter closes it, and otherwise the whole string goes to parseValue
        // untouched. That is also why "M2" still parses as 2 megohms -- M names
        // no taper, so nothing is peeled.
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

        // Upper-case M is mega, lower-case m is milli, and the distinction has
        // to survive the toLowerCase below -- so it is spelled out first.
        //
        // This is not a nicety. formatValue *writes* an upper-case M for mega,
        // so without this a 2.2M resistor read straight back off its own editor
        // parsed as 2.2 milliohms: opening the inspector on a part and pressing
        // return, changing nothing, turned a 2M2 grid leak into a short.
        // "meg" keeps meaning mega whatever case it arrives in, since that is
        // what a netlist writes.
        if (! source.containsIgnoreCase("meg"))
            source = source.replace("M", "meg", false);

        auto trimmed = source.toLowerCase();

        if (trimmed.isEmpty())
            return false;

        // Drop a trailing unit letter so "10k ohm", "100nF", "9V" and "40ms"
        // all work.
        //
        // "s" goes last, after "ohms", because the loop stops at the first
        // match and stripping the s off "10 ohms" would leave "10 ohm" for the
        // suffix scan to choke on. It has to be here at all because formatValue
        // *writes* seconds -- a scope's time span is displayed as "40ms", and
        // without this that text did not read back as anything.
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

        // "meg" before "m", since SPICE means milli by "m" and mega by "meg" --
        // and a capacitor typed as "10m" is far more likely to be millifarads.
        for (const auto& suffix : suffixes)
        {
            const juce::String letter(suffix.letter);

            if (! trimmed.contains(letter))
                continue;

            const auto before = trimmed.upToFirstOccurrenceOf(letter, false, false);
            const auto after = trimmed.fromFirstOccurrenceOf(letter, false, false);

            if (before.isEmpty() && after.isEmpty())
                continue;

            // "4k7" means 4.7k -- the decimal point standing in for the suffix,
            // which is how values are written on a real schematic.
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
} // namespace SchematicUI
