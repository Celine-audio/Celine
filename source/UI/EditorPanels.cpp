#include "EditorPanels.h"

#include <numeric>

#include "CelineLookAndFeel.h"
#include "EmbeddedAssets.h"
#include "Fonts.h"
#include "Theme.h"

namespace SchematicUI
{
    using namespace SchematicModel;

    namespace
    {

        /** Every type, in the order the palette lists them: terminals first,
            because a drawing needs them and a blank sheet gives no clue that
            ground is a part you place. */
        constexpr ElementType paletteOrder[] = {
            ElementType::Ground,        ElementType::Input,       ElementType::Output,     ElementType::Node,
            ElementType::Resistor,      ElementType::Capacitor,   ElementType::Inductor,
            ElementType::Potentiometer, ElementType::Switch,      ElementType::Spdt,      ElementType::VoltageSource,
            ElementType::Transformer,   ElementType::CenterTapTransformer,
            ElementType::Diode,         ElementType::Transistor,  ElementType::Jfet,
            ElementType::OpAmp,
            ElementType::Triode,        ElementType::Pentode,     ElementType::VacuumDiode,
            ElementType::Text,          ElementType::Rectangle,
            ElementType::Scope,
        };

        constexpr int paletteCount = static_cast<int>(std::size(paletteOrder));
        static_assert(paletteCount == numElementTypes, "every element type should be reachable from the palette");

        /** Item id for the "model this build hasn't got" entry in the model
            dropdown. Well clear of the real ones, which are `modelIndex + 1`
            and so run from 1 upwards. */
        constexpr int missingModelItemId = 10000;
    } // namespace

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

        void paintButton(juce::Graphics& g, bool highlighted, bool down) override
        {
            auto bounds = getLocalBounds().toFloat().reduced(2.0f);

            // A pill per row, on the light panel. The mockup gives every row a
            // filled pill rather than the hover-only tint this had, so the list
            // reads as a stack of buttons at rest and not only under the mouse.
            g.setColour(active            ? Theme::teal()
                        : down            ? Theme::pill().darker(0.12f)
                        : highlighted     ? Theme::pill().darker(0.05f)
                                          : Theme::pill());
            g.fillRoundedRectangle(bounds, Theme::cornerRadius);

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

            g.setColour(ink);
            g.setFont(Fonts::light(15.0f));
            g.drawText(isWire ? "Wire" : getElementInfo(type).name, bounds.reduced(6.0f, 0.0f),
                       juce::Justification::centredLeft, true);
        }

        ElementType type{};
        bool isWire = false;
        bool active = false;
    };

    //==========================================================================

    ElementPalette::ElementPalette()
    {
        // Wire first, above the parts: it is the thing you reach for most, and
        // it is the one row whose position never changes as parts are added.
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

        // Vertical only: the entries are as wide as the panel, so a horizontal
        // bar would only ever be dead space.
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setWantsKeyboardFocus(false);
        addAndMakeVisible(viewport);
    }

    ElementPalette::~ElementPalette() = default;

    void ElementPalette::setActive(const ElementType* type, bool wire)
    {
        for (auto* entry : entries)
            entry->active = entry->isWire ? wire
                                          : (type != nullptr && entry->type == *type);

        repaint();
    }

    void ElementPalette::paint(juce::Graphics& g)
    {
        g.fillAll(Theme::panel());
    }

    void ElementPalette::resized()
    {
        constexpr int rowHeight = Theme::paletteRowHeight;

        viewport.setBounds(getLocalBounds().reduced(4));

        // Asked for rather than assumed: the width available inside the viewport
        // is the panel minus the scrollbar, and whether the bar is there depends
        // on the height we are about to set.
        const int listHeight = rowHeight * entries.size();
        content.setSize(listHeight > viewport.getHeight() ? viewport.getMaximumVisibleWidth()
                                                          : viewport.getWidth(),
                        listHeight);

        auto area = content.getLocalBounds();

        for (auto* entry : entries)
            entry->setBounds(area.removeFromTop(rowHeight));
    }

    //==========================================================================
    // Inspector
    //==========================================================================

    ElementInspector::ElementInspector()
    {
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setWantsKeyboardFocus(false);
        addAndMakeVisible(viewport);

        titleLabel.setFont(Fonts::light(17.0f));
        titleLabel.setColour(juce::Label::textColourId, Theme::text());
        content.addAndMakeVisible(titleLabel);

        for (auto* caption : {&valueCaption, &valueCaptionB, &labelCaption, &modelCaption, &taperCaption,
                              &orderCaption, &knobCaption, &widthCaption, &heightCaption,
                              &scopeMinCaption, &scopeMaxCaption, &scopeSecondsCaption,
                              &cabFileCaption})
        {
            caption->setFont(Fonts::light(12.0f));
            caption->setColour(juce::Label::textColourId, Theme::text().withAlpha(0.65f));
            content.addAndMakeVisible(caption);
        }

        scopeMinCaption.setText("Volts, bottom", juce::dontSendNotification);
        scopeMaxCaption.setText("Volts, top", juce::dontSendNotification);
        scopeSecondsCaption.setText("Time span", juce::dontSendNotification);

        scopeAutoButton.setColour(juce::ToggleButton::textColourId, Theme::text());
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
                modelDescription.setText(getModelDescription(element->type, element->modelIndex),
                                         juce::dontSendNotification);

                if (onEdited)
                    onEdited();
            }
        };
        content.addAndMakeVisible(modelBox);

        modelDescription.setFont(Fonts::light(12.0f));
        modelDescription.setColour(juce::Label::textColourId, Theme::text().withAlpha(0.6f));
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

        polarisedButton.setColour(juce::ToggleButton::textColourId, Theme::text());
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

        rotateButton.onClick = [this] { if (onRotateRequested) onRotateRequested(); };
        content.addAndMakeVisible(rotateButton);

        flipButton.onClick = [this] { if (onFlipRequested) onFlipRequested(); };
        content.addAndMakeVisible(flipButton);

        deleteButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6b2f2f));
        deleteButton.onClick = [this] { if (onDeleteRequested) onDeleteRequested(); };
        content.addAndMakeVisible(deleteButton);

        hintLabel.setFont(Fonts::light(13.0f));
        hintLabel.setColour(juce::Label::textColourId, Theme::text().withAlpha(0.5f));
        hintLabel.setJustificationType(juce::Justification::topLeft);
        content.addAndMakeVisible(hintLabel);

        setElement(nullptr);
    }

    void ElementInspector::setElement(Element* newElement)
    {
        element = newElement;
        rebuildForElement();
        resized();
        repaint();
    }

    void ElementInspector::showNothingSelected()
    {
        titleLabel.setText("Nothing Selected", juce::dontSendNotification);
        hintLabel.setText(  "S  select tool\n"
                            "X  delete tool\n"
                            "F  fit to screen\n\n"
                            "W  wire\n"
                            "G  ground\n\n"
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
                            "Middle-click to copy an element.\n",
                            juce::dontSendNotification);
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
            modelDescription.setText("Not in this build. The part is using "
                                         + choices[0] + " instead.",
                                     juce::dontSendNotification);
        }
        else
        {
            modelBox.setSelectedId(part.modelIndex + 1, juce::dontSendNotification);
            modelDescription.setText(getModelDescription(part.type, part.modelIndex),
                                     juce::dontSendNotification);
        }
    }

    void ElementInspector::fillFieldsFrom(const Element& part)
    {
        const auto& info = getElementInfo(part.type);
        titleLabel.setText(info.name, juce::dontSendNotification);
        hintLabel.setText({}, juce::dontSendNotification);

        if (part.hasNumericValue())
        {
            valueCaption.setText(info.valueLabel, juce::dontSendNotification);

            if (part.hasSecondValue())
            {
                valueCaptionB.setText(info.valueLabelB, juce::dontSendNotification);
                valueEditorB.setText(SymbolPainter::formatValue(part.valueB, ""),
                                     juce::dontSendNotification);
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
        modelDescription.setVisible(models);
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
        const bool orientable = has && element->getPinCount() > 0;
        rotateButton.setVisible(orientable);
        flipButton.setVisible(orientable);
        deleteButton.setVisible(has);
        hintLabel.setVisible(! has);
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

        if (currentView.isVisible())
            currentView.repaint();
    }

    void ElementInspector::CurrentView::paint(juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();

        float amps = 0.0f, watts = 0.0f, peak = 0.0f;
        const bool got = owner.currentReader && owner.currentReader(amps, watts, peak);

        g.setColour(Theme::comment());
        g.setFont(Fonts::light(11.0f));

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
            g.setColour(Theme::comment());
            g.setFont(Fonts::light(11.0f));
            g.drawText(caption, box.removeFromTop(13.0f), juce::Justification::centred, false);

            g.setColour(colour);
            g.setFont(Fonts::light(15.0f));
            g.drawText(value, box, juce::Justification::centred, false);
        };

        const auto third = bounds.getWidth() / 3.0f;

        cell(bounds.removeFromLeft(third), "CURRENT",
             SymbolPainter::formatValue(amps, "A"), Theme::captionValue());
        cell(bounds.removeFromLeft(third), "POWER",
             SymbolPainter::formatValue(watts, "W"), Theme::captionValue());
        cell(bounds, "PEAK",
             SymbolPainter::formatValue(peak, "A"), Theme::captionValue());
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
            g.setFont(Fonts::light(15.0f));
            g.drawText(value, box, juce::Justification::centred, false);
        };

        cell(readout.removeFromLeft(half), "DC",
             SymbolPainter::formatValue(reading.dcAverage, "V"));
        cell(readout, "PEAK-PEAK", SymbolPainter::formatValue(reading.peakToPeak, "V"));
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

        if (element->hasSecondValue()
            && SymbolPainter::parseValue(valueEditorB.getText(), parsed) && parsed > 0.0)
        {
            element->valueB = parsed;
            changed = true;
        }

        if (changed && onEdited)
            onEdited();

        // Either way, show what the values actually are now -- so a rejected
        // entry visibly snaps back rather than sitting there looking accepted.
        valueEditor.setText(valueTextFor(*element), juce::dontSendNotification);

        if (element->hasSecondValue())
            valueEditorB.setText(SymbolPainter::formatValue(element->valueB, ""),
                                 juce::dontSendNotification);
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
            resized();
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

    void ElementInspector::paint(juce::Graphics& g)
    {
        g.fillAll(Theme::chrome());
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

        auto area = content.getLocalBounds().reduced(8);

        titleLabel.setBounds(area.removeFromTop(22));
        area.removeFromTop(6);

        auto field = [&](juce::Component& caption, juce::Component& control)
        {
            if (! control.isVisible())
                return;

            caption.setBounds(area.removeFromTop(14));
            control.setBounds(area.removeFromTop(24));
            area.removeFromTop(6);
        };

        field(valueCaption, valueEditor);
        field(valueCaptionB, valueEditorB);
        field(modelCaption, modelBox);

        if (modelDescription.isVisible())
        {
            modelDescription.setBounds(area.removeFromTop(46));
            area.removeFromTop(4);
        }

        field(taperCaption, taperBox);
        field(orderCaption, orderEditor);
        field(knobCaption, knobEditor);
        field(widthCaption, widthEditor);
        field(heightCaption, heightEditor);
        field(labelCaption, labelEditor);

        for (auto* toggle : {&polarisedButton, &cabButton})
            if (toggle->isVisible())
            {
                toggle->setBounds(area.removeFromTop(24));
                area.removeFromTop(4);
            }

        // Directly under its switch, because the two are one control: the file
        // is what the switch switches in.
        field(cabFileCaption, cabFileButton);

        // The scope's picture, given real room -- it is the whole of what the
        // part is for, and a trace two centimetres wide tells you nothing.
        if (scopeView.isVisible())
        {
            scopeView.setBounds(area.removeFromTop(150));
            area.removeFromTop(6);
        }

        if (scopeAutoButton.isVisible())
        {
            scopeAutoButton.setBounds(area.removeFromTop(24));
            area.removeFromTop(6);
        }

        // Top above bottom, the way they sit on the graph. Reading a pair of
        // bounds in the opposite order to the axis they describe is a small
        // thing that goes wrong every single time you use it.
        field(scopeMaxCaption, scopeMaxEditor);
        field(scopeMinCaption, scopeMinEditor);
        field(scopeSecondsCaption, scopeSecondsEditor);

        if (currentView.isVisible())
        {
            currentView.setBounds(area.removeFromTop(34));
            area.removeFromTop(6);
        }

        if (rotateButton.isVisible())
        {
            auto row = area.removeFromTop(26);
            rotateButton.setBounds(row.removeFromLeft(row.getWidth() / 2 - 3));
            flipButton.setBounds(row.removeFromRight(row.getWidth() - 3));
            area.removeFromTop(4);
        }

        // On its own visibility, not rotate's: Delete shows for anything
        // selected, rotate and flip only for something with pins.
        if (deleteButton.isVisible())
            deleteButton.setBounds(area.removeFromTop(26));

        // The hint only shows when nothing is selected, so it never competes
        // with the fields for room -- it just wants enough to read.
        if (hintLabel.isVisible())
        {
            const int wanted = juce::jmax(200, viewport.getHeight() - area.getY() - 8);
            hintLabel.setBounds(area.removeFromTop(wanted));
        }

        return area.getY() + 8;
    }

    void ElementInspector::resized()
    {
        viewport.setBounds(getLocalBounds());

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
        constexpr int gutterWidth = 57;
        constexpr int promptSize = 22;
        constexpr int promptTop = 12;
    } // namespace

    MessageConsole::MessageConsole()
    {
        viewport.setViewedComponent(&rowList, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setWantsKeyboardFocus(false);
        addAndMakeVisible(viewport);

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
    }

    void MessageConsole::setHeadline(juce::String text, bool isError)
    {
        status = std::move(text);
        statusIsError = isError;
        layOutRows();
        repaint();
    }

    int MessageConsole::measureRows(int width)
    {
        rows.clear();

        const juce::Font font(Fonts::light(13.5f));
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
                                                                                    : Theme::info();

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
        viewport.setBounds(getLocalBounds().withTrimmedLeft(gutterWidth).withTrimmedTop(promptTop
                                                                                       - rowPadding));
        layOutRows();
    }

    void MessageConsole::paint(juce::Graphics& g)
    {
        g.fillAll(Theme::consoleBackground());

        if (promptIcon != nullptr)
            promptIcon->drawWithin(g,
                                   juce::Rectangle<float>(static_cast<float>(rowPadding * 3),
                                                          static_cast<float>(promptTop),
                                                          static_cast<float>(promptSize),
                                                          static_cast<float>(promptSize)),
                                   juce::RectanglePlacement::centred, 1.0f);
    }

    void MessageConsole::mouseDown(const juce::MouseEvent&) {}

    void MessageConsole::Rows::paint(juce::Graphics& g)
    {
        g.setFont(Fonts::light(13.5f));

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
} // namespace SchematicUI
