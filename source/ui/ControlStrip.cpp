#include "ControlStrip.h"

#include "PluginLookAndFeel.h"
#include <CelineUI/Fonts.h>
#include <CelineUI/Theme.h>

namespace Celine
{
    ControlStrip::ControlStrip(PluginProcessor& processor) : processorRef(processor)
    {
        // Horizontal only: the strip is one row, so a vertical bar would be dead
        // space. Neither it nor its contents may take the keyboard off the sheet.
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(false, true);
        viewport.setWantsKeyboardFocus(false);
        addAndMakeVisible(viewport);
    }

    void ControlStrip::refresh()
    {
        // Which part each knob was working before this refresh. A slot that
        // keeps its part keeps its position; one that changes hands takes the
        // position of the part that now owns it. Without that, adding a pot that
        // sorts first shifts every control down and hands Volume's position to
        // Tone.
        std::vector<std::vector<int>> previousIds;
        previousIds.reserve(widgets.size());

        for (const auto& widget : widgets)
            previousIds.push_back(widget.elementIds);

        widgets.clear();

        const auto controls = processorRef.getLiveControls();
        const auto count = juce::jmin(static_cast<int>(controls.size()),
                                      PluginProcessor::maxLiveControls);

        for (int i = 0; i < count; ++i)
        {
            const auto& control = controls[static_cast<size_t>(i)];
            const auto parameterId = PluginProcessor::getControlParameterId(i);

            Widget widget;
            widget.parameter = processorRef.apvts.getParameter(parameterId);
            widget.elementIds = control.elementIds;

            // Upper case, like every other caption in the band. A drawn part's
            // label is whatever was typed into the inspector, so the strip does
            // the shouting rather than asking people to type in capitals.
            widget.label = std::make_unique<juce::Label>(juce::String{},
                                                        control.name.toUpperCase());
            widget.label->setFont(Fonts::light(Theme::bandCaptionFontHeight));
            widget.label->setJustificationType(juce::Justification::centred);
            widget.label->setColour(juce::Label::textColourId, Theme::textOnPanel());
            content.addAndMakeVisible(widget.label.get());

            if (control.kind == SchematicModel::LiveControl::Kind::Switch)
            {
                // A switch is still a float parameter -- the pool has to be fixed
                // at construction and can't know which slots become switches --
                // so the button drives it directly and syncToggles() keeps it in
                // step with the host.
                widget.toggle = std::make_unique<juce::ToggleButton>(juce::String{});

                // Named for the accessibility layer even though it draws no
                // text: the label above is the name, and repeating it beside
                // the pill would say the same thing twice.
                widget.toggle->setTitle(control.name);
                widget.toggle->getProperties().set(pillSwitchProperty, true);
                widget.toggle->setColour(juce::ToggleButton::textColourId, Theme::textOnPanel());

                auto* parameter = widget.parameter;
                auto* button = widget.toggle.get();

                widget.toggle->onClick = [this, parameter, button, ids = widget.elementIds]
                {
                    const float position = button->getToggleState() ? 1.0f : 0.0f;

                    if (parameter != nullptr)
                    {
                        parameter->beginChangeGesture();
                        parameter->setValueNotifyingHost(position);
                        parameter->endChangeGesture();
                    }

                    if (onControlMoved)
                        onControlMoved(ids, position);
                };

                content.addAndMakeVisible(widget.toggle.get());
            }
            else
            {
                widget.slider = std::make_unique<juce::Slider>();
                widget.slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);


                // The wheel scrolls the strip rather than turning the knob
                // under the pointer. Slider::mouseWheelMove falls through to
                // Component::mouseWheelMove when this is off, so the event
                // reaches the Viewport.
                widget.slider->setScrollWheelEnabled(false);

                content.addAndMakeVisible(widget.slider.get());

                widget.attachment =
                    std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                        processorRef.apvts, parameterId, *widget.slider);

                // Its position, as a percentage -- see formatPosition for why not
                // the resistance. Typed back the same way, the % optional.
                showValueBelow(*widget.slider, formatPosition, [](const juce::String& text)
                               { return juce::jlimit(0.0, 1.0, text.getDoubleValue() / 100.0); });

                // Fires for a drag and for the host moving the parameter alike,
                // since the attachment drives the slider either way -- which is
                // what keeps the drawing in step with automation too.
                widget.slider->onValueChange = [this, slider = widget.slider.get(),
                                                ids = widget.elementIds]
                {
                    if (onControlMoved)
                        onControlMoved(ids, static_cast<float>(slider->getValue()));
                };
            }

            widgets.push_back(std::move(widget));
        }

        for (int i = 0; i < count; ++i)
        {
            const bool samePart = i < static_cast<int>(previousIds.size())
                               && previousIds[static_cast<size_t>(i)]
                                      == widgets[static_cast<size_t>(i)].elementIds;

            if (! samePart)
                pushPositionFor(controls[static_cast<size_t>(i)].primaryElementId());
        }

        // A button has no attachment to seed it, so it sat at its default off
        // until the editor's next timer tick and flickered on every rebuild.
        // After the loop above, so a slot that changed hands shows its new part.
        syncToggles();

        resized();
        repaint();

        if (onContentChanged != nullptr)
            onContentChanged();
    }

    void ControlStrip::pushPositionFor(int elementId)
    {
        for (const auto& widget : widgets)
        {
            if (widget.parameter == nullptr
                || std::find(widget.elementIds.begin(), widget.elementIds.end(), elementId)
                       == widget.elementIds.end())
                continue;

            if (const auto* element = processorRef.getSchematic().findElement(elementId))
            {
                const auto position = element->getControlPosition();

                if (! juce::approximatelyEqual(widget.parameter->getValue(), position))
                {
                    widget.parameter->beginChangeGesture();
                    widget.parameter->setValueNotifyingHost(position);
                    widget.parameter->endChangeGesture();
                }
            }

            return;
        }
    }

    void ControlStrip::syncToggles()
    {
        for (auto& widget : widgets)
        {
            if (widget.toggle == nullptr || widget.parameter == nullptr)
                continue;

            const bool on = widget.parameter->getValue() >= 0.5f;

            if (widget.toggle->getToggleState() != on)
                widget.toggle->setToggleState(on, juce::dontSendNotification);
        }
    }

    void ControlStrip::showValueBelow(juce::Slider& knob, std::function<juce::String(double)> format,
                                      std::function<double(const juce::String&)> parse)
    {
        knob.getProperties().set(panelReadoutProperty, true);
        knob.textFromValueFunction = std::move(format);
        knob.valueFromTextFunction = std::move(parse);

        // As wide as the cell rather than the cap: "-12.5 dB" is wider than a
        // knob, and a value clipped to "-12." is worse than none.
        knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, cellWidth - 4, valueHeight);
        knob.updateText();
    }

    juce::String ControlStrip::formatPosition(double position)
    {
        return juce::String(juce::roundToInt(position * 100.0)) + "%";
    }

    juce::String ControlStrip::formatDecibels(double decibels)
    {
        // Rounded first, so a value a hair under zero does not read "-0.0".
        const auto tenths = juce::roundToInt(decibels * 10.0);
        return (tenths > 0 ? "+" : "") + juce::String(tenths / 10.0, 1) + " dB";
    }

    void ControlStrip::paint(juce::Graphics& g)
    {
        // The second of the two light surfaces -- see Theme.h. It painted nothing
        // before and let the editor's chrome show through, which was fine while
        // everything was one colour and wrong the moment it wasn't.
        // A floating bar like the other panels, so rounded, and drawn inside
        // the strip rather than by the editor: the strip is the bar.
        g.setColour(Theme::panel());
        g.fillRoundedRectangle(getLocalBounds().toFloat(), Theme::bandCornerRadius);
    }

    juce::Rectangle<int> ControlStrip::layOutCell(juce::Rectangle<int> cell, juce::Label* label,
                                                  int controlHeight)
    {
        const int stack = labelHeight + labelGap + controlHeight;
        auto content = cell.withSizeKeepingCentre(cell.getWidth(),
                                                  juce::jmin(cell.getHeight(), stack));

        if (label != nullptr)
            label->setBounds(content.removeFromTop(labelHeight));

        content.removeFromTop(labelGap);
        return content;
    }

    void ControlStrip::resized()
    {
        viewport.setBounds(getLocalBounds().reduced(padding, 4));

        // Cells keep a usable width however many there are, and the scrollbar
        // appears only when they overflow -- which is why the content's height
        // has to shrink by the bar when it does.
        const int count = static_cast<int>(widgets.size());
        const bool scrolling = count * cellWidth > viewport.getWidth();

        content.setSize(juce::jmax(viewport.getWidth(), count * cellWidth),
                        scrolling ? viewport.getMaximumVisibleHeight() : viewport.getHeight());

        auto row = content.getLocalBounds();

        for (auto& widget : widgets)
        {
            auto cell = row.removeFromLeft(cellWidth);

            if (widget.slider != nullptr)
            {
                // The cap and the value under it, as one control: the slider
                // draws the cap in what is left above its value box.
                const auto face = layOutCell(cell, widget.label.get(), knobSize + valueHeight);
                widget.slider->setBounds(face.withSizeKeepingCentre(cellWidth - 4, knobSize + valueHeight));
            }
            else if (widget.toggle != nullptr)
            {
                const auto pill = layOutCell(cell, widget.label.get(), toggleHeight);
                widget.toggle->setBounds(
                    pill.withSizeKeepingCentre(juce::jmin(toggleWidth, cell.getWidth() - 4), toggleHeight));
            }
        }
    }
} // namespace Celine
