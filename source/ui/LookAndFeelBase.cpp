#include "LookAndFeelBase.h"

#include "EmbeddedAssets.h"
#include "Fonts.h"

using namespace Celine;

namespace
{
    /** How far text is lifted off the geometric centre, as a fraction of the font
        height.

        Not a fudge for a positioning bug. Jura's ascenders reach cap height and UI
        strings here carry no descenders, so what JUCE centres is the block from cap
        top to baseline, exactly — while what the eye centres on is the x-height
        mass, which sits in the lower half of that block. Arithmetically centred text
        therefore reads low in every button and dropdown. Zero restores JUCE's own
        centring. */
    constexpr float opticalRise = 0.09f;

    // The combo chevron's placement, shared between the two functions that need to
    // agree about it: where it is drawn, and how much room the text has beside it.
    constexpr float comboArrowInset = 16.0f;   // of its centre, from the right edge
    constexpr float comboArrowReach = 4.5f;

    int riseFor (const juce::Font& font) noexcept
    {
        return juce::roundToInt (font.getHeight() * opticalRise);
    }

    /** Half the shorter side, which is what makes a rectangle a pill rather than
        merely a rounded one: the short ends come out as full semicircles however the
        thumb is proportioned. */
    float pillRadius (juce::Rectangle<float> bounds) noexcept
    {
        return juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    }
}

//==============================================================================
LookAndFeelBase::LookAndFeelBase()
{
    if (auto face = Fonts::typeface (Fonts::Weight::Light))
        setDefaultSansSerifTypeface (face);

    applyPalette();
}

void LookAndFeelBase::applyPalette()
{
    using namespace Theme;

    setColour (juce::ResizableWindow::backgroundColourId, chrome());
    setColour (juce::DocumentWindow::textColourId, text());

    setColour (juce::TextButton::buttonColourId, button());
    setColour (juce::TextButton::buttonOnColourId, surfaceBright());
    setColour (juce::TextButton::textColourOffId, text());
    setColour (juce::TextButton::textColourOnId, text());

    setColour (juce::ToggleButton::textColourId, text());
    setColour (juce::ToggleButton::tickColourId, teal());
    setColour (juce::ToggleButton::tickDisabledColourId, line());

    setColour (juce::Label::textColourId, text());
    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::backgroundWhenEditingColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::outlineWhenEditingColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textWhenEditingColourId, text());

    setColour (juce::TextEditor::backgroundColourId, field());
    setColour (juce::TextEditor::textColourId, text());
    setColour (juce::TextEditor::highlightColourId, surfaceBright());
    setColour (juce::TextEditor::highlightedTextColourId, text());
    // No rule around a field at rest. It is filled with a colour that already separates
    // it from what it stands on, like every button and dropdown here -- and a border on
    // every field is what made these read as system controls dropped into the window.
    // The focused ring stays: that one is saying something.
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    // No rule on a focused field either. The caret is already saying where the typing
    // goes, and a ring that appears the moment you click is the one that reads as a
    // system control: it is the only edge in the window that arrives on a click.
    setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::CaretComponent::caretColourId, text());

    setColour (juce::ComboBox::backgroundColourId, button());
    setColour (juce::ComboBox::textColourId, text());
    setColour (juce::ComboBox::arrowColourId, textDim());
    setColour (juce::ComboBox::buttonColourId, button());

    // No rule around a dropdown, closed or focused. It is filled with a colour that
    // already separates it from what it stands on, like every other button here.
    setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::focusedOutlineColourId, juce::Colours::transparentBlack);

    // Transparent, and deliberately so: this is what lets a menu have rounded corners.
    //
    // PopupMenu makes its window opaque when this colour is opaque, and an opaque
    // component must paint every pixel it owns -- so a rounded background left the four
    // corners unpainted and they came out as dark squares. Handing it a transparent
    // colour makes the window transparent, and the corners are then genuinely absent
    // rather than merely unpainted. Nothing draws with this: drawPopupMenuBackground
    // below paints the panel itself.
    setColour (juce::PopupMenu::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::PopupMenu::textColourId, text());
    setColour (juce::PopupMenu::headerTextColourId, comment());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, surfaceBright());
    setColour (juce::PopupMenu::highlightedTextColourId, text());

    setColour (juce::Slider::rotarySliderFillColourId, teal());
    setColour (juce::Slider::rotarySliderOutlineColourId, surfaceBright());
    setColour (juce::Slider::thumbColourId, text());
    // The filled part of a track is the plugin's accent; the groove behind it is the
    // ground everything else sits on.
    setColour (juce::Slider::trackColourId, accent());
    setColour (juce::Slider::backgroundColourId, background());
    setColour (juce::Slider::textBoxTextColourId, text());
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, surfaceBright());

    setColour (juce::ScrollBar::thumbColourId, line());
    setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);

    setColour (juce::TooltipWindow::backgroundColourId, surface());
    setColour (juce::TooltipWindow::textColourId, text());
    setColour (juce::TooltipWindow::outlineColourId, juce::Colours::transparentBlack);

    // JUCE's own Audio/MIDI settings dialog is the only place a ListBox appears -- the
    // MIDI input picker in a standalone build. Left unset it draws white on white.
    setColour (juce::ListBox::backgroundColourId, background());
    setColour (juce::ListBox::outlineColourId, line());
    setColour (juce::ListBox::textColourId, text());

    setColour (juce::AlertWindow::backgroundColourId, chrome());
    setColour (juce::AlertWindow::textColourId, text());
    setColour (juce::AlertWindow::outlineColourId, line());
}

LookAndFeelBase::~LookAndFeelBase() = default;

//==============================================================================
void LookAndFeelBase::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPosProportional, float rotaryStartAngle,
                                         float rotaryEndAngle, juce::Slider& slider)
{
    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();

    // Square, because the cap is. A knob in a cell taller than it is wide would
    // otherwise be drawn as an ellipse.
    const auto side = juce::jmin (area.getWidth(), area.getHeight());
    const auto square = area.withSizeKeepingCentre (side, side);

    const auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    // A ring around the cap, and the value drawn on it. GALLERY's mockup rings every
    // knob -- which the plugins this kit came from do not -- because four strips of
    // four knobs is too many pointers to read one at a time: the arc is what makes a
    // row of them scannable, and it is where a slot's colour appears next to the
    // controls that belong to it.
    const auto ring = square.reduced (side * 0.06f);
    const auto thickness = juce::jmax (2.0f, side * 0.075f);
    const auto radius = (ring.getWidth() - thickness) * 0.5f;
    const auto centre = ring.getCentre();

    const auto arc = [&] (float from, float to, juce::Colour colour)
    {
        if (std::abs (to - from) < 1.0e-4f || radius <= 0.0f)
            return;

        juce::Path path;
        path.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                            juce::jmin (from, to), juce::jmax (from, to), true);

        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    };

    arc (rotaryStartAngle, rotaryEndAngle,
         slider.isEnabled() ? Theme::track() : Theme::track().withAlpha (0.5f));

    // A bipolar parameter fills out from twelve o'clock in whichever direction it has
    // been pushed, so the sign is visible without reading the number under it;
    // everything else fills from the start of the travel. Same rule the faders use,
    // and inferred the same way rather than declared, so a parameter cannot be given
    // a bipolar range and a unipolar arc.
    const auto bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const auto origin = bipolar ? (rotaryStartAngle + rotaryEndAngle) * 0.5f : rotaryStartAngle;

    arc (origin, angle, slider.isEnabled() ? slider.findColour (juce::Slider::rotarySliderFillColourId)
                                           : Theme::textDisabled());

    // The cap, inside the ring it is read against. Drawn rather than taken from
    // `knob.svg`, which the kit's other plugins use: that artwork is a fluted cap, and
    // at the size four strips of four knobs comes out at the flutes read as a cog.
    // GALLERY's mockup draws a plain disc with a single pointer, which stays legible
    // at any size and is four lines of drawing.
    const auto face = square.reduced (thickness + side * 0.07f);
    const auto capRadius = face.getWidth() * 0.5f;

    g.setColour (slider.isEnabled() ? Theme::panel() : Theme::panel().withAlpha (0.4f));
    g.fillEllipse (face);

    // The pointer runs from the middle of the cap to just inside its edge, so it reads
    // as a hand rather than as a tick mark stuck on the rim.
    const auto direction = juce::Point<float> (std::sin (angle), -std::cos (angle));

    juce::Path pointer;
    pointer.startNewSubPath (centre + direction * (capRadius * 0.12f));
    pointer.lineTo (centre + direction * (capRadius * 0.82f));

    g.setColour (Theme::background());
    g.strokePath (pointer, juce::PathStrokeType (juce::jmax (1.5f, capRadius * 0.13f),
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

void LookAndFeelBase::drawSliderGroove (juce::Graphics& g, juce::Rectangle<float> track,
                                          float radius, juce::Slider& slider)
{
    // The graph's own ground, whichever side of the two-tone split the slider stands
    // on: it reads as a channel cut into the surface rather than a gap left in it, and
    // being one colour on the light panel and the dark surround keeps the two rows of
    // controls agreeing.
    g.setColour (slider.findColour (juce::Slider::backgroundColourId));
    g.fillRoundedRectangle (track, radius);

    g.setColour (Theme::line().withAlpha (0.25f));
    g.drawRoundedRectangle (track, radius, Theme::borderWidth);
}

void LookAndFeelBase::drawSliderFill (juce::Graphics& g, juce::Rectangle<float> filled,
                                        float radius, juce::Slider& slider)
{
    if (filled.getWidth() <= 1.0f || filled.getHeight() <= 1.0f)
        return;

    g.setColour (slider.isEnabled() ? slider.findColour (juce::Slider::trackColourId)
                                    : Theme::textDisabled());
    g.fillRoundedRectangle (filled, radius);
}

void LookAndFeelBase::drawHorizontalSlider (juce::Graphics& g, juce::Rectangle<float> bounds,
                                              float sliderPos, juce::Slider& slider)
{
    constexpr float trackHeight = 6.0f;
    constexpr float thumbWidth = 14.0f;
    constexpr float radius = trackHeight * 0.5f;

    // Inset by the thumb, so it stays fully inside the component at both extremes.
    const auto track = bounds.withSizeKeepingCentre (bounds.getWidth() - thumbWidth, trackHeight);
    const auto position = juce::jlimit (track.getX(), track.getRight(), sliderPos);

    drawSliderGroove (g, track, radius, slider);
    drawSliderFill (g, track.withRight (position), radius, slider);

    const auto thumb = juce::Rectangle<float> (thumbWidth, bounds.getHeight() * 0.62f)
                           .withCentre ({ position, bounds.getCentreY() });

    g.setColour (slider.findColour (juce::Slider::thumbColourId));
    g.fillRoundedRectangle (thumb, pillRadius (thumb));
}

void LookAndFeelBase::drawVerticalSlider (juce::Graphics& g, juce::Rectangle<float> bounds,
                                            float sliderPos, juce::Slider& slider)
{
    constexpr float trackWidth = 6.0f;
    constexpr float thumbHeight = 14.0f;
    constexpr float radius = trackWidth * 0.5f;

    const auto track = bounds.withSizeKeepingCentre (trackWidth, bounds.getHeight() - thumbHeight);
    const auto position = juce::jlimit (track.getY(), track.getBottom(), sliderPos);

    drawSliderGroove (g, track, radius, slider);

    // A bipolar parameter fills out from its zero point in whichever direction it has
    // been pushed, so the sign is visible at a glance; everything else fills up from
    // the bottom like a mix fader.
    const auto bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;

    auto origin = track.getBottom();

    if (bipolar)
    {
        const auto span = slider.getMaximum() - slider.getMinimum();
        const auto zero = (float) ((0.0 - slider.getMinimum()) / span);

        origin = track.getBottom() - zero * track.getHeight();

        g.setColour (Theme::line().withAlpha (0.5f));
        g.drawHorizontalLine ((int) origin, track.getX() - 4.0f, track.getRight() + 4.0f);
    }

    drawSliderFill (g, { track.getX(), std::min (position, origin),
                         track.getWidth(), std::abs (position - origin) },
                    radius, slider);

    // Capped rather than a flat fraction of the column, so this comes out the same
    // compact pill the bottom row wears: the two faders sit in a column much wider than
    // that row is tall, and 62% of it made a slab where the other is a lozenge.
    const auto thumbWidth = juce::jmin (bounds.getWidth() * 0.62f, thumbHeight * 1.3f);

    const auto thumb = juce::Rectangle<float> (thumbWidth, thumbHeight)
                           .withCentre ({ bounds.getCentreX(), position });

    g.setColour (slider.findColour (juce::Slider::thumbColourId));
    g.fillRoundedRectangle (thumb, pillRadius (thumb));
}

void LookAndFeelBase::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float minSliderPos, float maxSliderPos,
                                          juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();

    if (style == juce::Slider::LinearHorizontal)
        drawHorizontalSlider (g, bounds, sliderPos, slider);
    else if (style == juce::Slider::LinearVertical)
        drawVerticalSlider (g, bounds, sliderPos, slider);
    else
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                                minSliderPos, maxSliderPos, style, slider);
}

//==============================================================================
void LookAndFeelBase::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                         bool shouldDrawButtonAsHighlighted,
                                         bool shouldDrawButtonAsDown)
{
    if (! button.getProperties().contains (pillSwitchProperty))
    {
        LookAndFeel_V4::drawToggleButton (g, button, shouldDrawButtonAsHighlighted,
                                          shouldDrawButtonAsDown);
        return;
    }

    juce::ignoreUnused (shouldDrawButtonAsDown);

    // A pill with a travelling dot, which is what the design draws wherever a switch
    // appears. JUCE's tick box would be the only square-cornered, unfilled control
    // in the window.
    const auto bounds = button.getLocalBounds().toFloat();
    const bool on = button.getToggleState();

    const float height = juce::jmin (bounds.getHeight(), 20.0f);
    const float width = height * 2.8f;
    const bool labelled = button.getButtonText().isNotEmpty();

    const auto pill = juce::Rectangle<float> (width, height)
                          .withPosition (labelled ? bounds.getX() : bounds.getCentreX() - width * 0.5f,
                                         bounds.getCentreY() - height * 0.5f);

    g.setColour (on ? Theme::teal() : Theme::surfaceBright());
    g.fillRoundedRectangle (pill, height * 0.5f);

    if (shouldDrawButtonAsHighlighted)
    {
        g.setColour (Theme::text().withAlpha (0.12f));
        g.fillRoundedRectangle (pill, height * 0.5f);
    }

    // The dot is the throw, and it sits on the side that says which one is made.
    const float inset = 1.5f;
    const float dot = height - inset * 2.0f;
    const float travel = pill.getWidth() - dot - inset * 2.0f;
    const float dotX = pill.getX() + inset + (on ? travel : 0.0f);

    g.setColour (Theme::text());
    g.fillEllipse (juce::Rectangle<float> (dot, dot).withPosition (dotX, pill.getY() + inset));

    if (labelled)
    {
        g.setColour (button.findColour (juce::ToggleButton::textColourId));
        g.setFont (Fonts::light (13.0f));
        g.drawText (button.getButtonText(), bounds.withTrimmedLeft (pill.getWidth() + 8.0f),
                    juce::Justification::centredLeft, true);
    }
}

void LookAndFeelBase::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                             const juce::Colour& backgroundColour,
                                             bool shouldDrawButtonAsHighlighted,
                                             bool shouldDrawButtonAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (Theme::borderWidth * 0.5f);

    auto fill = backgroundColour;

    if (shouldDrawButtonAsDown || shouldDrawButtonAsHighlighted)
        fill = fill.overlaidWith (Theme::text().withAlpha (shouldDrawButtonAsDown ? 0.16f : 0.08f));

    if (! button.isEnabled())
        fill = fill.withMultipliedAlpha (0.5f);

    // Fill only. Every button this draws is filled with a colour that already
    // separates it from what it stands on, so a rule around it drew a second edge
    // where one was doing the job. Note that this reaches the editor's buttons only:
    // a dialog is a desktop window and inherits no look and feel from the editor, and
    // the toolbar's icons are IconButtons, which paint themselves.
    g.setColour (fill);
    g.fillRoundedRectangle (bounds, Theme::cornerRadius);
}

void LookAndFeelBase::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto font = getTextButtonFont (button, button.getHeight());
    g.setFont (font);
    g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                            : juce::TextButton::textColourOffId)
                     .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));

    // Room for the rounded ends, then the same lift the dropdowns get.
    const int margin = juce::jmin (6, button.getWidth() / 6);

    g.drawFittedText (button.getButtonText(),
                      button.getLocalBounds().reduced (margin, 0).translated (0, -riseFor (font)),
                      juce::Justification::centred, 2);
}

//==============================================================================
void LookAndFeelBase::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                     int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height)
                            .reduced (Theme::borderWidth * 0.5f);

    const auto outline = box.findColour (juce::ComboBox::outlineColourId);

    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, Theme::cornerRadius);

    // With no border to darken, the open state is shown by this wash alone, so it is
    // worth a little more than it was when it only had to tint an outlined box.
    if (isButtonDown)
    {
        g.setColour (outline.withAlpha (0.22f));
        g.fillRoundedRectangle (bounds, Theme::cornerRadius);
    }

    // Unoutlined, like the buttons: the phase box stands beside Export on the same
    // panel and in the same purple, and a rule on one of the pair and not the other
    // read as an oversight rather than a distinction.
    //
    // A chevron, drawn rather than JUCE's filled triangle: the design's arrow is two
    // strokes, and it has to match the weight of the border it sits in.
    const auto centre = juce::Point<float> (bounds.getRight() - comboArrowInset, bounds.getCentreY());
    constexpr float reach = comboArrowReach;

    juce::Path chevron;
    chevron.startNewSubPath (centre.x - reach, centre.y - reach * 0.55f);
    chevron.lineTo (centre.x, centre.y + reach * 0.55f);
    chevron.lineTo (centre.x + reach, centre.y - reach * 0.55f);

    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.strokePath (chevron, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

//==============================================================================
void LookAndFeelBase::fillTextEditorBackground (juce::Graphics& g, int width, int height,
                                                  juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, Theme::cornerRadius);
}

void LookAndFeelBase::drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                               juce::TextEditor& editor)
{
    if (! editor.isEnabled())
        return;

    const auto focused = editor.hasKeyboardFocus (true) && ! editor.isReadOnly();
    const auto colour = editor.findColour (focused ? juce::TextEditor::focusedOutlineColourId
                                                   : juce::TextEditor::outlineColourId);

    if (colour.isTransparent())
        return;

    const auto weight = focused ? Theme::borderWidth * 1.5f : Theme::borderWidth;
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height)
                            .reduced (weight * 0.5f);

    g.setColour (colour);
    g.drawRoundedRectangle (bounds, Theme::cornerRadius, weight);
}

//==============================================================================
void LookAndFeelBase::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);

    g.setColour (Theme::surface());
    g.fillRoundedRectangle (bounds, Theme::cornerRadius);
}

void LookAndFeelBase::drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int width, int height,
                                                            const juce::PopupMenu::Options& options)
{
    juce::ignoreUnused (options);

    drawPopupMenuBackground (g, width, height);
}

std::unique_ptr<juce::DropShadower> LookAndFeelBase::createDropShadowerForComponent (juce::Component&)
{
    // Nothing on any platform, and nothing lost on the Mac: the macOS peer turns
    // windowHasDropShadow into an NSWindow shadow and never asks for one of these, so
    // menus there keep the native shadow that already rounds itself against the alpha.
    //
    // The Windows peer does ask, because it cannot use the native shadow on a window
    // that is semi-transparent and temporary -- which every menu here is. What it would
    // build is a rectangle, and a rectangle behind a rounded panel is a dark wedge in
    // each of the four corners. An honest absence beats that.
    //
    // Callout boxes are unaffected: drawCallOutBoxBackground draws its own shadow into
    // the bubble, and the tooltip window is parented to the editor rather than put on
    // the desktop, so neither was getting one of these anyway.
    return nullptr;
}

int LookAndFeelBase::getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options& options)
{
    juce::ignoreUnused (options);

    // Vertical only, whatever the name says. JUCE lays every row out at x = 0 across
    // the full window width and adds this border twice to that width -- so it insets
    // the first and last rows from the top and bottom, and does nothing at all to the
    // sides. The horizontal gap is drawPopupMenuItem's own inset, and the two have to
    // be read together or the menu comes out padded more at the top than beside.
    //
    // Five here plus the one that inset takes vertically is six, which is what the six
    // it takes horizontally comes to. See drawPopupMenuItem.
    return 5;
}

void LookAndFeelBase::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator,
                                                   int standardMenuItemHeight,
                                                   int& idealWidth, int& idealHeight)
{
    if (isSeparator)
    {
        idealHeight = 7;
        idealWidth = 50;
        return;
    }

    const auto font = getPopupMenuFont();

    // Room around the word rather than against it. JUCE's default leaves a column for
    // tick marks on the left of every row whether anything is ticked or not, which on
    // a list of four short strings is most of the width.
    idealHeight = juce::jmax (standardMenuItemHeight, juce::roundToInt (font.getHeight() * 1.9f));
    idealWidth = juce::GlyphArrangement::getStringWidthInt (font, text) + 34;
}

void LookAndFeelBase::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                           bool isSeparator, bool isActive, bool isHighlighted,
                                           bool isTicked, bool hasSubMenu,
                                           const juce::String& text,
                                           const juce::String& shortcutKeyText,
                                           const juce::Drawable* icon,
                                           const juce::Colour* textColour)
{
    juce::ignoreUnused (hasSubMenu, icon, textColour);

    if (isSeparator)
    {
        const auto line = area.toFloat().reduced (10.0f, 0.0f).withSizeKeepingCentre (
            (float) area.getWidth() - 20.0f, 1.0f);

        g.setColour (Theme::line().withAlpha (0.2f));
        g.fillRect (line);
        return;
    }

    // Six a side, one top and bottom. The six matches the five the menu's own border
    // adds above the first row and below the last, so the gap round the outside is the
    // same all the way round -- see getPopupMenuBorderSizeWithOptions, which is
    // vertical only and is the other half of this number. The one is what separates
    // one row from the next.
    const auto bounds = area.toFloat().reduced (6.0f, 1.0f);

    // The row that is already chosen wears the accent; the one under the pointer wears
    // a wash of it. Both are shapes rather than full-width stripes, so the list reads
    // as a stack of things you can press.
    if (isTicked)
    {
        g.setColour (Theme::accent().withAlpha (isHighlighted ? 0.95f : 0.75f));
        g.fillRoundedRectangle (bounds, Theme::cornerRadius - 2.0f);
    }
    else if (isHighlighted && isActive)
    {
        g.setColour (Theme::surfaceBright());
        g.fillRoundedRectangle (bounds, Theme::cornerRadius - 2.0f);
    }

    const auto font = getPopupMenuFont();

    g.setColour (! isActive  ? Theme::textDisabled()
                 : isTicked  ? Theme::text()
                             : Theme::textDim());
    g.setFont (font);

    // No tick glyph. The filled row says which one is chosen, and a tick beside it
    // would be the same fact twice at the cost of the indent every row then needs.
    g.drawText (text, bounds.reduced (12.0f, 0.0f).translated (0.0f, (float) -riseFor (font)),
                juce::Justification::centredLeft, true);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (Theme::comment());
        g.setFont (font.withHeight (font.getHeight() * 0.85f));
        g.drawText (shortcutKeyText, bounds.reduced (12.0f, 0.0f),
                    juce::Justification::centredRight, true);
    }
}

juce::PopupMenu::Options LookAndFeelBase::getOptionsForComboBoxPopupMenu (juce::ComboBox& box,
                                                                            juce::Label& label)
{
    return juce::PopupMenu::Options()
        .withTargetComponent (&box)
        .withItemThatMustBeVisible (box.getSelectedId())
        .withInitiallySelectedItem (box.getSelectedId())
        .withMinimumWidth (box.getWidth())
        .withMaximumNumColumns (1)
        .withStandardItemHeight (label.getHeight());
}

//==============================================================================
//==============================================================================
namespace
{
    /** A tooltip's text, laid out in the house face. The width is what the caller has
        to measure before it can decide how big the window should be, so the layout is
        built once here and used for both. */
    juce::TextLayout layoutTooltip (const juce::String& text, float maximumWidth)
    {
        juce::AttributedString attributed;
        attributed.setWordWrap (juce::AttributedString::WordWrap::byWord);
        attributed.setJustification (juce::Justification::centredLeft);
        attributed.append (text, Fonts::light (12.5f), Theme::text());

        juce::TextLayout layout;
        layout.createLayoutWithBalancedLineLengths (attributed, maximumWidth);

        return layout;
    }

    constexpr float tooltipPaddingX = 10.0f;
    constexpr float tooltipPaddingY = 7.0f;
    constexpr float tooltipMaximumWidth = 320.0f;
}

void LookAndFeelBase::drawTooltip (juce::Graphics& g, const juce::String& text,
                                     int width, int height)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);

    // Fill only. The border V4 draws is the thing that made a tooltip read as a system
    // window sitting on top of the plugin rather than as part of it.
    g.setColour (Theme::surface());
    g.fillRoundedRectangle (bounds, Theme::cornerRadius);

    layoutTooltip (text, (float) width - tooltipPaddingX * 2.0f)
        .draw (g, bounds.reduced (tooltipPaddingX, tooltipPaddingY));
}

juce::Rectangle<int> LookAndFeelBase::getTooltipBounds (const juce::String& tipText,
                                                          juce::Point<int> screenPosition,
                                                          juce::Rectangle<int> parentArea)
{
    // Measured with the same layout that draws it, or the box is sized for one font
    // and filled with another.
    const auto layout = layoutTooltip (tipText, tooltipMaximumWidth);

    const auto width = (int) std::ceil (layout.getWidth() + tooltipPaddingX * 2.0f);
    const auto height = (int) std::ceil (layout.getHeight() + tooltipPaddingY * 2.0f);

    // Below and to the right of the pointer, unless that would take it off the edge.
    return juce::Rectangle<int> (screenPosition.x > parentArea.getCentreX() ? screenPosition.x - (width + 12)
                                                                           : screenPosition.x + 18,
                                 screenPosition.y > parentArea.getCentreY() ? screenPosition.y - (height + 6)
                                                                           : screenPosition.y + 18,
                                 width, height)
        .constrainedWithin (parentArea);
}

//==============================================================================
void LookAndFeelBase::drawCallOutBoxBackground (juce::CallOutBox& box, juce::Graphics& g,
                                                const juce::Path& path, juce::Image& cachedImage)
{
    // The shadow is cached because it is a blur over the whole bubble and the bubble
    // does not change shape once it is up — this is JUCE's own arrangement for it, and
    // the reason the image is passed in by reference.
    if (cachedImage.isNull())
    {
        cachedImage = { juce::Image::ARGB, box.getWidth(), box.getHeight(), true };
        juce::Graphics shadow (cachedImage);
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 9, { 0, 3 }).drawForPath (shadow, path);
    }

    g.setColour (juce::Colours::black);
    g.drawImageAt (cachedImage, 0, 0);

    // The surround the graph and the faders stand on, which is the darkest thing in
    // the window. Chrome was the obvious choice for something floating above the
    // window, but chrome is the aubergine of the toolbar and read as purple rather
    // than as dark; this reads as a hole cut through to the same ground the rest of
    // the plugin sits on.
    g.setColour (Theme::consoleBackground());
    g.fillPath (path);

    g.setColour (Theme::line().withAlpha (0.2f));
    g.strokePath (path, juce::PathStrokeType (Theme::borderWidth));
}

void LookAndFeelBase::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    // The text is centred in the run from the left edge to where the chevron starts,
    // not in the box. Mirroring the chevron's inset on the left instead centres it in
    // the whole box, which sounds right and looks wrong: the chevron is then the only
    // thing in the right margin, so "Minimum" sat 28px from the left border and 8px
    // from the arrow. Balancing it against what is actually beside it puts equal air
    // on both sides of the word.
    label.setBounds (0, 1 - riseFor (getComboBoxFont (box)),
                     juce::roundToInt ((float) box.getWidth() - comboArrowInset - comboArrowReach),
                     box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centred);
}

//==============================================================================
juce::Font LookAndFeelBase::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    if (button.getProperties().contains (largeGlyphProperty))
        return Fonts::light ((float) buttonHeight * 0.78f);

    return Fonts::light (juce::jmin (16.0f, (float) buttonHeight * 0.5f));
}

juce::Font LookAndFeelBase::getComboBoxFont (juce::ComboBox& box)
{
    return Fonts::light (juce::jmin (15.0f, (float) box.getHeight() * 0.55f));
}

juce::Font LookAndFeelBase::getPopupMenuFont() { return Fonts::light (15.0f); }

void LookAndFeelBase::drawPopupMenuSectionHeader (juce::Graphics& g,
                                                   const juce::Rectangle<int>& area,
                                                   const juce::String& sectionName)
{
    g.setFont (Fonts::bold (13.0f));
    g.setColour (findColour (juce::PopupMenu::headerTextColourId));

    auto r = area.reduced (1);
    r.reduce (juce::jmin (5, area.getWidth() / 20), 0);
    r.removeFromRight (3);

    g.drawFittedText (sectionName, r, juce::Justification::centredLeft, 1);
}

std::unique_ptr<juce::FocusOutline> LookAndFeelBase::createFocusOutlineForComponent (juce::Component&)
{
    // Explicitly handled by Desktop::updateFocusOutline, which checks for null.
    return nullptr;
}

juce::Font LookAndFeelBase::getLabelFont (juce::Label& label)
{
    // A label that has asked for a particular face keeps it.
    if (label.getProperties().contains (keepFontProperty))
        return label.getFont();

    // Otherwise the label keeps the size it was given and gets the design's face
    // whatever it asked for. Blunt on purpose: this is the net under every label in
    // the window, including the ones JUCE makes for itself inside a combo box or a
    // slider.
    return Fonts::light (label.getFont().getHeight());
}

juce::Font LookAndFeelBase::getAlertWindowTitleFont() { return Fonts::bold (17.0f); }
juce::Font LookAndFeelBase::getAlertWindowMessageFont() { return Fonts::light (15.0f); }

juce::Label* LookAndFeelBase::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);

    label->setFont (Fonts::light (13.0f));
    label->setJustificationType (juce::Justification::centred);

    // Set on the label rather than the look and feel: the first text box is built
    // while the slider still has the default look and feel attached.
    label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::textColourId, Theme::text());

    // Editing a value draws no box round it, like every other field here. This drew one
    // in the armed colour, which was the last rule left anywhere in the window -- and it
    // appeared on a click, which is the thing that made it look like a system control.
    label->setColour (juce::Label::backgroundWhenEditingColourId, juce::Colours::transparentBlack);
    label->setColour (juce::Label::outlineWhenEditingColourId, juce::Colours::transparentBlack);

    // JUCE fills this one from its own colour scheme rather than leaving it unset, so
    // without saying otherwise the digits you are typing are not the theme's ink.
    label->setColour (juce::Label::textWhenEditingColourId, Theme::text());

    return label;
}
