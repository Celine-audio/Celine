#include "ZoomStrip.h"

#include <CelineUI/Theme.h>

namespace Celine
{
    namespace
    {
        // Inset as the palette's entries are, and as big: folded, the two glyphs sit
        // exactly under its two columns of symbols.
        constexpr int padX = 10, padY = 5, button = Theme::paletteRowHeight - 4;
    } // namespace

    //==========================================================================
    ZoomStrip::ZoomStrip()
    {
        outButton.onClick = [this] { if (onStep) onStep (false); };
        inButton.onClick = [this] { if (onStep) onStep (true); };
        addAndMakeVisible (outButton);
        addAndMakeVisible (inButton);

        // The house slider, as every plugin of the house draws it.
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange (0.0, 1.0);
        slider.setScrollWheelEnabled (false);
        slider.setWantsKeyboardFocus (false);
        slider.setTooltip ("Zoom");
        slider.onValueChange = [this]
        {
            if (onZoomChosen)
                onZoomChosen (static_cast<float> (slider.getValue()));
        };
        addAndMakeVisible (slider);

        lookAndFeelChanged();
        setOpaque (false);
    }

    void ZoomStrip::showZoom (float proportion)
    {
        slider.setValue (juce::jlimit (0.0, 1.0, static_cast<double> (proportion)), juce::dontSendNotification);
    }

    void ZoomStrip::resized()
    {
        // How much room there is for the slider: none folded, all of it open.
        const float room = juce::jlimit (0.0f, 1.0f, (float) (getWidth() - 2 * button - 2 * padX) / (float) (sliderWidth - 2 * button - 2 * padX));

        // Folded, the glyphs are the palette's pills and sit under its columns; open, they
        // narrow so the slider runs close to them rather than leaving a gap either side.
        const int glyph = juce::roundToInt ((float) button - room * (float) (button - 24));

        auto area = getLocalBounds().reduced (padX, padY);
        outButton.setBounds (area.removeFromLeft (glyph));
        inButton.setBounds (area.removeFromRight (glyph));

        // The slider across the whole gap between the glyphs, fading in with the width as
        // the palette's names do; gone when folded.
        area.reduce (2, 0);
        slider.setBounds (area);
        slider.setAlpha (room * room);
        slider.setVisible (area.getWidth() >= 30);
    }

    void ZoomStrip::lookAndFeelChanged()
    {
        slider.setColour (juce::Slider::trackColourId, Theme::accent());
        slider.setColour (juce::Slider::backgroundColourId, Theme::pill());
        slider.setColour (juce::Slider::thumbColourId, Theme::accent());
        repaint();
    }

    void ZoomStrip::paint (juce::Graphics& g)
    {
        g.setColour (Theme::panel());
        g.fillRoundedRectangle (getLocalBounds().toFloat(), Theme::paletteCornerRadius);
    }
} // namespace Celine
