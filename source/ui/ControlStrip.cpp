#include "ControlStrip.h"

#include "PluginLookAndFeel.h"
#include <CelineUI/Fonts.h>
#include <CelineUI/Theme.h>

namespace Celine
{
    ControlStrip::ControlStrip(PluginProcessor& processor) : processorRef(processor)
    {
        // Horizontal scrolling only; never take keyboard focus from the sheet.
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(false, true);
        viewport.setWantsKeyboardFocus(false);
        addAndMakeVisible(viewport);
    }

    void ControlStrip::refresh()
    {
        // Which parts each slot worked before this refresh. A slot that keeps its part keeps
        // its position; one that changes hands takes its new part's position.
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

            // Captions are upper case.
            widget.label = std::make_unique<juce::Label>(juce::String{},
                                                        control.name.toUpperCase());
            widget.label->setFont(Fonts::light(Theme::bandCaptionFontHeight));
            widget.label->setJustificationType(juce::Justification::centred);
            widget.label->setColour(juce::Label::textColourId, Theme::textOnPanel());
            content.addAndMakeVisible(widget.label.get());

            if (control.kind == SchematicModel::LiveControl::Kind::Switch && control.positions == 3)
            {
                widget.lever = std::make_unique<LeverSwitch>();
                widget.lever->setTitle(control.name);
                widget.lever->offPosition = control.offPosition;

                auto* parameter = widget.parameter;

                widget.lever->onChange = [this, parameter, ids = widget.elementIds](int stop)
                {
                    const float value = static_cast<float>(stop) / static_cast<float>(LeverSwitch::positions - 1);

                    if (parameter != nullptr)
                    {
                        parameter->beginChangeGesture();
                        parameter->setValueNotifyingHost(value);
                        parameter->endChangeGesture();
                    }

                    if (onControlMoved)
                        onControlMoved(ids, value);
                };

                content.addAndMakeVisible(widget.lever.get());
            }
            else if (control.kind == SchematicModel::LiveControl::Kind::Switch)
            {
                // Switches use a float parameter like the knobs; the widget drives it directly and
                // syncToggles() follows host changes.
                widget.toggle = std::make_unique<juce::ToggleButton>(juce::String{});

                // Accessible name; the caption above shows it visually.
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


                // The wheel scrolls the strip instead of turning knobs.
                widget.slider->setScrollWheelEnabled(false);

                content.addAndMakeVisible(widget.slider.get());

                widget.attachment =
                    std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                        processorRef.apvts, parameterId, *widget.slider);

                // Position as a percentage; the % is optional when typing.
                showValueBelow(*widget.slider, formatPosition, [](const juce::String& text)
                               { return juce::jlimit(0.0, 1.0, text.getDoubleValue() / 100.0); });

                // Fires for drags and host automation alike, so the drawing follows both.
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

        // Show switch states now rather than on the next timer tick.
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
            if (widget.lever != nullptr && widget.parameter != nullptr)
            {
                widget.lever->setPosition(SchematicModel::LiveControl::Contact::positionFor(
                                              widget.parameter->getValue(), LeverSwitch::positions),
                                          juce::dontSendNotification);
                continue;
            }

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

        // As wide as the cell, so "-12.5 dB" fits.
        knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, cellWidth - 4, valueHeight);
        knob.updateText();
    }

    juce::String ControlStrip::formatPosition(double position)
    {
        return juce::String(juce::roundToInt(position * 100.0)) + "%";
    }

    juce::String ControlStrip::formatDecibels(double decibels)
    {
        // Round first, so nothing reads "-0.0".
        const auto tenths = juce::roundToInt(decibels * 10.0);
        return (tenths > 0 ? "+" : "") + juce::String(tenths / 10.0, 1) + " dB";
    }

    juce::String ControlStrip::formatDbu(double dbu)
    {
        // Round first, so nothing reads "-0.00".
        const auto hundredths = juce::roundToInt(dbu * 100.0);
        return (hundredths > 0 ? "+" : "") + juce::String(hundredths / 100.0, 2) + " dBu";
    }

    void ControlStrip::paint(juce::Graphics& g)
    {
        // The strip's light background, a rounded bar.
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

        // Fixed-width cells; the content shrinks by the scrollbar's height when it scrolls.
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
                // Knob cap above its value box.
                const auto face = layOutCell(cell, widget.label.get(), knobSize + valueHeight);
                widget.slider->setBounds(face.withSizeKeepingCentre(cellWidth - 4, knobSize + valueHeight));
            }
            else if (widget.toggle != nullptr)
            {
                const auto pill = layOutCell(cell, widget.label.get(), toggleHeight);
                widget.toggle->setBounds(
                    pill.withSizeKeepingCentre(juce::jmin(toggleWidth, cell.getWidth() - 4), toggleHeight));
            }
            else if (widget.lever != nullptr)
            {
                const auto pill = layOutCell(cell, widget.label.get(), toggleHeight);
                widget.lever->setBounds(
                    pill.withSizeKeepingCentre(juce::jmin(toggleWidth + 20, cell.getWidth() - 4), toggleHeight));
            }
        }
    }

    //==========================================================================
    void LeverSwitch::setPosition(int newPosition, juce::NotificationType notification)
    {
        newPosition = juce::jlimit(0, positions - 1, newPosition);

        if (newPosition == position)
            return;

        position = newPosition;
        repaint();

        if (notification != juce::dontSendNotification && onChange)
            onChange(position);
    }

    juce::Rectangle<float> LeverSwitch::trackBounds() const
    {
        // The toggle's height, one stop wider.
        const auto bounds = getLocalBounds().toFloat();
        const float h = juce::jmin(bounds.getHeight(), 20.0f);
        return bounds.withSizeKeepingCentre(juce::jmin(bounds.getWidth(), h * 2.8f), h);
    }

    int LeverSwitch::stopAt(float x) const
    {
        const auto track = trackBounds();
        const float third = track.getWidth() / static_cast<float>(positions);
        return juce::jlimit(0, positions - 1, static_cast<int>((x - track.getX()) / third));
    }

    void LeverSwitch::paint(juce::Graphics& g)
    {
        const auto track = trackBounds();
        const float h = track.getHeight();
        const bool lit = position != offPosition;
        const float alpha = isEnabled() ? 1.0f : 0.45f;

        g.setColour((lit ? Theme::accent() : Theme::pill().darker(0.18f)).withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(track, h * 0.5f);

        const float inset = 2.0f, dot = h - 2.0f * inset;
        const float travel = track.getWidth() - dot - 2.0f * inset;
        const auto stopX = [&](int k)
        { return track.getX() + inset + travel * static_cast<float>(k) / static_cast<float>(positions - 1); };

        // A dot at each stop the lever is not on.
        g.setColour(Theme::panel().withMultipliedAlpha(0.55f * alpha));

        for (int k = 0; k < positions; ++k)
            if (k != position)
                g.fillEllipse(juce::Rectangle<float>(4.0f, 4.0f).withCentre({stopX(k) + dot * 0.5f, track.getCentreY()}));

        g.setColour(Theme::panel().withMultipliedAlpha(alpha));
        g.fillEllipse(stopX(position), track.getY() + inset, dot, dot);
    }

    void LeverSwitch::mouseDown(const juce::MouseEvent& event)
    {
        setPosition(stopAt(event.position.x), juce::sendNotificationSync);
    }

    void LeverSwitch::mouseDrag(const juce::MouseEvent& event)
    {
        setPosition(stopAt(event.position.x), juce::sendNotificationSync);
    }
} // namespace Celine
