#include "EditorPanels.h"

#include <numeric>

#include "PluginLookAndFeel.h"
#include <CelineUI/EmbeddedAssets.h>
#include <CelineUI/Fonts.h>
#include <CelineUI/Theme.h>

namespace Celine
{
    using namespace SchematicModel;

    namespace
    {

        /** Every type, in the order the palette lists them, after the wire.
            Written in pairs, and each line here is one row of the panel.
            Ground pairs with the wire, the first two things any drawing needs,
            and a blank sheet gives no other clue that ground is a part you
            place. */
        constexpr ElementType paletteOrder[] = {
            /* Wire */                  ElementType::Ground,
            ElementType::Input,         ElementType::Output,
            ElementType::Node,          ElementType::VoltageSource,
            ElementType::Resistor,      ElementType::Potentiometer,
            ElementType::Capacitor,     ElementType::Inductor,
            ElementType::Switch,        ElementType::Spdt,
            ElementType::Diode,         ElementType::VacuumDiode,
            ElementType::OpAmp,         ElementType::PowerAmp,
            ElementType::Transistor,    ElementType::Jfet,
            ElementType::Mosfet,        ElementType::Scope,
            ElementType::Triode,        ElementType::Pentode,
            ElementType::Transformer,   ElementType::CenterTapTransformer,
            ElementType::Rectangle,     ElementType::Text,
        };

        constexpr int paletteCount = static_cast<int>(std::size(paletteOrder));
        static_assert(paletteCount == numElementTypes, "every element type should be reachable from the palette");

        /** The panel's own padding, the same on all four sides -- it was a
            pixel less top and bottom than at the sides, which read as the
            buttons sitting high and off-centre in the card -- and the gap
            between its two columns. The gap between rows is in
            Theme::paletteRowHeight. */
        constexpr int palettePad = 10;
        constexpr int paletteColumnGap = 4;
        constexpr int paletteEntryHeight = Theme::paletteRowHeight - 4;

        /** The tab standing off the panel's right-hand edge: how far out, how
            tall, and how round its outer corners are. */
        constexpr int paletteTabWidth = 14;
        constexpr int paletteTabHeight = 44;
        constexpr float paletteTabCornerRadius = 6.0f;

        /** After the lettering, so the longest name in a column does not run
            into the end of its pill. */
        constexpr int paletteTextTrailing = 10;

        /** Item id for the "model this build hasn't got" entry in the model
            dropdown. Well clear of the real ones, which are `modelIndex + 1`
            and so run from 1 upwards. */
        constexpr int missingModelItemId = 10000;

        /** The inspector's shortcut list, at most -- its lettering comes down
            from here only if that is what it takes for the longest line to fit
            the card. */
        constexpr float hintFontHeight = 13.0f;

        /** A reading drawn in its box at the largest size, up to `largest`,
            at which it fits: a third of a narrow card is not much room, and a
            number clipped at the edge reads as a different number. */
        void drawReading(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> box,
                         float largest)
        {
            float height = largest;

            while (height > 8.0f
                   && juce::GlyphArrangement::getStringWidth(Fonts::light(height), text) > box.getWidth() - 4.0f)
                height -= 0.5f;

            g.setFont(Fonts::light(height));
            g.drawText(text, box, juce::Justification::centred, false);
        }
    } // namespace

    juce::String ElementInspector::formatReading(double value, const juce::String& unit)
    {
        struct Prefix
        {
            double scale;
            const char* suffix;
        };

        static const Prefix prefixes[] = {
            {1.0e12, "T"}, {1.0e9, "G"}, {1.0e6, "M"}, {1.0e3, "k"}, {1.0, ""},
            {1.0e-3, "m"}, {1.0e-6, "u"}, {1.0e-9, "n"}, {1.0e-12, "p"},
        };

        if (! std::isfinite(value))
            return "--" + unit;

        // The largest prefix the value reaches once rounded to two decimals --
        // so 0.999 V reads "1.00V" rather than "999.00mV" -- and pico for
        // anything smaller.
        const auto* prefix = &prefixes[std::size(prefixes) - 1];

        for (const auto& candidate : prefixes)
        {
            if (std::abs(value) >= candidate.scale * 0.995)
            {
                prefix = &candidate;
                break;
            }
        }

        const double scaled = value / prefix->scale;

        // Nothing to read, or so little it rounds to nothing: plain zero, with
        // no prefix and no sign -- "-0.00pV" says something that isn't so.
        if (std::abs(scaled) < 0.005)
            return "0.00" + unit;

        return juce::String(scaled, 2) + prefix->suffix + unit;
    }

    //==========================================================================
    // Palette
    //==========================================================================

    class ElementPalette::Entry : public juce::Button
    {
       public:
        /** The wire row: no element type, and it draws its own symbol. */
        Entry() : juce::Button("Wire"), isWire(true)
        {
            setTooltip("Draw wires (W)");
            setWantsKeyboardFocus(false);
        }

        explicit Entry(ElementType t) : juce::Button(getElementInfo(t).name), type(t)
        {
            setTooltip(getElementInfo(t).name);

            // Arming a part must not move keyboard focus off the canvas. It used
            // to: clicking a palette entry gave the button focus, so R and F
            // then went to the button and the ghost would not turn.
            setWantsKeyboardFocus(false);
        }

        juce::String getLabel() const { return isWire ? "Wire" : getElementInfo(type).name; }

        /** The name's width at a given size, for finding the size at which
            every name fits its column. */
        float getNameWidth(float size) const
        {
            return juce::GlyphArrangement::getStringWidth(Fonts::light(size), getLabel());
        }

        /** The size the names are drawn at, the same for every entry -- see
            ElementPalette::getNameFontHeight. */
        float fontHeight = Theme::paletteFontHeight;

        void paintButton(juce::Graphics& g, bool highlighted, bool down) override
        {
            auto bounds = getLocalBounds().toFloat();

            // A filled pill per entry, on the light shelf, so the palette reads as
            // a set of buttons at rest and not only under the mouse.
            g.setColour(active            ? Theme::accent()
                        : down            ? Theme::pill().darker(0.12f)
                        : highlighted     ? Theme::pill().darker(0.05f)
                                          : Theme::pill());
            g.fillRoundedRectangle(bounds, Theme::paletteEntryCornerRadius);

            // Ink is dark here: this is one of the two light surfaces, and
            // Theme::text() on it is invisible -- which is exactly what the
            // first pass of this repalette looked like.
            const auto ink = active ? Theme::text() : Theme::textOnPanel();

            // Symbol on the left, name beside it. The swatch is square on the
            // row height, which is what sets how big the symbol is drawn.
            auto symbolArea = bounds.removeFromLeft(bounds.getHeight());

            if (isWire)
            {
                // An elbow, which is what the wire tool actually draws.
                const auto area = symbolArea.reduced(7.0f);
                g.setColour(ink);
                g.drawLine(area.getX(), area.getBottom(), area.getCentreX(), area.getBottom(), 1.5f);
                g.drawLine(area.getCentreX(), area.getBottom(), area.getCentreX(), area.getY(), 1.5f);
                g.drawLine(area.getCentreX(), area.getY(), area.getRight(), area.getY(), 1.5f);
            }
            else
            {
                SymbolPainter::drawPreview(g, type, symbolArea, ink);
            }

            // Icons only, the pill is the swatch and nothing is left for the
            // name -- which the tooltip still gives.
            if (! iconOnly)
            {
                g.setColour(ink);
                g.setFont(Fonts::light(fontHeight));
                g.drawText(getLabel(), bounds, juce::Justification::centredLeft, true);
            }
        }

        ElementType type{};
        bool isWire = false;
        bool active = false;
        bool iconOnly = false;
    };

    //==========================================================================

    /** The arrow on the panel's edge that folds it to symbols and back.

        Drawn as a tab standing off the panel rather than as a button inside
        it, so it reads as a handle on the panel instead of one more part. */
    class ElementPalette::CollapseTab : public juce::Button
    {
       public:
        CollapseTab() : juce::Button("Fold palette")
        {
            // Like every palette entry: the canvas keeps the keyboard.
            setWantsKeyboardFocus(false);
            setCollapsed(false);
        }

        void setCollapsed(bool isNowCollapsed)
        {
            collapsed = isNowCollapsed;
            setTooltip(collapsed ? "Show the parts' names" : "Show the parts as symbols only");
            repaint();
        }

        void paintButton(juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto bounds = getLocalBounds().toFloat();

            // Square where it meets the panel, so the two read as one piece;
            // rounded on the side that stands free.
            juce::Path shape;
            shape.addRoundedRectangle(bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
                                      paletteTabCornerRadius, paletteTabCornerRadius,
                                      false, true, false, true);

            g.setColour(down          ? Theme::pill()
                        : highlighted ? Theme::pill().brighter(0.4f)
                                      : Theme::panel());
            g.fillPath(shape);

            // A chevron pointing the way the panel will go: in towards the
            // edge to fold, back out to unfold.
            const auto centre = bounds.getCentre();
            const float half = 4.0f;
            const float reach = 2.5f;
            const float pointX = collapsed ? centre.x + reach : centre.x - reach;
            const float backX = collapsed ? centre.x - reach : centre.x + reach;

            juce::Path chevron;
            chevron.startNewSubPath(backX, centre.y - half);
            chevron.lineTo(pointX, centre.y);
            chevron.lineTo(backX, centre.y + half);

            g.setColour(Theme::textOnPanel());
            g.strokePath(chevron, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

       private:
        bool collapsed = false;
    };

    //==========================================================================

    ElementPalette::ElementPalette()
    {
        // Wire first: it is the thing you reach for most, and it is the one
        // entry whose position never changes as parts are added.
        {
            auto* wire = entries.add(new Entry());
            wire->onClick = [this] { if (onWireChosen) onWireChosen(); };
            content.addAndMakeVisible(wire);
        }

        for (const auto type : paletteOrder)
        {
            auto* entry = entries.add(new Entry(type));
            entry->onClick = [this, type] { if (onTypeChosen) onTypeChosen(type); };
            content.addAndMakeVisible(entry);
        }

        // One size for every name, the largest at which the longest still fits
        // its column -- so the list reads as a set rather than as names
        // shrunk one by one.
        nameFontHeight = fittingFontHeight();

        for (auto* entry : entries)
            entry->fontHeight = nameFontHeight;

        // Vertical only: the list is exactly as wide as its two columns.
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setWantsKeyboardFocus(false);
        addAndMakeVisible(viewport);

        tab = std::make_unique<CollapseTab>();
        tab->onClick = [this]
        {
            setCollapsed(! collapsed);

            if (onCollapsedChanged)
                onCollapsedChanged();
        };
        addAndMakeVisible(*tab);

        // Rounded, and with a tab standing off one edge, so the sheet shows
        // at the corners and beside the tab.
        setOpaque(false);
    }

    ElementPalette::~ElementPalette() = default;

    void ElementPalette::setActive(const ElementType* type, bool wire)
    {
        for (auto* entry : entries)
            entry->active = entry->isWire ? wire
                                          : (type != nullptr && entry->type == *type);

        repaint();
    }

    void ElementPalette::setCollapsed(bool shouldBeCollapsed)
    {
        if (collapsed == shouldBeCollapsed)
            return;

        collapsed = shouldBeCollapsed;
        tab->setCollapsed(collapsed);

        for (auto* entry : entries)
        {
            entry->iconOnly = collapsed;
            entry->repaint();
        }

        resized();
        repaint();
    }

    float ElementPalette::fittingFontHeight() const
    {
        // The room a name has: the column, less the symbol's square and the
        // air after the lettering.
        constexpr float room = static_cast<float>(Theme::paletteColumnWidth - paletteEntryHeight - paletteTextTrailing);

        auto widest = [this](float height)
        {
            float width = 0.0f;

            for (const auto* entry : entries)
                width = juce::jmax(width, entry->getNameWidth(height));

            return width;
        };

        // Lettering is not quite proportional to its size -- glyphs are hinted
        // to the pixel -- so the estimate is checked, and stepped down by
        // half a point until the longest name really fits.
        float height = Theme::paletteFontHeight;
        const float atFull = widest(height);

        if (atFull > room)
            height = std::floor(height * room / atFull * 2.0f) / 2.0f;

        while (height > 6.0f && widest(height) > room)
            height -= 0.5f;

        return height;
    }

    std::vector<juce::Rectangle<int>> ElementPalette::layOutGrid() const
    {
        // Two to a row, always: a symbol's square each when folded, the fixed
        // column otherwise -- the names are sized to fit it rather than the
        // other way round, since a name given a row of its own left the grid
        // ragged.
        const int column = collapsed ? paletteEntryHeight : Theme::paletteColumnWidth;

        std::vector<juce::Rectangle<int>> cells;
        cells.reserve(static_cast<size_t>(entries.size()));

        for (int i = 0; i < entries.size(); ++i)
            cells.push_back({palettePad + (i % 2) * (column + paletteColumnGap),
                             palettePad + (i / 2) * Theme::paletteRowHeight,
                             column, paletteEntryHeight});

        return cells;
    }

    int ElementPalette::listWidth() const
    {
        const int column = collapsed ? paletteEntryHeight : Theme::paletteColumnWidth;
        return 2 * palettePad + 2 * column + paletteColumnGap;
    }

    int ElementPalette::listHeight() const
    {
        const auto cells = layOutGrid();
        const int lastBottom = cells.empty() ? palettePad : cells.back().getBottom();
        return lastBottom + palettePad;
    }

    juce::Point<int> ElementPalette::getPreferredSize(int maxHeight) const
    {
        const int height = listHeight();

        if (height <= maxHeight)
            return {listWidth() + paletteTabWidth, height};

        // Scrolling: the bar takes its width beside the rows, not out of them.
        return {listWidth() + viewport.getScrollBarThickness() + paletteTabWidth, juce::jmax(0, maxHeight)};
    }

    int ElementPalette::getFoldedBodyWidth() noexcept
    {
        return 2 * palettePad + 2 * paletteEntryHeight + paletteColumnGap;
    }

    int ElementPalette::getMaximumWidth() const
    {
        return 2 * palettePad + 2 * Theme::paletteColumnWidth + paletteColumnGap
               + viewport.getScrollBarThickness() + paletteTabWidth;
    }

    bool ElementPalette::isScrolling() const
    {
        return listHeight() > getHeight();
    }

    juce::Rectangle<int> ElementPalette::getBodyBounds() const
    {
        return getLocalBounds().withTrimmedRight(paletteTabWidth);
    }

    bool ElementPalette::hitTest(int x, int y)
    {
        return getBodyBounds().contains(x, y) || tab->getBounds().contains(x, y);
    }

    void ElementPalette::paint(juce::Graphics& g)
    {
        g.setColour(Theme::panel());
        g.fillRoundedRectangle(getBodyBounds().toFloat(), Theme::paletteCornerRadius);
    }

    void ElementPalette::resized()
    {
        const auto body = getBodyBounds();

        viewport.setBounds(body);
        content.setSize(listWidth(), listHeight());

        const auto cells = layOutGrid();

        for (int i = 0; i < entries.size(); ++i)
            entries[i]->setBounds(cells[static_cast<size_t>(i)]);

        // Halfway down the edge, or the panel's whole height when it is
        // shorter than the tab.
        const int tabHeight = juce::jmin(paletteTabHeight, body.getHeight());
        tab->setBounds(body.getRight(), body.getCentreY() - tabHeight / 2, paletteTabWidth, tabHeight);
    }

    //==========================================================================
    // Inspector
    //==========================================================================

    std::array<juce::Label*, 14> ElementInspector::captionLabels()
    {
        return {&valueCaption, &valueCaptionB, &labelCaption, &modelCaption, &taperCaption,
                &orderCaption, &knobCaption, &widthCaption, &heightCaption,
                &scopeMinCaption, &scopeMaxCaption, &scopeSecondsCaption, &cabFileCaption,
                &inputFileCaption};
    }

    void ElementInspector::applyColours()
    {
        // The card is the light half of the house design, like the palette on
        // the other side of the sheet, so everything on it takes the panel's
        // dark ink. Fields are a step off the card -- the palette's pill.
        const auto ink = Theme::textOnPanel();
        const auto well = Theme::pill().withMultipliedAlpha(0.7f);

        for (auto* caption : captionLabels())
            caption->setColour(juce::Label::textColourId, ink.withAlpha(0.55f));

        for (auto* editor : {&valueEditor, &valueEditorB, &labelEditor, &orderEditor, &knobEditor,
                             &widthEditor, &heightEditor, &scopeMinEditor, &scopeMaxEditor,
                             &scopeSecondsEditor})
        {
            editor->setColour(juce::TextEditor::backgroundColourId, well);
            editor->setColour(juce::TextEditor::textColourId, ink);
            editor->setColour(juce::TextEditor::highlightColourId, Theme::accent().withAlpha(0.3f));
            editor->setColour(juce::TextEditor::highlightedTextColourId, ink);
            editor->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
            editor->setColour(juce::TextEditor::focusedOutlineColourId, Theme::accent());
            editor->setColour(juce::CaretComponent::caretColourId, ink);
            editor->applyColourToAllText(ink);
        }

        // The two value fields go red, box and lettering, while they still
        // hold nothing -- the same red the part's caption wears on the sheet.
        markUnsetFields();

        for (auto* box : {&modelBox, &taperBox})
        {
            box->setColour(juce::ComboBox::backgroundColourId, well);
            box->setColour(juce::ComboBox::textColourId, ink);
            box->setColour(juce::ComboBox::arrowColourId, ink.withAlpha(0.5f));
            box->setColour(juce::ComboBox::outlineColourId, ink);
        }

        for (auto* button : {&cabFileButton, &inputFileButton, &playButton, &stopButton})
        {
            button->setColour(juce::TextButton::buttonColourId, well);
            button->setColour(juce::TextButton::textColourOffId, ink);
        }

        // Play lights while the file runs, which is the one thing about the
        // transport you want to see from across the room.
        playButton.setColour(juce::TextButton::buttonOnColourId, Theme::accent());
        playButton.setColour(juce::TextButton::textColourOnId, Theme::panel());

        for (auto* toggle : {&polarisedButton, &cabButton, &scopeAutoButton, &fileInputButton, &loopButton})
            toggle->setColour(juce::ToggleButton::textColourId, ink);

        modelDescription.setColour(juce::Label::textColourId, Theme::captionUnset());
        hintLabel.setColour(juce::Label::textColourId, ink.withAlpha(0.6f));

        viewport.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId, ink.withAlpha(0.25f));
        repaint();
    }

    void ElementInspector::markUnsetFields()
    {
        const auto mark = [](juce::TextEditor& editor, bool unset)
        {
            const auto ink = unset ? Theme::captionUnset() : Theme::textOnPanel();
            editor.setColour(juce::TextEditor::textColourId, ink);
            editor.applyColourToAllText(ink);
            editor.setColour(juce::TextEditor::outlineColourId,
                             unset ? Theme::captionUnset().withAlpha(0.6f) : juce::Colours::transparentBlack);
            editor.repaint();
        };

        const bool numeric = element != nullptr && element->hasNumericValue();
        mark(valueEditor, numeric && element->value <= 0.0);
        mark(valueEditorB, numeric && element->hasSecondValue() && ! element->isSecondValueRail()
                               && element->valueB <= 0.0);
    }

    ElementInspector::ElementInspector()
    {
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setWantsKeyboardFocus(false);
        addAndMakeVisible(viewport);

        // Small capitals over each field, spaced out a little, as the iPad
        // designer draws them. Flush with the field's left edge: the field
        // indents its own text.
        for (auto* caption : captionLabels())
        {
            caption->setFont(Fonts::bold(11.0f).withExtraKerningFactor(0.08f));
            caption->setBorderSize(juce::BorderSize<int>(0));
            content.addAndMakeVisible(caption);
        }

        for (auto* editor : {&valueEditor, &valueEditorB, &labelEditor, &orderEditor, &knobEditor,
                             &widthEditor, &heightEditor, &scopeMinEditor, &scopeMaxEditor,
                             &scopeSecondsEditor})
        {
            editor->setFont(Fonts::light(14.0f));
            editor->setIndents(10, 0);
            editor->setJustification(juce::Justification::centredLeft);
            editor->getProperties().set(inspectorFieldProperty, true);
        }

        for (juce::Component* c : std::initializer_list<juce::Component*>{
                 &modelBox, &taperBox, &polarisedButton, &cabButton, &scopeAutoButton,
                 &fileInputButton, &loopButton})
            c->getProperties().set(inspectorFieldProperty, true);


        scopeMinCaption.setText("Volts, bottom", juce::dontSendNotification);
        scopeMaxCaption.setText("Volts, top", juce::dontSendNotification);
        scopeSecondsCaption.setText("Time span", juce::dontSendNotification);

        scopeAutoButton.getProperties().set(pillSwitchProperty, true);
        scopeAutoButton.onClick = [this] { commitScopeAxes(); };
        content.addAndMakeVisible(scopeAutoButton);

        for (auto* editor : {&scopeMinEditor, &scopeMaxEditor, &scopeSecondsEditor})
        {
            editor->onReturnKey = [this] { commitScopeAxes(); };
            editor->onFocusLost = [this] { commitScopeAxes(); };
            content.addAndMakeVisible(*editor);
        }

        valueCaption.setText("Value", juce::dontSendNotification);
        labelCaption.setText("Label", juce::dontSendNotification);
        modelCaption.setText("Model", juce::dontSendNotification);
        taperCaption.setText("Taper", juce::dontSendNotification);

        // Committing on Enter and on focus loss both matter: people type a value
        // and then click straight onto the canvas.
        valueEditor.onReturnKey = [this] { commitValue(); };
        valueEditor.onFocusLost = [this] { commitValue(); };
        content.addAndMakeVisible(valueEditor);

        valueEditorB.onReturnKey = [this] { commitValue(); };
        valueEditorB.onFocusLost = [this] { commitValue(); };
        content.addAndMakeVisible(valueEditorB);

        orderCaption.setText("Panel position", juce::dontSendNotification);
        orderEditor.onReturnKey = [this] { commitOrder(); };
        orderEditor.onFocusLost = [this] { commitOrder(); };
        content.addAndMakeVisible(orderEditor);

        knobCaption.setText("Knob position (%)", juce::dontSendNotification);
        knobEditor.onReturnKey = [this] { commitKnob(); };
        knobEditor.onFocusLost = [this] { commitKnob(); };
        content.addAndMakeVisible(knobEditor);

        widthCaption.setText("Width (squares)", juce::dontSendNotification);
        heightCaption.setText("Height (squares)", juce::dontSendNotification);

        for (auto* editor : {&widthEditor, &heightEditor})
        {
            editor->onReturnKey = [this] { commitSize(); };
            editor->onFocusLost = [this] { commitSize(); };
            content.addAndMakeVisible(editor);
        }

        labelEditor.onReturnKey = [this] { labelEditor.giveAwayKeyboardFocus(); };
        labelEditor.onTextChange = [this]
        {
            if (element != nullptr)
            {
                element->label = labelEditor.getText();

                if (onEdited)
                    onEdited();
            }
        };
        content.addAndMakeVisible(labelEditor);

        modelBox.onChange = [this]
        {
            // The missing-model entry is disabled, so nothing can select it and
            // its id never arrives here.
            if (element != nullptr && modelBox.getSelectedId() > 0
                  && modelBox.getSelectedId() != missingModelItemId)
            {
                element->modelIndex = modelBox.getSelectedId() - 1;

                // Choosing a model is the gesture that settles an unresolved
                // one, whether that means accepting the substitute or picking
                // something else. Either way the sheet stops asking for a model
                // this build hasn't got.
                element->unresolvedModelId.clear();
                headerSubtitle = headerSubtitleFor(*element);
                modelDescription.setText({}, juce::dontSendNotification);
                showFieldsFor();
                contentChanged();
                repaint();

                if (onEdited)
                    onEdited();
            }
        };
        content.addAndMakeVisible(modelBox);

        modelDescription.setFont(Fonts::light(12.0f));
        modelDescription.setBorderSize(juce::BorderSize<int>(0));
        modelDescription.setJustificationType(juce::Justification::topLeft);
        content.addAndMakeVisible(modelDescription);

        for (const auto& name : getTaperNames())
            taperBox.addItem(name, taperBox.getNumItems() + 1);

        taperBox.onChange = [this]
        {
            if (element != nullptr && taperBox.getSelectedId() > 0)
            {
                element->taper = static_cast<Taper>(taperBox.getSelectedId() - 1);

                if (onEdited)
                    onEdited();
            }
        };
        content.addAndMakeVisible(taperBox);

        polarisedButton.getProperties().set(pillSwitchProperty, true);
        cabButton.getProperties().set(pillSwitchProperty, true);
        polarisedButton.onClick = [this]
        {
            if (element != nullptr)
            {
                element->polarised = polarisedButton.getToggleState();

                if (onEdited)
                    onEdited();
            }
        };
        content.addAndMakeVisible(polarisedButton);

        // The cabinet. Announced through its own callback, which is what keeps
        // the Rebuild button out of it -- see ElementInspector::onCabChanged.
        cabButton.onClick = [this]
        {
            if (element == nullptr || element->type != ElementType::Output)
                return;

            element->cabEnabled = cabButton.getToggleState();

            if (onCabChanged)
                onCabChanged();
        };
        content.addAndMakeVisible(cabButton);

        content.addAndMakeVisible(scopeView);
        content.addAndMakeVisible(currentView);

        cabFileCaption.setText("File", juce::dontSendNotification);
        cabFileButton.onClick = [this] { if (onCabFileRequested) onCabFileRequested(); };
        content.addAndMakeVisible(cabFileCaption);
        content.addAndMakeVisible(cabFileButton);

        // The file player. Every button goes straight to the player and then
        // re-reads it, so what the panel shows is what the player says rather
        // than what was clicked.
        fileInputButton.getProperties().set(pillSwitchProperty, true);
        loopButton.getProperties().set(pillSwitchProperty, true);

        fileInputButton.setTooltip("Feed the circuit from the loaded file instead of the input. "
                                   "Not saved with the sheet.");
        inputFileButton.setTooltip("Choose an audio file to play through the circuit");
        loopButton.setTooltip("Play the file over and over instead of once");
        playButton.setTooltip("Play the file through the circuit");
        stopButton.setTooltip("Stop the file and go back to its start");

        fileInputButton.onClick = [this]
        {
            if (filePlayer != nullptr)
                filePlayer->setFileMode(fileInputButton.getToggleState());

            syncFilePlayer();
        };

        loopButton.onClick = [this]
        {
            if (filePlayer != nullptr)
                filePlayer->setLooping(loopButton.getToggleState());

            syncFilePlayer();
        };

        playButton.onClick = [this]
        {
            if (filePlayer != nullptr)
                filePlayer->play();

            syncFilePlayer();
        };

        stopButton.onClick = [this]
        {
            if (filePlayer != nullptr)
                filePlayer->stop();

            syncFilePlayer();
        };

        inputFileCaption.setText("File", juce::dontSendNotification);
        inputFileButton.onClick = [this] { if (onInputFileRequested) onInputFileRequested(); };

        for (juce::Component* c : std::initializer_list<juce::Component*>{
                 &fileInputButton, &inputFileButton, &loopButton, &playButton, &stopButton, &transportView})
            content.addAndMakeVisible(c);

        hintLabel.setFont(Fonts::light(hintFontHeight));
        hintLabel.setJustificationType(juce::Justification::topLeft);
        content.addAndMakeVisible(hintLabel);

        // A rounded card: the sheet shows at the corners.
        setOpaque(false);

        applyColours();
        setElement(nullptr);
    }

    void ElementInspector::setElement(Element* newElement)
    {
        element = newElement;

        // Before the size is announced, so the editor lays the window out
        // knowing whether the card is there.
        setVisible(element != nullptr || shortcutsShown);
        rebuildForElement();
        contentChanged();
        repaint();
    }

    void ElementInspector::setShortcutsShown(bool shouldShow)
    {
        if (shortcutsShown == shouldShow)
            return;

        shortcutsShown = shouldShow;
        setVisible(element != nullptr || shortcutsShown);
        contentChanged();
    }

    void ElementInspector::contentChanged()
    {
        resized();

        if (onPreferredSizeChanged)
            onPreferredSizeChanged();
    }

    juce::String ElementInspector::headerSubtitleFor(const Element& part)
    {
        // A box's "models" are colours, and its description is an instruction,
        // not a fact about the part. A model the build has not got is described
        // under the model box instead, in red.
        if (! part.hasModelChoice() || part.type == ElementType::Rectangle
            || part.unresolvedModelId.isNotEmpty())
            return {};

        return getModelDescription(part.type, part.modelIndex);
    }

    void ElementInspector::showNothingSelected()
    {
        headerTitle = "Nothing selected";
        headerSubtitle = "Keyboard and mouse shortcuts";
        headerHasSymbol = false;
        hintLabel.setText(  "S  select tool\n"
                            "X  delete tool\n"
                            "F  fit to screen\n\n"
                            "W  wire\n"
                            "G  ground\n"
                            "I  input\n"
                            "O  output\n\n"
                            "R  resistor\n"
                            "C  capacitor\n"
                            "L  inductor\n"
                            "D  diode\n"
                            "T  transistor\n"
                            "B  box\n\n"
                            "Ctrl/Cmd-R  rotate\n"
                            "Ctrl/Cmd-F  flip\n\n"
                            "Delete  removes the selection.\n\n"
                            "Right-drag to move the canvas.\n"
                            "Scroll to adjust zoom.\n"
                            "Middle-click to copy an element.",
                            juce::dontSendNotification);

        fitHintToWidth();
    }

    /** Fills the model dropdown, grouped as the table groups it and sorted by
        name inside each group. The item id stays `index + 1` however the display
        is reordered, so this is a view over the table rather than an ordering of
        it.

        compareNatural because these are part numbers: it reads digit runs as
        numbers, so 1N914 sorts before 1N4148. Group order is left as the table
        authors it. */
    void ElementInspector::populateModelBox(const Element& part)
    {
        const auto choices = getModelChoices(part.type);
        const auto groups = getModelGroups(part.type);
        modelBox.clear(juce::dontSendNotification);

        const auto byName = [&choices](int a, int b)
        { return choices[a].compareNatural(choices[b]) < 0; };

        std::vector<int> order(static_cast<size_t>(choices.size()));
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), byName);

        if (groups.isEmpty())
        {
            for (const auto i : order)
                modelBox.addItem(choices[i], i + 1);
        }
        else
        {
            for (const auto& group : groups)
            {
                modelBox.addSectionHeading(group);

                for (const auto i : order)
                    if (getModelGroup(part.type, i) == group)
                        modelBox.addItem(choices[i], i + 1);
            }
        }

        // A model this build could not find gets its own greyed entry at the
        // top. The console said so at load, but console lines scroll away and
        // the inspector does not. Disabled because there is nothing to select.
        if (part.unresolvedModelId.isNotEmpty())
        {
            modelBox.addItem(part.unresolvedModelId + " (missing)", missingModelItemId);
            modelBox.setItemEnabled(missingModelItemId, false);
            modelBox.setSelectedId(missingModelItemId, juce::dontSendNotification);
            modelDescription.setText("Saved as \"" + part.unresolvedModelId + "\", a model this build "
                                         "doesn't have. The part is using " + choices[0] + " instead.",
                                     juce::dontSendNotification);
        }
        else
        {
            modelBox.setSelectedId(part.modelIndex + 1, juce::dontSendNotification);
            modelDescription.setText({}, juce::dontSendNotification);
        }
    }

    void ElementInspector::fillFieldsFrom(const Element& part)
    {
        const auto& info = getElementInfo(part.type);
        headerTitle = info.name;
        headerSubtitle = headerSubtitleFor(part);
        headerHasSymbol = part.getPinCount() > 0 || part.type == ElementType::Rectangle;
        headerType = part.type;
        hintLabel.setText({}, juce::dontSendNotification);
        modelDescription.setText({}, juce::dontSendNotification);

        if (part.hasNumericValue())
        {
            valueCaption.setText(info.valueLabel, juce::dontSendNotification);

            if (part.hasSecondValue())
            {
                valueCaptionB.setText(info.valueLabelB, juce::dontSendNotification);
                valueEditorB.setText(secondValueText(part), juce::dontSendNotification);
            }

            valueEditor.setText(valueTextFor(part), juce::dontSendNotification);
        }

        labelEditor.setText(part.label, juce::dontSendNotification);

        if (part.hasModelChoice())
            populateModelBox(part);

        taperBox.setSelectedId(static_cast<int>(part.taper) + 1, juce::dontSendNotification);
        polarisedButton.setToggleState(part.polarised, juce::dontSendNotification);
    }

    /** Which fields this part has, and the values of the ones it has. */
    void ElementInspector::showFieldsFor()
    {
        const bool has = element != nullptr;

        const bool numeric = has && element->hasNumericValue();
        const bool models = has && element->hasModelChoice();
        const bool isPot = has && element->type == ElementType::Potentiometer;

        const bool isControl = has && (element->type == ElementType::Potentiometer
                                       || isSwitch(element->type));

        if (isControl)
            orderEditor.setText(juce::String(element->controlOrder), juce::dontSendNotification);

        orderCaption.setVisible(isControl);
        orderEditor.setVisible(isControl);

        if (isPot)
            knobEditor.setText(juce::String(juce::roundToInt(element->controlPosition * 100.0)),
                               juce::dontSendNotification);

        knobCaption.setVisible(isPot);
        knobEditor.setVisible(isPot);

        const bool isOutput = has && element->type == ElementType::Output;

        if (isOutput)
        {
            cabButton.setToggleState(element->cabEnabled, juce::dontSendNotification);

            // Named, not "Browse". And a file that has gone says so here as well
            // as in the console, because this is where you are looking when you
            // wonder why the cab made no difference.
            const juce::File file(element->cabFile);

            cabFileButton.setButtonText(element->cabFile.isEmpty() ? "Choose..."
                                        : file.existsAsFile()      ? file.getFileName()
                                                                   : file.getFileName() + "  (missing)");
        }

        const bool isScope = has && element->type == ElementType::Scope;

        scopeView.setVisible(isScope);
        scopeAutoButton.setVisible(isScope);
        scopeSecondsCaption.setVisible(isScope);
        scopeSecondsEditor.setVisible(isScope);

        // The two range boxes only when they do something. Left visible but
        // dead while auto-scaling, they invite you to type a range and then
        // ignore it, which is a worse way to learn what the checkbox does.
        const bool manual = isScope && ! element->scopeAutoScale;
        scopeMinCaption.setVisible(manual);
        scopeMinEditor.setVisible(manual);
        scopeMaxCaption.setVisible(manual);
        scopeMaxEditor.setVisible(manual);

        if (isScope)
        {
            scopeAutoButton.setToggleState(element->scopeAutoScale, juce::dontSendNotification);
            scopeMinEditor.setText(SymbolPainter::formatValue(element->scopeMin, "V"),
                                   juce::dontSendNotification);
            scopeMaxEditor.setText(SymbolPainter::formatValue(element->scopeMax, "V"),
                                   juce::dontSendNotification);
            scopeSecondsEditor.setText(SymbolPainter::formatValue(element->scopeSeconds, "s"),
                                       juce::dontSendNotification);
        }

        // Only for parts that can have a reading at all. Showing an empty
        // CURRENT box on a part whose current is not derivable would read as a
        // circuit fault rather than as a limit of the readout.
        currentView.setVisible(has && element->type == ElementType::Resistor);

        if (onInspectedElementChanged)
            onInspectedElementChanged(currentView.isVisible() ? element->id : 0);

        cabButton.setVisible(isOutput);
        cabFileCaption.setVisible(isOutput);
        cabFileButton.setVisible(isOutput);

        const bool isInput = has && element->type == ElementType::Input;

        for (juce::Component* c : std::initializer_list<juce::Component*>{
                 &fileInputButton, &inputFileCaption, &inputFileButton, &loopButton,
                 &playButton, &stopButton, &transportView})
            c->setVisible(isInput);

        if (isInput)
            syncFilePlayer();

        const bool isBox = has && element->type == ElementType::Rectangle;

        if (isBox)
        {
            // What the box will actually be, not what was typed: the size is
            // clamped, so a 1 has to come back as the minimum rather than sit
            // there looking accepted.
            const auto box = element->getRectangleBounds();
            widthEditor.setText(juce::String(box.getWidth()), juce::dontSendNotification);
            heightEditor.setText(juce::String(box.getHeight()), juce::dontSendNotification);
        }

        widthCaption.setVisible(isBox);
        widthEditor.setVisible(isBox);
        heightCaption.setVisible(isBox);
        heightEditor.setVisible(isBox);

        const bool twoValues = has && element->hasSecondValue();

        valueCaption.setVisible(numeric);
        valueEditor.setVisible(numeric);
        valueCaptionB.setVisible(twoValues);
        valueEditorB.setVisible(twoValues);
        // A group box's models are colours, and calling that "Model" would be
        // asking which sort of rectangle it is.
        modelCaption.setText(isBox ? "Colour" : "Model", juce::dontSendNotification);
        modelCaption.setVisible(models);
        modelBox.setVisible(models);

        // A part with one model and nothing to say about it gets no line.
        modelDescription.setVisible(models && modelDescription.getText().isNotEmpty());
        taperCaption.setVisible(isPot);
        taperBox.setVisible(isPot);
        labelCaption.setText(has && element->type == ElementType::Text     ? "Text"
                             : isBox                                      ? "Title"
                                                                          : "Label",
                             juce::dontSendNotification);
        labelCaption.setVisible(has);
        labelEditor.setVisible(has);
        polarisedButton.setVisible(has && element->type == ElementType::Capacitor);

        // A switch has no throw control here. Its position is a *live* one --
        // it moves the running circuit without a rebuild -- so it belongs with
        // the other live controls in the strip along the bottom, and having it
        // in two places meant two things to keep in step for no gain.
        //
        // Nor are rotate, flip and delete: the toolbar has all three, and so do
        // the keyboard and the canvas.
        hintLabel.setVisible(! has);

        // The captions as the design sets them: small capitals. Here rather
        // than at each setText, because the words are chosen in several places.
        for (auto* caption : captionLabels())
            caption->setText(caption->getText().toUpperCase(), juce::dontSendNotification);

        markUnsetFields();
    }

    void ElementInspector::rebuildForElement()
    {
        if (element == nullptr)
            showNothingSelected();
        else
            fillFieldsFrom(*element);

        showFieldsFor();
    }

    void ElementInspector::repaintReadouts()
    {
        if (scopeView.isVisible())
            scopeView.repaint();

        if (transportView.isVisible())
            syncFilePlayer();

        if (currentView.isVisible())
            currentView.repaint();
    }

    void ElementInspector::syncFilePlayer()
    {
        const bool have = filePlayer != nullptr;
        const auto status = have ? filePlayer->getStatus() : InputFilePlayer::Status{};
        const bool loaded = status.length > 0;

        // Only what the transport draws: a stopped file, or none, costs no
        // repaint at all.
        const bool transportMoved = status.position != fileStatus.position
                                 || status.length != fileStatus.length
                                 || status.loading != fileStatus.loading
                                 || status.fileMode != fileStatus.fileMode
                                 || ! juce::exactlyEqual(status.sampleRate, fileStatus.sampleRate);

        fileStatus = status;

        // All of these are no-ops when nothing changed, which is what lets this
        // run on every timer tick.
        fileInputButton.setToggleState(status.fileMode, juce::dontSendNotification);
        loopButton.setToggleState(status.looping, juce::dontSendNotification);
        playButton.setToggleState(status.playing, juce::dontSendNotification);

        fileInputButton.setEnabled(have);
        loopButton.setEnabled(have);
        inputFileButton.setEnabled(have);
        playButton.setEnabled(loaded);
        stopButton.setEnabled(loaded);

        // Named, not "Browse", for the cabinet's reason: a file you cannot
        // identify without opening a dialog is one you will load twice.
        inputFileButton.setButtonText(status.file == juce::File{} ? juce::String("Choose...")
                                                                   : status.file.getFileName());

        if (transportMoved)
            transportView.repaint();
    }

    void ElementInspector::TransportView::paint(juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();

        const auto& status = owner.fileStatus;

        // Minutes, seconds and a tenth: enough to find the bar a take goes
        // wrong in, and steady enough to read while it counts.
        const auto clock = [&status](juce::int64 samples)
        {
            const double seconds = status.sampleRate > 0.0 ? (double) samples / status.sampleRate : 0.0;
            const auto tenths = (int) std::floor(seconds * 10.0);
            return juce::String(tenths / 600) + ":" + juce::String((tenths / 10) % 60).paddedLeft('0', 2)
                 + "." + juce::String(tenths % 10);
        };

        juce::String text;

        if (status.loading)
            text = "Loading...";
        else if (status.length == 0)
            text = "No file loaded";
        else
            text = clock(status.position) + " / " + clock(status.length)
                 + (status.fileMode ? juce::String() : juce::String("   (live input)"));

        g.setColour(Theme::textOnPanel().withAlpha(0.6f));
        g.setFont(Fonts::light(12.0f));
        g.drawText(text, bounds.removeFromTop(16.0f), juce::Justification::centredLeft, false);

        auto track = bounds.withSizeKeepingCentre(bounds.getWidth(), 4.0f);
        g.setColour(Theme::pill().darker(0.12f));
        g.fillRoundedRectangle(track, 2.0f);

        if (status.length > 0)
        {
            const auto done = juce::jlimit(0.0f, 1.0f, (float) ((double) status.position / (double) status.length));
            g.setColour(Theme::accent());
            g.fillRoundedRectangle(track.withWidth(track.getWidth() * done), 2.0f);
        }
    }

    void ElementInspector::CurrentView::paint(juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();

        // A well, like the fields: the readings are a thing on the card rather
        // than lettering floating on it.
        g.setColour(Theme::pill().withMultipliedAlpha(0.7f));
        g.fillRoundedRectangle(bounds, Theme::cornerRadius);
        bounds = bounds.reduced(4.0f, 6.0f);

        float amps = 0.0f, watts = 0.0f, peak = 0.0f;
        const bool got = owner.currentReader && owner.currentReader(amps, watts, peak);

        const auto dim = Theme::textOnPanel().withAlpha(0.5f);
        g.setColour(dim);
        g.setFont(Fonts::light(12.0f));

        if (! got)
        {
            g.drawText("Press Rebuild to measure this part", bounds,
                       juce::Justification::centred, false);
            return;
        }

        // Mean current, what it dissipates, and the largest current in the
        // window: mean power says whether a part is inside its rating, peak
        // current whether a stage is being driven into grid conduction.
        auto cell = [&](juce::Rectangle<float> box, const juce::String& caption,
                        const juce::String& value, juce::Colour colour)
        {
            g.setColour(dim);
            g.setFont(Fonts::bold(10.0f).withExtraKerningFactor(0.08f));
            g.drawText(caption, box.removeFromTop(13.0f), juce::Justification::centred, false);

            g.setColour(colour);
            drawReading(g, value, box, 15.0f);
        };

        const auto third = bounds.getWidth() / 3.0f;
        const auto ink = Theme::textOnPanel();

        cell(bounds.removeFromLeft(third), "CURRENT", ElementInspector::formatReading(amps, "A"), ink);
        cell(bounds.removeFromLeft(third), "POWER", ElementInspector::formatReading(watts, "W"), ink);
        cell(bounds, "PEAK", ElementInspector::formatReading(peak, "A"), ink);
    }

    /** A round number at or below `rough`, from the 1-2-5 sequence.

        Gridlines want to land on numbers a person would have chosen -- 2 V, 50
        mV -- because the point of a labelled axis is reading a value off it by
        eye, and thirds of an arbitrary span cannot be added up in your head. */
    static double niceStep(double rough)
    {
        if (rough <= 0.0)
            return 1.0;

        const double decade = std::pow(10.0, std::floor(std::log10(rough)));
        const double norm = rough / decade;

        return (norm >= 5.0 ? 5.0 : norm >= 2.0 ? 2.0 : 1.0) * decade;
    }

    void ElementInspector::ScopeView::drawScopeAxes(juce::Graphics& g,
                                                    juce::Rectangle<float> plot,
                                                    juce::Rectangle<float> gutter,
                                                    juce::Rectangle<float> timeAxis,
                                                    const ScopeScale& scale,
                                                    const ScopeReading& reading)
    {
        if (! scale.valid || plot.getHeight() < 8.0f)
            return;

        const float span = scale.highest - scale.lowest;

        if (! (span > 0.0f))
            return;

        g.setFont(Fonts::light(10.0f));

        // Volts up the side. At most five lines: past that they crowd into each
        // other in a panel this size and the numbers stop being readable, which
        // costs more than the resolution gains.
        const double step = niceStep(span / 4.0);
        const double first = std::ceil(scale.lowest / step) * step;

        for (double v = first; v <= scale.highest; v += step)
        {
            const float y = plot.getBottom()
                          - static_cast<float>((v - scale.lowest) / span) * plot.getHeight();

            if (y < plot.getY() || y > plot.getBottom())
                continue;

            // Ground gets a brighter line, recognised by tolerance: the levels
            // accumulate by repeated addition, so the one that should be zero is
            // a few ulps off and an equality test would miss it.
            const bool isGround = std::abs(v) < step * 1.0e-6;
            g.setColour(Theme::line().withAlpha(isGround ? 0.5f : 0.16f));
            g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());

            g.setColour(Theme::comment());
            g.drawText(SymbolPainter::formatValue(v, "V"),
                       juce::Rectangle<float>(gutter.getX(), y - 6.0f, gutter.getWidth() - 4.0f, 12.0f),
                       juce::Justification::centredRight, false);
        }

        // Time along the bottom, counted back from now. The newest column is at
        // the right -- see drawScopeTrace -- so zero belongs at that end and the
        // numbers run negative leftwards, which is what "how long ago" means.
        if (reading.windowSeconds > 0.0f)
        {
            g.setColour(Theme::comment());

            const auto label = [&](float atX, const juce::String& text, juce::Justification j)
            {
                g.drawText(text, juce::Rectangle<float>(atX - 30.0f, timeAxis.getY(), 60.0f, 12.0f),
                           j, false);
            };

            label(plot.getX() + 30.0f,
                  "-" + SymbolPainter::formatValue(reading.windowSeconds, "s"),
                  juce::Justification::centredLeft);
            label(plot.getRight() - 30.0f, "now", juce::Justification::centredRight);
        }
    }

    void ElementInspector::ScopeView::paint(juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour(Theme::background());
        g.fillRoundedRectangle(bounds, Theme::cornerRadius);
        g.setColour(Theme::line().withAlpha(0.35f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), Theme::cornerRadius, 1.0f);

        ScopeReading reading;
        const auto* probe = owner.element;

        const bool got = probe != nullptr && owner.scopeReader
                      && owner.scopeReader(probe->id, reading);

        auto readout = bounds.removeFromBottom(34.0f);

        // Room for the axes down the left and along the bottom. A graph with no
        // scale on it says "something happened"; the whole reason to put a
        // scope on a sheet is to find out how much of it, and at a plate that
        // is the difference between a healthy stage and one about to clip.
        auto plot = bounds.reduced(6.0f);
        auto gutter = plot.removeFromLeft(42.0f);
        auto timeAxis = plot.removeFromBottom(14.0f);

        const auto scale = drawScopeTrace(g, plot, reading,
                                          Theme::captionValue(), Theme::comment(),
                                          Theme::background());

        if (! got || ! reading.live)
        {
            g.setColour(Theme::comment());
            g.setFont(Fonts::light(13.0f));
            g.drawText(got ? "Not in a built circuit" : "Press Rebuild to read this probe",
                       readout, juce::Justification::centred, false);
            return;
        }

        drawScopeAxes(g, plot, gutter, timeAxis, scale, reading);

        // Two numbers, side by side and captioned: which one you are reading
        // matters, and a bare pair of voltages does not say.
        const auto half = readout.getWidth() * 0.5f;

        auto cell = [&](juce::Rectangle<float> box, const juce::String& caption,
                        const juce::String& value)
        {
            g.setColour(Theme::comment());
            g.setFont(Fonts::light(11.0f));
            g.drawText(caption, box.removeFromTop(13.0f), juce::Justification::centred, false);

            g.setColour(Theme::captionValue());
            drawReading(g, value, box, 15.0f);
        };

        cell(readout.removeFromLeft(half), "DC", formatReading(reading.dcAverage, "V"));
        cell(readout, "PEAK-PEAK", formatReading(reading.peakToPeak, "V"));
    }

    /** What the value box shows for a part. One place, so the box the value is
        typed into and the box it is echoed back into cannot disagree -- which,
        for a pot, would mean the taper letter appearing only after an edit. */
    juce::String ElementInspector::valueTextFor(const Element& e)
    {
        if (e.type == ElementType::Potentiometer)
            return SymbolPainter::formatPotValue(e.value, e.taper);

        return SymbolPainter::formatValue(e.value, getElementInfo(e.type).unit);
    }

    /** The same for the second box. A rail is a voltage and says so, sign and
        all; a turns count is a bare number. */
    juce::String ElementInspector::secondValueText(const Element& e)
    {
        return SymbolPainter::formatValue(e.valueB, e.isSecondValueRail() ? getElementInfo(e.type).unit : "");
    }

    void ElementInspector::commitValue()
    {
        if (element == nullptr || ! element->hasNumericValue())
            return;

        double parsed = 0.0;
        bool changed = false;

        if (element->type == ElementType::Potentiometer)
        {
            // "A250K" sets the taper and the track in one go, which is how a pot
            // is named out loud and now the shortest way to say it here too. A
            // bare "250k" still means only the resistance -- see parsePotValue.
            auto taper = element->taper;
            bool taperGiven = false;

            if (SymbolPainter::parsePotValue(valueEditor.getText(), parsed, taper, taperGiven)
                && parsed > 0.0)
            {
                element->value = parsed;
                changed = true;

                if (taperGiven && taper != element->taper)
                {
                    element->taper = taper;
                    rebuildForElement();   // the taper box has to follow the text
                }
            }
        }
        else if (SymbolPainter::parseValue(valueEditor.getText(), parsed) && parsed > 0.0)
        {
            element->value = parsed;
            changed = true;
        }

        if (element->hasSecondValue() && SymbolPainter::parseValue(valueEditorB.getText(), parsed))
        {
            // A rail takes zero -- ground -- and whatever sign was typed; a
            // count of turns has to be more than none.
            if (element->isSecondValueRail())
            {
                element->valueB = Element::railBelowGround(parsed);
                changed = true;
            }
            else if (parsed > 0.0)
            {
                element->valueB = parsed;
                changed = true;
            }
        }

        if (changed && onEdited)
            onEdited();

        // Either way, show what the values actually are now -- so a rejected
        // entry visibly snaps back rather than sitting there looking accepted.
        valueEditor.setText(valueTextFor(*element), juce::dontSendNotification);

        if (element->hasSecondValue())
            valueEditorB.setText(secondValueText(*element), juce::dontSendNotification);

        markUnsetFields();
    }

    void ElementInspector::commitScopeAxes()
    {
        if (element == nullptr || element->type != ElementType::Scope)
            return;

        const bool wasAuto = element->scopeAutoScale;
        element->scopeAutoScale = scopeAutoButton.getToggleState();

        double parsed = 0.0;

        // Negative volts are ordinary here, unlike everywhere else a value is
        // typed: the bottom of a scope's window is usually below ground. So
        // these accept what parseValue returns rather than insisting it be
        // positive, which is the check every other field makes.
        if (SymbolPainter::parseValue(scopeMinEditor.getText(), parsed))
            element->scopeMin = parsed;

        if (SymbolPainter::parseValue(scopeMaxEditor.getText(), parsed))
            element->scopeMax = parsed;

        // An inverted or empty range would divide by zero in the renderer and
        // draw a line of infinities, so it is refused here rather than guarded
        // for at every place that draws.
        if (! (element->scopeMax > element->scopeMin))
        {
            element->scopeMin = -5.0;
            element->scopeMax = 5.0;
        }

        if (SymbolPainter::parseValue(scopeSecondsEditor.getText(), parsed))
            element->scopeSeconds = juce::jlimit(0.001, 2.0, parsed);

        // The time base is the one setting here the audio thread reads, since
        // it decides how many samples go into a column. Everything else on this
        // panel is read by the renderer alone and needs nothing.
        if (onScopeTimebaseChanged)
            onScopeTimebaseChanged();

        // Showing or hiding the range boxes changes the panel's shape, so it is
        // laid out again -- but only when the checkbox actually moved, or every
        // commit would rebuild the fields under the cursor. rebuildForElement()
        // decides what is visible and resized() where it goes, which is the
        // pairing setElement() uses.
        if (wasAuto != element->scopeAutoScale)
        {
            rebuildForElement();
            contentChanged();
            repaint();
        }
        else
            scopeMinEditor.setText(SymbolPainter::formatValue(element->scopeMin, "V"),
                                   juce::dontSendNotification);

        scopeMaxEditor.setText(SymbolPainter::formatValue(element->scopeMax, "V"),
                               juce::dontSendNotification);
        scopeSecondsEditor.setText(SymbolPainter::formatValue(element->scopeSeconds, "s"),
                                   juce::dontSendNotification);
    }

    void ElementInspector::commitOrder()
    {
        if (element == nullptr)
            return;

        // Any integer, negative included -- putting a control first is easier
        // said as -1 than by renumbering everything after it.
        element->controlOrder = orderEditor.getText().trim().getIntValue();
        orderEditor.setText(juce::String(element->controlOrder), juce::dontSendNotification);

        if (onEdited)
            onEdited();
    }

    void ElementInspector::commitKnob()
    {
        if (element == nullptr)
            return;

        // Clamped rather than refused: there is no wrong knob position, only
        // one past an end, and a pot at 110% is a typo with an obvious meaning.
        element->setControlPosition(static_cast<float>(knobEditor.getText().trim().getIntValue()) * 0.01f);
        knobEditor.setText(juce::String(juce::roundToInt(element->controlPosition * 100.0)),
                           juce::dontSendNotification);

        if (onControlEdited)
            onControlEdited();
    }

    void ElementInspector::commitSize()
    {
        if (element == nullptr || element->type != ElementType::Rectangle)
            return;

        // Plain integers in grid squares, not engineering notation: a box is
        // measured in the same units the sheet is drawn on, and "4k7" squares
        // means nothing. getRectangleBounds does the clamping, so a 0 or a
        // negative simply comes back as the minimum.
        element->width = widthEditor.getText().trim().getIntValue();
        element->height = heightEditor.getText().trim().getIntValue();

        const auto box = element->getRectangleBounds();
        element->width = box.getWidth();
        element->height = box.getHeight();

        widthEditor.setText(juce::String(element->width), juce::dontSendNotification);
        heightEditor.setText(juce::String(element->height), juce::dontSendNotification);

        if (onEdited)
            onEdited();
    }

    namespace
    {
        /** Between the card's edge and the fields, the same on all four sides. */
        constexpr int inspectorPad = 14;

        /** The heading: a symbol tile with the name and model beside it, and the
            rule under it. */
        constexpr int headerHeight = 60;
        constexpr int headerTile = 36;

        /** A field, its caption above it, and the air between one field and
            the next. */
        constexpr int fieldHeight = 32;
        constexpr int captionHeight = 13;
        constexpr int captionGap = 6;
        constexpr int fieldGap = 14;

        /** A Label's own border either side of its text, which a measurement of
            the text alone leaves out. */
        constexpr int labelBorderX = 5;
        constexpr int labelBorderY = 1;

    } // namespace

    juce::Rectangle<int> ElementInspector::getHeaderBounds() const
    {
        return getLocalBounds().removeFromTop(headerHeight);
    }

    void ElementInspector::paint(juce::Graphics& g)
    {
        // The light card, like the palette across the sheet: no outline.
        const auto card = getLocalBounds().toFloat();
        g.setColour(Theme::panel());
        g.fillRoundedRectangle(card, Theme::inspectorCornerRadius);

        const auto ink = Theme::textOnPanel();
        auto header = getHeaderBounds().reduced(inspectorPad, 0).toFloat();

        if (headerHasSymbol)
        {
            const auto tile = header.removeFromLeft((float) headerTile)
                                  .withSizeKeepingCentre((float) headerTile, (float) headerTile);
            g.setColour(Theme::pill());
            g.fillRoundedRectangle(tile, 8.0f);
            SymbolPainter::drawPreview(g, headerType, tile, ink);
            header.removeFromLeft(12.0f);
        }

        const bool twoLines = headerSubtitle.isNotEmpty();
        auto titleArea = header.withSizeKeepingCentre(header.getWidth(), twoLines ? 38.0f : 22.0f);

        g.setColour(ink);
        g.setFont(Fonts::bold(16.5f));
        g.drawText(headerTitle, titleArea.removeFromTop(21.0f), juce::Justification::centredLeft, true);

        if (twoLines)
        {
            g.setColour(ink.withAlpha(0.6f));
            g.setFont(Fonts::light(12.5f));
            g.drawFittedText(headerSubtitle, titleArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.9f);
        }

        // The rule under the heading, edge to edge.
        g.setColour(ink.withAlpha(0.08f));
        g.fillRect(card.getX(), (float) headerHeight - 1.0f, card.getWidth(), 1.0f);
    }

    int ElementInspector::hintWidth() const
    {
        const auto font = hintLabel.getFont();
        float widest = 0.0f;

        for (const auto& line : juce::StringArray::fromLines(hintLabel.getText()))
            widest = juce::jmax(widest, juce::GlyphArrangement::getStringWidth(font, line));

        return juce::roundToInt(std::ceil(widest)) + 2 * labelBorderX;
    }

    int ElementInspector::hintHeight() const
    {
        // One line per line: a Label lays its lines out at the font's height,
        // and the card is sized to the longest line, so nothing wraps.
        const auto lines = juce::StringArray::fromLines(hintLabel.getText()).size();
        return juce::roundToInt(std::ceil(static_cast<float>(lines) * hintLabel.getFont().getHeight()))
               + 2 * labelBorderY + 2;
    }

    int ElementInspector::contentWidth() const
    {
        // One width whatever is showing, so the card does not jump sideways as
        // the selection changes. What it shows is fitted to it instead.
        return Theme::inspectorWidth - 2 * inspectorPad;
    }

    void ElementInspector::fitHintToWidth()
    {
        // The width a hint line has: the field column, as layOutFields
        // leaves it, and a scrollbar's width spare -- on a short window the list scrolls, the bar takes its
        // width from the text, and a line measured to fit exactly then wraps,
        // pushing the last lines out of a label sized for one line each.
        const int room = contentWidth() - viewport.getScrollBarThickness();

        // The largest size, half a point at a time, at which the longest line
        // fits. Measured rather than scaled, since lettering is hinted to the
        // pixel and not quite proportional to its size.
        float height = hintFontHeight;
        hintLabel.setFont(Fonts::light(height));

        while (height > 8.0f && hintWidth() > room)
        {
            height -= 0.5f;
            hintLabel.setFont(Fonts::light(height));
        }
    }

    juce::Point<int> ElementInspector::getPreferredSize()
    {
        const int width = contentWidth();
        const int height = layOutFields(width);

        // Measuring lays the fields out in a scratch area, so put them back where
        // the card actually is.
        resized();

        return {width + 2 * inspectorPad, headerHeight + height + 2 * inspectorPad};
    }

    int ElementInspector::layOutFields(int width)
    {
        // Laid out into a deliberately tall area, because Rectangle::removeFromTop
        // clamps once it runs out -- so measuring inside the visible height can
        // never report more than that height, which is precisely the number we
        // are trying to find. Without this the panel silently clips instead of
        // growing a scrollbar.
        constexpr int roomToMeasureIn = 4000;
        content.setSize(width, roomToMeasureIn);

        auto area = content.getLocalBounds();
        bool first = true;

        // The gap goes *before* each row, so the last one leaves none to trim.
        const auto next = [&](int height)
        {
            if (! first)
                area.removeFromTop(fieldGap);

            first = false;
            return area.removeFromTop(height);
        };

        const auto field = [&](juce::Component& caption, juce::Component& control)
        {
            if (! control.isVisible())
                return;

            auto cell = next(captionHeight + captionGap + fieldHeight);
            caption.setBounds(cell.removeFromTop(captionHeight));
            cell.removeFromTop(captionGap);
            control.setBounds(cell);
        };

        // Two fields side by side, for numbers that come as a pair.
        const auto pair = [&](juce::Component& captionA, juce::Component& controlA,
                              juce::Component& captionB, juce::Component& controlB)
        {
            if (! controlA.isVisible() && ! controlB.isVisible())
                return;

            auto cell = next(captionHeight + captionGap + fieldHeight);
            auto left = cell.removeFromLeft((cell.getWidth() - 12) / 2);
            cell.removeFromLeft(12);

            for (auto [caption, control, box] : {std::tuple{&captionA, &controlA, left},
                                                  std::tuple{&captionB, &controlB, cell}})
            {
                if (! control->isVisible())
                    continue;

                caption->setBounds(box.removeFromTop(captionHeight));
                box.removeFromTop(captionGap);
                control->setBounds(box);
            }
        };

        const auto row = [&](juce::Component& c, int height)
        {
            if (c.isVisible())
                c.setBounds(next(height));
        };

        // What it is called, then which part it is, then what it is set to --
        // the order the iPad designer asks in.
        field(labelCaption, labelEditor);
        field(modelCaption, modelBox);

        if (modelDescription.isVisible())
        {
            // As many lines as the note wraps to at this width.
            const auto font = modelDescription.getFont();
            juce::AttributedString text;
            text.append(modelDescription.getText(), font);
            juce::TextLayout layout;
            layout.createLayout(text, static_cast<float>(area.getWidth()));

            const int lines = juce::jmax(1, layout.getNumLines());
            area.removeFromTop(6);
            modelDescription.setBounds(area.removeFromTop(
                juce::roundToInt(std::ceil(static_cast<float>(lines) * font.getHeight())) + 2));
        }

        pair(widthCaption, widthEditor, heightCaption, heightEditor);
        field(valueCaption, valueEditor);
        field(valueCaptionB, valueEditorB);
        field(taperCaption, taperBox);
        field(knobCaption, knobEditor);
        row(polarisedButton, 24);
        field(orderCaption, orderEditor);

        // Directly under its switch, because the two are one control: the file
        // is what the switch switches in.
        row(cabButton, 24);
        field(cabFileCaption, cabFileButton);

        // The file player, in the order you use it: live or file, which file,
        // how it repeats, then the transport and where it has got to.
        row(fileInputButton, 24);
        field(inputFileCaption, inputFileButton);

        if (loopButton.isVisible())
        {
            row(loopButton, 24);

            auto buttons = next(fieldHeight);
            playButton.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2 - 4));
            stopButton.setBounds(buttons.removeFromRight(buttons.getWidth() - 8));

            row(transportView, 30);
        }

        // The scope's picture, given real room -- it is the whole of what the
        // part is for, and a trace two centimetres wide tells you nothing.
        row(scopeView, 150);
        row(scopeAutoButton, 24);

        // Top beside bottom, top first, the way they are read off the graph.
        pair(scopeMaxCaption, scopeMaxEditor, scopeMinCaption, scopeMinEditor);
        field(scopeSecondsCaption, scopeSecondsEditor);

        row(currentView, 52);

        // The hint only shows when nothing is selected, so it never competes
        // with the fields for room -- it is as tall as its lines.
        if (hintLabel.isVisible())
            hintLabel.setBounds(next(hintHeight()));

        return area.getY();
    }

    void ElementInspector::resized()
    {
        viewport.setBounds(getLocalBounds().withTrimmedTop(headerHeight).reduced(inspectorPad));

        // Same two-pass as the console: a scrollbar takes width, and whether one
        // is needed depends on the height. Field heights here don't depend on
        // width, so the first pass only has to decide, and the second lays out
        // at the width that will actually be there.
        const int full = juce::jmax(80, viewport.getWidth());
        int width = full;
        int used = layOutFields(width);

        if (used > viewport.getHeight())
        {
            width = juce::jmax(80, full - viewport.getScrollBarThickness());
            used = layOutFields(width);
        }

        content.setSize(width, juce::jmax(used, viewport.getHeight()));
    }

    //==========================================================================
    // Message console
    //==========================================================================

    namespace
    {

        constexpr int rowPadding = 5;

        /** The left gutter the prompt glyph sits in, and how big the glyph is
            drawn in it. The rows start after it, so the icon is beside the first
            message rather than above it -- which is what lets the panel be named
            without a heading spending a row on saying so. */
        constexpr int gutterWidth = 60;
        constexpr int promptLeft = 16;
        constexpr int promptSize = 26;
        constexpr int promptTop = 7;

        /** The rows' list inside the pill. With one message the pill comes to
            the design's 43 px, the glyph centred on the line beside it. */
        constexpr int listTop = 7;
        constexpr int listBottom = 4;
        constexpr int listRight = 11;

        /** Narrower than this and a message is a column of single words. */
        constexpr int minimumTextWidth = 120;

        static_assert(MessageConsole::minimumWidth == gutterWidth + 2 * rowPadding + listRight + minimumTextWidth,
                      "the console's published minimum must be what it actually lays out at");

        /** While the pointer rests on the console, how often it looks again. */
        constexpr int hoverRecheckMs = 1000;

        /** How long the console takes to fade once it does go. */
        constexpr int fadeOutMs = 400;

        juce::Font consoleFont() { return Fonts::light(13.5f); }
    } // namespace

    MessageConsole::MessageConsole()
    {
        viewport.setViewedComponent(&rowList, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setWantsKeyboardFocus(false);
        addAndMakeVisible(viewport);

        // A rounded, translucent pill: the sheet shows through it and at its
        // corners.
        setOpaque(false);

        // Nothing to say until something happens.
        setVisible(false);

        applyColours();
    }

    bool MessageConsole::hasProblems() const
    {
        if (statusIsError)
            return true;

        return std::any_of(messages.begin(), messages.end(), [](const SchematicModel::Diagnostic& m)
                           { return m.severity == SchematicModel::Diagnostic::Severity::Error
                                    || m.severity == SchematicModel::Diagnostic::Severity::Warning; });
    }

    void MessageConsole::announce()
    {
        setAlpha(1.0f);
        setVisible(true);

        // A problem holds the console up until something without one replaces
        // it; anything else goes on its own.
        if (hasProblems())
            stopTimer();
        else
            startTimer(hideDelayMs);

        if (onPreferredSizeChanged)
            onPreferredSizeChanged();
    }

    void MessageConsole::timerCallback()
    {
        // Not out from under the pointer: someone reading it, or about to click
        // a row, gets to finish.
        if (isMouseOverOrDragging(true))
        {
            startTimer(hoverRecheckMs);
            return;
        }

        stopTimer();

        // Fades a stand-in and hides this at once, so the console is gone as
        // far as anything asking is concerned.
        juce::Desktop::getInstance().getAnimator().fadeOut(this, fadeOutMs);
    }

    juce::Point<int> MessageConsole::getPreferredSize(int maxWidth)
    {
        // Every row laid out on one line, to find the widest.
        measureRows(1 << 16);

        float widest = 0.0f;

        for (const auto& row : rows)
            widest = juce::jmax(widest, juce::GlyphArrangement::getStringWidth(consoleFont(), row.text));

        const int chrome = gutterWidth + 2 * rowPadding + listRight;

        // A couple of pixels' slack, or a line measured to exactly its width
        // can wrap its last word when it is laid out again.
        const int natural = juce::roundToInt(std::ceil(widest)) + 2;
        const int textWidth = juce::jmax(minimumTextWidth, juce::jmin(natural, maxWidth - chrome));
        const int listHeight = measureRows(textWidth);

        // Measuring replaced the rows; lay them out again for the size the
        // console actually is.
        layOutRows();

        return {textWidth + chrome, listTop + listHeight + listBottom};
    }

    void MessageConsole::applyColours()
    {
        promptIcon = Assets::drawable("terminal-solid-full.svg");

        if (promptIcon != nullptr)
            Assets::tint(*promptIcon, Theme::text());
    }

    void MessageConsole::setMessages(juce::String newStatus, bool isError,
                                     const std::vector<SchematicModel::Diagnostic>& newMessages)
    {
        status = std::move(newStatus);
        statusIsError = isError;
        messages = newMessages;

        layOutRows();
        repaint();
        announce();
    }

    void MessageConsole::setHeadline(juce::String text, bool isError)
    {
        status = std::move(text);
        statusIsError = isError;
        layOutRows();
        repaint();
        announce();
    }

    int MessageConsole::measureRows(int width)
    {
        rows.clear();

        const juce::Font font(consoleFont());
        int y = rowPadding;

        // Measures a row and advances down the list. Everything goes through
        // here, including the status line and the empty-state text, so nothing
        // can end up in a box shorter than the words it holds -- which is what
        // clipped "Nothing to report." to half its height.
        auto addRow = [&](juce::String text, juce::Colour colour, int elementId)
        {
            juce::AttributedString wrapped;
            wrapped.append(text, font);
            juce::TextLayout layout;
            layout.createLayout(wrapped, static_cast<float>(width));

            Row row;
            row.text = std::move(text);
            row.colour = colour;
            row.elementId = elementId;
            row.y = y;
            row.height = juce::jmax(16, juce::roundToInt(layout.getHeight())) + rowPadding;
            y += row.height;

            rows.push_back(std::move(row));
        };

        // The status line is the first row, beside the prompt glyph -- so the
        // most recent thing the build had to say is the thing next to the icon.
        if (status.isNotEmpty())
            addRow(status, statusIsError ? Theme::error() : Theme::text().withAlpha(0.8f), -1);

        if (messages.empty() && status.isEmpty())
            addRow("Nothing to report.", Theme::text().withAlpha(0.35f), -1);

        for (const auto& message : messages)
        {
            const auto colour =
                message.severity == SchematicModel::Diagnostic::Severity::Error     ? Theme::error()
                : message.severity == SchematicModel::Diagnostic::Severity::Warning ? Theme::warning()
                                                                                    : Theme::notice();

            // "R4 (35,-67)  Pin 2 touches nothing else." -- the part first,
            // because that is what you are looking for.
            juce::String line;

            if (message.subject.isNotEmpty())
                line << message.subject;

            if (message.hasPosition)
                line << " (" << message.position.x << "," << message.position.y << ")";

            if (line.isNotEmpty())
                line << "  ";

            addRow(line + message.text, colour, message.elementId);
        }

        return y + rowPadding;
    }

    void MessageConsole::layOutRows()
    {
        // Two passes, because whether a scrollbar is needed depends on the
        // wrapped height, which depends on the width, which depends on whether
        // there is a scrollbar. Measure assuming there isn't; if that overflows,
        // measure again in what is actually left. Without the second pass the
        // scrollbar appears and quietly eats the last word of every line.
        const int full = juce::jmax(60, viewport.getWidth() - 2 * rowPadding);
        int width = full;
        int total = measureRows(width);

        if (total > viewport.getHeight())
        {
            width = juce::jmax(60, full - viewport.getScrollBarThickness());
            total = measureRows(width);
        }

        rowList.setSize(width + 2 * rowPadding, juce::jmax(total, viewport.getHeight()));
        rowList.repaint();
    }

    void MessageConsole::resized()
    {
        viewport.setBounds(getLocalBounds()
                               .withTrimmedLeft(gutterWidth)
                               .withTrimmedTop(listTop)
                               .withTrimmedBottom(listBottom)
                               .withTrimmedRight(listRight));
        layOutRows();
    }

    void MessageConsole::paint(juce::Graphics& g)
    {
        // The window's darkest ground, let a little of the sheet through -- the
        // design's 80%.
        g.setColour(Theme::consoleBackground().withMultipliedAlpha(0.8f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), Theme::consoleCornerRadius);

        if (promptIcon != nullptr)
            promptIcon->drawWithin(g,
                                   juce::Rectangle<float>(static_cast<float>(promptLeft),
                                                          static_cast<float>(promptTop),
                                                          static_cast<float>(promptSize),
                                                          static_cast<float>(promptSize)),
                                   juce::RectanglePlacement::centred, 1.0f);
    }

    void MessageConsole::mouseDown(const juce::MouseEvent&) {}

    void MessageConsole::Rows::paint(juce::Graphics& g)
    {
        g.setFont(consoleFont());

        for (const auto& row : owner.rows)
        {
            g.setColour(row.colour);
            g.drawFittedText(row.text,
                             juce::Rectangle<int>(rowPadding, row.y, getWidth() - 2 * rowPadding,
                                                  row.height - rowPadding),
                             juce::Justification::topLeft, 6);
        }

    }

    void MessageConsole::Rows::mouseDown(const juce::MouseEvent& e)
    {
        for (const auto& row : owner.rows)
        {
            if (e.y >= row.y && e.y < row.y + row.height && row.elementId >= 0)
            {
                if (owner.onMessageClicked)
                    owner.onMessageClicked(row.elementId);

                return;
            }
        }
    }
} // namespace Celine
