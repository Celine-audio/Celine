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

        /** Every type, in palette order after the wire. Each line here is one row of two. */
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

        /** The panel's padding on all four sides, and the gap between its two columns. The
            row gap is Theme::paletteRowHeight. */
        constexpr int palettePad = 10;
        constexpr int paletteColumnGap = 4;
        constexpr int paletteEntryHeight = Theme::paletteRowHeight - 4;

        /** The fold tab on the panel's right edge: width, height and corner radius. */
        constexpr int paletteTabWidth = 14;
        constexpr int paletteTabHeight = 44;
        constexpr float paletteTabCornerRadius = 6.0f;

        /** Space after the lettering, so the longest name doesn't touch the pill's end. */
        constexpr int paletteTextTrailing = 10;

        /** Item id of the missing-model entry in the model dropdown; real ids are
            `modelIndex + 1`. */
        constexpr int missingModelItemId = 10000;

        /** The largest size for the inspector's shortcut list; it shrinks until the longest
            line fits. */
        constexpr float hintFontHeight = 13.0f;

        /** Draws a reading at the largest size, up to `largest`, at which it fits the box. */
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

        // The largest prefix the value reaches once rounded to two decimals (0.999 V reads
        // "1.00V"), and pico for anything smaller.
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

        // Rounds to nothing: plain zero, with no prefix or sign.
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
        /** The wire row: no element type, draws its own symbol. */
        Entry() : juce::Button("Wire"), isWire(true)
        {
            setTooltip("Draw wires (W)");
            setWantsKeyboardFocus(false);
        }

        explicit Entry(ElementType t) : juce::Button(getElementInfo(t).name), type(t)
        {
            setTooltip(getElementInfo(t).name);

            // The canvas keeps keyboard focus, so R and F still reach it.
            setWantsKeyboardFocus(false);
        }

        juce::String getLabel() const { return isWire ? "Wire" : getElementInfo(type).name; }

        /** The name's width at a given size. */
        float getNameWidth(float size) const
        {
            return juce::GlyphArrangement::getStringWidth(Fonts::light(size), getLabel());
        }

        /** The size every name is drawn at; see ElementPalette::getNameFontHeight. */
        float fontHeight = Theme::paletteFontHeight;

        void paintButton(juce::Graphics& g, bool highlighted, bool down) override
        {
            auto bounds = getLocalBounds().toFloat();

            // A filled pill per entry.
            g.setColour(active            ? Theme::accent()
                        : down            ? Theme::pill().darker(0.12f)
                        : highlighted     ? Theme::pill().darker(0.05f)
                                          : Theme::pill());
            g.fillRoundedRectangle(bounds, Theme::paletteEntryCornerRadius);

            // Dark ink: the palette is a light surface.
            const auto ink = active ? Theme::text() : Theme::textOnPanel();

            // Symbol in a square on the left, name beside it.
            auto symbolArea = bounds.removeFromLeft(bounds.getHeight());

            if (isWire)
            {
                // An elbow, as the wire tool draws.
                const auto area = symbolArea.reduced(7.0f);
                juce::Path elbow;
                elbow.startNewSubPath(area.getX(), area.getBottom());
                elbow.lineTo(area.getCentreX(), area.getBottom());
                elbow.lineTo(area.getCentreX(), area.getY());
                elbow.lineTo(area.getRight(), area.getY());

                g.setColour(ink);
                g.strokePath(elbow, juce::PathStrokeType(SymbolPainter::previewLineWidth(symbolArea),
                                                         juce::PathStrokeType::mitered,
                                                         juce::PathStrokeType::rounded));
            }
            else
            {
                SymbolPainter::drawPreview(g, type, symbolArea, ink);
            }

            // Folded: the pill is just the symbol; the tooltip gives the name.
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

    /** The tab on the panel's edge that folds it to symbols and back. */
    class ElementPalette::CollapseTab : public juce::Button
    {
       public:
        CollapseTab() : juce::Button("Fold palette")
        {
            // The canvas keeps the keyboard.
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

            // Square where it meets the panel, rounded on the free side.
            juce::Path shape;
            shape.addRoundedRectangle(bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
                                      paletteTabCornerRadius, paletteTabCornerRadius,
                                      false, true, false, true);

            g.setColour(down          ? Theme::pill()
                        : highlighted ? Theme::pill().brighter(0.4f)
                                      : Theme::panel());
            g.fillPath(shape);

            // A chevron pointing the way the panel will move.
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
        // Wire first.
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

        // One size for every name: the largest at which the longest fits its column.
        nameFontHeight = fittingFontHeight();

        for (auto* entry : entries)
            entry->fontHeight = nameFontHeight;

        // Vertical scrolling only.
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

        // Not opaque: rounded corners and the tab let the sheet show.
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
        // A name's room: the column less the symbol square and trailing space.
        constexpr float room = static_cast<float>(Theme::paletteColumnWidth - paletteEntryHeight - paletteTextTrailing);

        auto widest = [this](float height)
        {
            float width = 0.0f;

            for (const auto* entry : entries)
                width = juce::jmax(width, entry->getNameWidth(height));

            return width;
        };

        // Text width isn't exactly proportional to size (hinting), so step down by half a
        // point until the longest name fits.
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
        // Always two to a row: symbol squares when folded, fixed columns otherwise.
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

        // The scrollbar sits beside the rows.
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

        // Vertically centred on the edge, or the whole height if the panel is shorter.
        const int tabHeight = juce::jmin(paletteTabHeight, body.getHeight());
        tab->setBounds(body.getRight(), body.getCentreY() - tabHeight / 2, paletteTabWidth, tabHeight);
    }

    //==========================================================================
    // Inspector
    //==========================================================================

    std::array<juce::Label*, 15> ElementInspector::captionLabels()
    {
        return {&valueCaption, &valueCaptionB, &labelCaption, &modelCaption, &taperCaption,
                &orderCaption, &knobCaption, &widthCaption, &heightCaption,
                &scopeMinCaption, &scopeMaxCaption, &scopeSecondsCaption, &cabFileCaption,
                &inputFileCaption, &actionCaption};
    }

    void ElementInspector::reshapeSwitch(SwitchAction action)
    {
        if (element == nullptr || element->type != ElementType::Spdt)
            return;

        // Keep the lever on the same throw in the new action (or the middle, if it was off
        // and the new action has an off).
        const int wasOn = element->getThrowAt(element->switchPosition);

        element->switchAction = action;

        const int top = element->getSwitchPositions() - 1;
        int position = top;

        for (int k = 0; k <= top; ++k)
            if (element->getThrowAt(k) == wasOn)
                position = k;

        element->switchPosition = position;
        element->closed = position == top;

        headerSubtitle = headerSubtitleFor(*element);
        repaint();

        if (onEdited)
            onEdited();
    }

    void ElementInspector::applyColours()
    {
        // The card is a light surface: dark ink, fields one step off the card.
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

        // Value fields still at zero go red, box and lettering.
        markUnsetFields();

        for (auto* box : {&modelBox, &taperBox, &actionBox})
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

        // Play lights while the file runs.
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

        // Small-capital captions, flush with the field's left edge.
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
                 &modelBox, &taperBox, &actionBox, &polarisedButton, &cabButton, &scopeAutoButton,
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
        actionCaption.setText("Action", juce::dontSendNotification);

        for (const auto& name : getSwitchActionNames())
            actionBox.addItem(name, actionBox.getNumItems() + 1);

        actionBox.onChange = [this]
        {
            if (element != nullptr && actionBox.getSelectedId() > 0)
                reshapeSwitch(static_cast<SwitchAction>(actionBox.getSelectedId() - 1));
        };
        content.addAndMakeVisible(actionBox);

        // Commit on Enter and on focus loss.
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
            // The missing-model entry is disabled, so its id never arrives here.
            if (element != nullptr && modelBox.getSelectedId() > 0
                  && modelBox.getSelectedId() != missingModelItemId)
            {
                element->modelIndex = modelBox.getSelectedId() - 1;

                // Choosing any model clears the unresolved one.
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

        // The cabinet has its own callback, so it doesn't light Rebuild (see
        // ElementInspector::onCabChanged).
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

        // The file player. Buttons act on the player, then re-read it, so the panel shows
        // the player's state.
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

        // Before announcing the size, so the layout knows whether the card is shown.
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
        // A switch is named as sold: "DPDT, ON-OFF-ON".
        if (part.type == ElementType::Spdt)
            return part.getSwitchKindName() + ", "
                   + getSwitchActionNames()[static_cast<int>(part.switchAction)];

        // No description for a box (its models are colours). A missing model is described
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

    /** Fills the model dropdown, grouped as the table groups it and sorted naturally
        (1N914 before 1N4148) within each group. Item ids stay `index + 1`. Group order is
        the table's. */
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

        // A model this build couldn't find gets a greyed, disabled entry at the top.
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
        actionBox.setSelectedId(static_cast<int>(part.switchAction) + 1, juce::dontSendNotification);
        polarisedButton.setToggleState(part.polarised, juce::dontSendNotification);
    }

    /** Which fields this part has, and their values. */
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

            // Shows the file's name, and says so if the file is missing.
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

        // The range boxes only show when auto-scale is off.
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

        // Only parts that can have a reading; current only for resistors.
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
            // Show the clamped size, not what was typed.
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
        // A group box's models are colours.
        modelCaption.setText(isBox ? "Colour" : "Model", juce::dontSendNotification);
        modelCaption.setVisible(models);
        modelBox.setVisible(models);

        // No line when there is nothing to say.
        modelDescription.setVisible(models && modelDescription.getText().isNotEmpty());
        taperCaption.setVisible(isPot);

        const bool isChangeover = has && element->type == ElementType::Spdt;
        actionCaption.setVisible(isChangeover);
        actionBox.setVisible(isChangeover);
        taperBox.setVisible(isPot);
        labelCaption.setText(has && element->type == ElementType::Text     ? "Text"
                             : isBox                                      ? "Title"
                                                                          : "Label",
                             juce::dontSendNotification);
        labelCaption.setVisible(has);
        labelEditor.setVisible(has);
        polarisedButton.setVisible(has && element->type == ElementType::Capacitor);

        // Switch position, rotate, flip and delete are not here: the switch is in the
        // control strip, the rest in the toolbar.
        hintLabel.setVisible(! has);

        // Captions in small capitals, applied here once.
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

        // Repaint only when the transport display changes.
        const bool transportMoved = status.position != fileStatus.position
                                 || status.length != fileStatus.length
                                 || status.loading != fileStatus.loading
                                 || status.fileMode != fileStatus.fileMode
                                 || ! juce::exactlyEqual(status.sampleRate, fileStatus.sampleRate);

        fileStatus = status;

        // These are no-ops when nothing changed, so they can run every tick.
        fileInputButton.setToggleState(status.fileMode, juce::dontSendNotification);
        loopButton.setToggleState(status.looping, juce::dontSendNotification);
        playButton.setToggleState(status.playing, juce::dontSendNotification);

        fileInputButton.setEnabled(have);
        loopButton.setEnabled(have);
        inputFileButton.setEnabled(have);
        playButton.setEnabled(loaded);
        stopButton.setEnabled(loaded);

        // Shows the file's name.
        inputFileButton.setButtonText(status.file == juce::File{} ? juce::String("Choose...")
                                                                   : status.file.getFileName());

        if (transportMoved)
            transportView.repaint();
    }

    void ElementInspector::TransportView::paint(juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();

        const auto& status = owner.fileStatus;

        // Minutes, seconds and tenths.
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

        // A well behind the readings, like the fields.
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

        // Mean current, power, and peak current over the window.
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

    /** A round number at or below `rough`, from the 1-2-5 sequence. */
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

        // Volts up the side, at most five lines.
        const double step = niceStep(span / 4.0);
        const double first = std::ceil(scale.lowest / step) * step;

        for (double v = first; v <= scale.highest; v += step)
        {
            const float y = plot.getBottom()
                          - static_cast<float>((v - scale.lowest) / span) * plot.getHeight();

            if (y < plot.getY() || y > plot.getBottom())
                continue;

            // Ground gets a brighter line. Compared with a tolerance: the levels are summed and
            // zero may be a few ulps off.
            const bool isGround = std::abs(v) < step * 1.0e-6;
            g.setColour(Theme::line().withAlpha(isGround ? 0.5f : 0.16f));
            g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());

            g.setColour(Theme::comment());
            g.drawText(SymbolPainter::formatValue(v, "V"),
                       juce::Rectangle<float>(gutter.getX(), y - 6.0f, gutter.getWidth() - 4.0f, 12.0f),
                       juce::Justification::centredRight, false);
        }

        // Time along the bottom, newest at the right (zero), negative leftwards.
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

        // Room for the axes down the left and along the bottom.
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

        // Two captioned numbers side by side.
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

    /** What the value box shows for a part; used for both typing and echoing back. */
    juce::String ElementInspector::valueTextFor(const Element& e)
    {
        if (e.type == ElementType::Potentiometer)
            return SymbolPainter::formatPotValue(e.value, e.taper);

        return SymbolPainter::formatValue(e.value, getElementInfo(e.type).unit);
    }

    /** The same for the second box: a rail shows a signed voltage, a turns count a bare
        number. */
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
            // "A250K" sets taper and resistance together; a bare "250k" only the resistance (see
            // parsePotValue).
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
            // A rail accepts zero and either sign; a turns count must be positive.
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

        // Show the actual values, so a rejected entry snaps back.
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

        // Negative volts are allowed here.
        if (SymbolPainter::parseValue(scopeMinEditor.getText(), parsed))
            element->scopeMin = parsed;

        if (SymbolPainter::parseValue(scopeMaxEditor.getText(), parsed))
            element->scopeMax = parsed;

        // Refuse an inverted or empty range (the renderer would divide by zero).
        if (! (element->scopeMax > element->scopeMin))
        {
            element->scopeMin = -5.0;
            element->scopeMax = 5.0;
        }

        if (SymbolPainter::parseValue(scopeSecondsEditor.getText(), parsed))
            element->scopeSeconds = juce::jlimit(0.001, 2.0, parsed);

        // The time base is the only setting here the audio thread reads.
        if (onScopeTimebaseChanged)
            onScopeTimebaseChanged();

        // Toggling auto-scale shows or hides the range boxes, so re-lay out, only when it
        // changed.
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

        // Any integer, negative included.
        element->controlOrder = orderEditor.getText().trim().getIntValue();
        orderEditor.setText(juce::String(element->controlOrder), juce::dontSendNotification);

        if (onEdited)
            onEdited();
    }

    void ElementInspector::commitKnob()
    {
        if (element == nullptr)
            return;

        // Clamped rather than refused.
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

        // Plain integers in grid squares; getRectangleBounds clamps to the minimum.
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
        /** Padding between the card's edge and the fields, all four sides. */
        constexpr int inspectorPad = 14;

        /** The heading: symbol tile, name and model, and the rule under it. */
        constexpr int headerHeight = 60;
        constexpr int headerTile = 36;

        /** A field, its caption above it, and the gap to the next. */
        constexpr int fieldHeight = 32;
        constexpr int captionHeight = 13;
        constexpr int captionGap = 6;
        constexpr int fieldGap = 14;

        /** A Label's own border either side of its text. */
        constexpr int labelBorderX = 5;
        constexpr int labelBorderY = 1;

    } // namespace

    juce::Rectangle<int> ElementInspector::getHeaderBounds() const
    {
        return getLocalBounds().removeFromTop(headerHeight);
    }

    void ElementInspector::paint(juce::Graphics& g)
    {
        // The light card, no outline.
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
        // One row per line; the card is sized so nothing wraps.
        const auto lines = juce::StringArray::fromLines(hintLabel.getText()).size();
        return juce::roundToInt(std::ceil(static_cast<float>(lines) * hintLabel.getFont().getHeight()))
               + 2 * labelBorderY + 2;
    }

    int ElementInspector::contentWidth() const
    {
        // A fixed width, so the card doesn't move with the selection.
        return Theme::inspectorWidth - 2 * inspectorPad;
    }

    void ElementInspector::fitHintToWidth()
    {
        // A hint line's room: the field column less a scrollbar's width, so the text doesn't
        // wrap when the list scrolls.
        const int room = contentWidth() - viewport.getScrollBarThickness();

        // The largest size, in half-point steps, at which the longest line fits (measured,
        // since hinting isn't proportional).
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

        // Measuring laid the fields out in a scratch area; put them back.
        resized();

        return {width + 2 * inspectorPad, headerHeight + height + 2 * inspectorPad};
    }

    int ElementInspector::layOutFields(int width)
    {
        // Laid out in a tall area, because removeFromTop clamps and could never report more
        // than the visible height.
        constexpr int roomToMeasureIn = 4000;
        content.setSize(width, roomToMeasureIn);

        auto area = content.getLocalBounds();
        bool first = true;

        // The gap goes before each row, so the last leaves none.
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

        // Two fields side by side.
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

        // Label, then model, then value.
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
        field(actionCaption, actionBox);
        field(knobCaption, knobEditor);
        row(polarisedButton, 24);
        field(orderCaption, orderEditor);

        // Directly under its switch: the file is what the switch switches in.
        row(cabButton, 24);
        field(cabFileCaption, cabFileButton);

        // The file player, in order of use: live or file, which file, repeat, transport,
        // position.
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

        // The scope's picture, given real room.
        row(scopeView, 150);
        row(scopeAutoButton, 24);

        // Top beside bottom.
        pair(scopeMaxCaption, scopeMaxEditor, scopeMinCaption, scopeMinEditor);
        field(scopeSecondsCaption, scopeSecondsEditor);

        row(currentView, 52);

        // The hint only shows when nothing is selected; as tall as its lines.
        if (hintLabel.isVisible())
            hintLabel.setBounds(next(hintHeight()));

        return area.getY();
    }

    void ElementInspector::resized()
    {
        viewport.setBounds(getLocalBounds().withTrimmedTop(headerHeight).reduced(inspectorPad));

        // Two passes: whether a scrollbar is needed depends on height. Field heights don't
        // depend on width, so the second pass lays out at the final width.
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

        /** The left gutter holding the prompt glyph, and the glyph's size. Rows start after
            it. */
        constexpr int gutterWidth = 60;
        constexpr int promptLeft = 16;
        constexpr int promptSize = 26;
        constexpr int promptTop = 7;

        /** The rows' list inside the pill; one message gives a 43 px pill. */
        constexpr int listTop = 7;
        constexpr int listBottom = 4;
        constexpr int listRight = 11;

        /** Narrowest a message column may be. */
        constexpr int minimumTextWidth = 120;

        static_assert(MessageConsole::minimumWidth == gutterWidth + 2 * rowPadding + listRight + minimumTextWidth,
                      "the console's published minimum must be what it actually lays out at");

        /** How often to re-check while the pointer rests on the console. */
        constexpr int hoverRecheckMs = 1000;

        /** Fade-out duration. */
        constexpr int fadeOutMs = 400;

        juce::Font consoleFont() { return Fonts::light(13.5f); }
    } // namespace

    MessageConsole::MessageConsole()
    {
        viewport.setViewedComponent(&rowList, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setWantsKeyboardFocus(false);
        addAndMakeVisible(viewport);

        // A rounded, translucent pill.
        setOpaque(false);

        // Hidden until there is something to say.
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

        // Errors and warnings keep the console up until replaced; anything else fades.
        if (hasProblems())
            stopTimer();
        else
            startTimer(hideDelayMs);

        if (onPreferredSizeChanged)
            onPreferredSizeChanged();
    }

    void MessageConsole::timerCallback()
    {
        // Stay up while the pointer is over it.
        if (isMouseOverOrDragging(true))
        {
            startTimer(hoverRecheckMs);
            return;
        }

        stopTimer();

        // Fades a stand-in and hides this immediately.
        juce::Desktop::getInstance().getAnimator().fadeOut(this, fadeOutMs);
    }

    juce::Point<int> MessageConsole::getPreferredSize(int maxWidth)
    {
        // Every row on one line, to find the widest.
        measureRows(1 << 16);

        float widest = 0.0f;

        for (const auto& row : rows)
            widest = juce::jmax(widest, juce::GlyphArrangement::getStringWidth(consoleFont(), row.text));

        const int chrome = gutterWidth + 2 * rowPadding + listRight;

        // A couple of pixels' slack so the last word doesn't wrap when laid out again.
        const int natural = juce::roundToInt(std::ceil(widest)) + 2;
        const int textWidth = juce::jmax(minimumTextWidth, juce::jmin(natural, maxWidth - chrome));
        const int listHeight = measureRows(textWidth);

        // Measuring replaced the rows; lay them out for the actual size.
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

        // Measures a row and advances down the list. Everything goes through here, so no box
        // is shorter than its text.
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

        // The status line is the first row, beside the prompt glyph.
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

            // "R4 (35,-67)  Pin 2 touches nothing else.": the part first.
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
        // Two passes: measure without a scrollbar; if that overflows, measure again in the
        // width that is left.
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
        // The darkest ground at 80% opacity.
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
