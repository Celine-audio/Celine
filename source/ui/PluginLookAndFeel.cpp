#include "PluginLookAndFeel.h"

#include "EmbeddedAssets.h"
#include "Fonts.h"

using namespace Celine;

PluginLookAndFeel::PluginLookAndFeel()
{
    applyPalette();
}

PluginLookAndFeel::~PluginLookAndFeel() = default;

void PluginLookAndFeel::applyPalette()
{
    LookAndFeelBase::applyPalette();

    // The knob face, tinted here rather than per frame: it is the same colour every
    // time it is drawn, and a drawable copy per knob per repaint would be allocation on
    // the paint path.
    //
    // Re-read from the binary rather than re-tinted in place, because this runs again on
    // every theme change and tinting is destructive -- the second pass would be
    // colouring whatever the first pass left, not the artwork.
    knob = Assets::drawable("knob.svg");
    ioKnob = Assets::drawable("knob_io.svg");

    for (auto* face : { knob.get(), ioKnob.get() })
        if (face != nullptr)
            Assets::tint(*face, Theme::textOnPanel());
}

//==============================================================================
void PluginLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPosProportional, float rotaryStartAngle,
                                         float rotaryEndAngle, juce::Slider& slider)
{
    // Which cap this is: the plugin's own gain wears a smooth one against
    // the fluted cap a drawn knob gets, so the two kinds of control in the
    // bottom band are told apart by the face and not by which end they sit
    // at.
    const auto* cap = slider.getProperties().contains(digitalGainProperty) ? ioKnob.get()
                                                                          : knob.get();

    if (cap == nullptr)
    {
        // No artwork: JUCE's own rotary beats drawing nothing.
        LookAndFeel_V4::drawRotarySlider(g, x, y, width, height, sliderPosProportional,
                                         rotaryStartAngle, rotaryEndAngle, slider);
        return;
    }

    const auto area = juce::Rectangle<int>(x, y, width, height).toFloat();

    // Square, because the cap is. A knob in a cell taller than it is wide
    // would otherwise be drawn as an ellipse.
    const auto face = area.withSizeKeepingCentre(juce::jmin(area.getWidth(), area.getHeight()),
                                                 juce::jmin(area.getWidth(), area.getHeight()));

    const auto angle = rotaryStartAngle
                     + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    juce::Graphics::ScopedSaveState state(g);

    // The pointer is drawn straight up in both files, so rotating the whole
    // cap about its centre *is* the readout. No arc: the design has none,
    // and inventing one would be my design rather than the drawn one.
    g.addTransform(juce::AffineTransform::rotation(angle, face.getCentreX(), face.getCentreY()));
    cap->drawWithin(g, face, juce::RectanglePlacement::centred, 1.0f);
}

void PluginLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                         bool shouldDrawButtonAsHighlighted,
                                         bool shouldDrawButtonAsDown)
{
    if (! button.getProperties().contains(pillSwitchProperty))
    {
        LookAndFeel_V4::drawToggleButton(g, button, shouldDrawButtonAsHighlighted,
                                         shouldDrawButtonAsDown);
        return;
    }

    // A pill with a travelling dot, which is what the design draws wherever a
    // switch appears. JUCE's tick box would be the only square-cornered,
    // unfilled control in the window.
    const auto bounds = button.getLocalBounds().toFloat();
    const bool on = button.getToggleState();

    juce::ignoreUnused(shouldDrawButtonAsDown);

    // Sized to itself, not stretched to whatever the layout handed the
    // button. An inspector row is as wide as the panel, and a switch that
    // wide reads as a banner rather than as a control.
    const float height = juce::jmin(bounds.getHeight(), 20.0f);
    const float width = height * 2.8f;
    const bool labelled = button.getButtonText().isNotEmpty();

    // Left when there is a word to sit beside it, centred when the pill is
    // the whole control -- which is the control strip, where the name is
    // already on the label above.
    const auto pill =
        juce::Rectangle<float>(width, height)
            .withPosition(labelled ? bounds.getX() : bounds.getCentreX() - width * 0.5f,
                          bounds.getCentreY() - height * 0.5f);

    g.setColour(on ? Theme::teal() : Theme::surfaceBright());
    g.fillRoundedRectangle(pill, height * 0.5f);

    if (shouldDrawButtonAsHighlighted)
    {
        g.setColour(Theme::textOnPanel().withAlpha(0.15f));
        g.fillRoundedRectangle(pill, height * 0.5f);
    }

    // The dot is the throw, and it sits on the side that says which one is
    // made: left for off, right for on, the way every hardware toggle reads.
    // Dark, because on the accent it is the only thing that can be.
    const float inset = 1.5f;
    const float dot = height - inset * 2.0f;
    const float travel = pill.getWidth() - dot - inset * 2.0f;
    const float dotX = pill.getX() + inset + (on ? travel : 0.0f);

    g.setColour(Theme::textOnPanel());
    g.fillEllipse(juce::Rectangle<float>(dot, dot).withPosition(dotX, pill.getY() + inset));

    // The *state*, in the half the dot is not in. This used to draw the
    // button's own text, which is fixed at construction -- so every switch
    // read the same word in both positions and told you nothing.
    g.setColour(on ? Theme::textOnPanel() : Theme::text());
    g.setFont(Fonts::light(11.0f));
    g.drawText(on ? "ON" : "OFF",
               on ? pill.withTrimmedRight(dot + inset * 2.0f)
                  : pill.withTrimmedLeft(dot + inset * 2.0f),
               juce::Justification::centred, false);

    // What the switch is for goes beside it, now that the pill is busy
    // saying what it is doing.
    if (labelled)
    {
        g.setColour(button.findColour(juce::ToggleButton::textColourId));
        g.setFont(Fonts::light(13.0f));
        g.drawText(button.getButtonText(),
                   bounds.withTrimmedLeft(pill.getWidth() + 8.0f),
                   juce::Justification::centredLeft, true);
    }
}
