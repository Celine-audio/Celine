#include "CelineLookAndFeel.h"

#include "Fonts.h"

#include "EmbeddedAssets.h"

namespace SchematicUI
{

    //==========================================================================

    CelineLookAndFeel::CelineLookAndFeel()
    {
        using namespace Theme;

        if (auto face = Fonts::typeface (Fonts::Weight::Light))
        setDefaultSansSerifTypeface (face);
        setColour (juce::ResizableWindow::backgroundColourId, chrome());
        setColour (juce::DocumentWindow::textColourId, text());
        setColour (juce::TextButton::buttonColourId, surface());
        setColour (juce::TextButton::buttonOnColourId, surfaceBright());
        setColour (juce::TextButton::textColourOffId, text());
        setColour (juce::TextButton::textColourOnId, text());
        setColour (juce::ToggleButton::textColourId, text());
        setColour (juce::ToggleButton::tickColourId, teal());
        setColour (juce::ToggleButton::tickDisabledColourId, line());
        setColour (juce::Label::textColourId, text());
        setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::TextEditor::backgroundColourId, background());
        setColour (juce::TextEditor::textColourId, text());
        setColour (juce::TextEditor::highlightColourId, surfaceBright());
        setColour (juce::TextEditor::highlightedTextColourId, text());
        setColour (juce::TextEditor::outlineColourId, line());
        setColour (juce::TextEditor::focusedOutlineColourId, teal());
        setColour (juce::CaretComponent::caretColourId, text());
        setColour (juce::ComboBox::backgroundColourId, surface());
        setColour (juce::ComboBox::textColourId, text());
        setColour (juce::ComboBox::outlineColourId, line());
        setColour (juce::ComboBox::arrowColourId, textDim());
        setColour (juce::ComboBox::buttonColourId, surface());
        // A menu drops out of a toolbar button, so it belongs on the *dark* side
        // of the two-tone split -- which is exactly what surface() means when it
        // says "dropdowns". This was panel() while the ink stayed text(): near-
        // white on near-white, so a menu was readable only on the one row that
        // happened to be highlighted.
        setColour (juce::PopupMenu::backgroundColourId, surface());
        setColour (juce::PopupMenu::textColourId, text());
        setColour (juce::PopupMenu::headerTextColourId, comment());
        setColour (juce::PopupMenu::highlightedBackgroundColourId, surfaceBright());
        setColour (juce::PopupMenu::highlightedTextColourId, text());
        setColour (juce::Slider::rotarySliderFillColourId, teal());
        setColour (juce::Slider::rotarySliderOutlineColourId, surfaceBright());
        setColour (juce::Slider::thumbColourId, text());
        setColour (juce::Slider::textBoxTextColourId, text());
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, surfaceBright());
        setColour (juce::ScrollBar::thumbColourId, line());
        setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
        setColour (juce::TooltipWindow::backgroundColourId, surface());
        setColour (juce::TooltipWindow::textColourId, text());
        setColour (juce::TooltipWindow::outlineColourId, line());
        // The same pairing mistake as the menu, in the more visible place: the
        // first-run preset question is an AlertWindow, so this was the first
        // thing a new user saw. chrome() rather than surface() because it is a
        // window rather than a dropdown, matching ResizableWindow above.
        setColour (juce::AlertWindow::backgroundColourId, chrome());
        setColour (juce::AlertWindow::textColourId, text());
        setColour (juce::AlertWindow::outlineColourId, line());

        // JUCE's own Audio/MIDI settings dialog is the only place a ListBox
        // appears -- the MIDI input picker. Left unset it draws white on white
        // the same way the menu did.
        setColour (juce::ListBox::backgroundColourId, background());
        setColour (juce::ListBox::outlineColourId, line());
        setColour (juce::ListBox::textColourId, text());

        // The knob face. Tinted once, at load, rather than per frame: it is the
        // same colour every time it is drawn, and a drawable copy per knob per
        // repaint would be allocation on the paint path.
        knob = Assets::drawable("knob.svg");
        ioKnob = Assets::drawable("knob_io.svg");

        for (auto* face : { knob.get(), ioKnob.get() })
            if (face != nullptr)
                Assets::tint(*face, Theme::textOnPanel());
    }

    CelineLookAndFeel::~CelineLookAndFeel() = default;

    //==========================================================================

    void CelineLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
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

    void CelineLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
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

    //==========================================================================

    void CelineLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                                         int, int, int, int, juce::ComboBox& box)
    {
        const auto bounds =
            juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height))
                .reduced(Theme::borderWidth * 0.5f);

        const auto outline = box.findColour(juce::ComboBox::outlineColourId);

        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(bounds, Theme::cornerRadius);

        if (isButtonDown)
        {
            g.setColour(outline.withAlpha(0.15f));
            g.fillRoundedRectangle(bounds, Theme::cornerRadius);
        }

        g.setColour(outline);
        g.drawRoundedRectangle(bounds, Theme::cornerRadius, Theme::borderWidth * 1.5f);

        // A chevron, drawn rather than JUCE's filled triangle: the design's arrow
        // is two strokes, and it has to match the weight of the border it sits in.
        const auto centre = juce::Point<float>(bounds.getRight() - 16.0f, bounds.getCentreY());
        constexpr float reach = 4.5f;

        juce::Path chevron;
        chevron.startNewSubPath(centre.x - reach, centre.y - reach * 0.55f);
        chevron.lineTo(centre.x, centre.y + reach * 0.55f);
        chevron.lineTo(centre.x + reach, centre.y - reach * 0.55f);

        g.setColour(box.findColour(juce::ComboBox::arrowColourId));
        g.strokePath(chevron, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    namespace
    {
        /** How far text is lifted off the geometric centre, as a fraction of the
            font height.

            Not a fudge for a positioning bug -- measured, the cap block already
            centres to within half a pixel. Jura's ascenders reach cap height and
            UI strings here carry no descenders, so what JUCE centres is the
            block from cap top to baseline, exactly. What the *eye* centres on is
            the x-height mass, which sits in the lower half of that block, so
            arithmetically centred text reads low in every button and dropdown.

            This is the correction. Turn it down if text starts reading high;
            zero restores JUCE's own centring. */
        constexpr float opticalRise = 0.09f;

        int riseFor(const juce::Font& font) noexcept
        {
            return juce::roundToInt(font.getHeight() * opticalRise);
        }
    } // namespace

    void CelineLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                           bool, bool)
    {
        const auto font = getTextButtonFont(button, button.getHeight());
        g.setFont(font);
        g.setColour(button
                        .findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                            : juce::TextButton::textColourOffId)
                        .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));

        // Room for the rounded ends, then the same lift the dropdowns get.
        const int margin = juce::jmin(6, button.getWidth() / 6);

        g.drawFittedText(button.getButtonText(),
                         button.getLocalBounds().reduced(margin, 0).translated(0, -riseFor(font)),
                         juce::Justification::centred, 2);
    }

    void CelineLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
    {
        // Room for the chevron on the right, and the same on the left so the text
        // reads as centred in what is left rather than shoved against the border.
        // Lifted by the same fraction the buttons are, so a dropdown and a
        // button sitting side by side agree about where their middle is.
        label.setBounds(26, 1 - riseFor(getComboBoxFont(box)),
                        box.getWidth() - 52, box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
        label.setJustificationType(juce::Justification::centred);
    }

    //==========================================================================

    juce::Font CelineLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
    {
        return Fonts::light(juce::jmin(16.0f, static_cast<float>(buttonHeight) * 0.5f));
    }

    juce::Font CelineLookAndFeel::getComboBoxFont(juce::ComboBox& box)
    {
        return Fonts::light(juce::jmin(15.0f, static_cast<float>(box.getHeight()) * 0.55f));
    }

    juce::Font CelineLookAndFeel::getPopupMenuFont() { return Fonts::light(15.0f); }

    void CelineLookAndFeel::drawPopupMenuSectionHeader(juce::Graphics& g,
                                                       const juce::Rectangle<int>& area,
                                                       const juce::String& sectionName)
    {
        // Below the items it labels, which is the whole job: a header is read
        // once on the way past, and at the items' own size it competes with
        // them. 13 against their 15 -- see the comparison this was picked from.
        g.setFont(Fonts::bold(13.0f));
        g.setColour(findColour(juce::PopupMenu::headerTextColourId));

        // Flush with the menu's own left margin -- the same border and side
        // inset LookAndFeel_V4::drawPopupMenuItem starts from, and then no
        // further. That puts a header level with the *ticks* rather than with
        // the item text beyond them, which is what a section label wants: it
        // names the rows under it, so it belongs at the edge of the block, not
        // indented into it.
        //
        // Deliberately neither of the other two candidates. Indenting on past
        // the tick gutter, to where the item text begins, reads as though the
        // header were itself an item that had lost its tick. JUCE's own flat
        // 12 px lands between the two and lines up with nothing at all.
        auto r = area.reduced(1);
        r.reduce(juce::jmin(5, area.getWidth() / 20), 0);
        r.removeFromRight(3);

        // Centred, where JUCE bottom-aligns within four fifths of the row. At a
        // size below the items' that pins the text to the top of its row and
        // leaves the whole gap underneath, which is most of what reads as
        // crooked when a header and an item sit next to each other.
        g.drawFittedText(sectionName, r, juce::Justification::centredLeft, 1);
    }

    juce::Font CelineLookAndFeel::getLabelFont(juce::Label& label)
    {
        // The label keeps the *size* it was given and gets the design's face
        // whatever it asked for. Blunt on purpose: this is the net under every
        // label in the window, including the ones JUCE makes for itself inside a
        // combo box or a slider, and any of those left on the platform sans is
        // the "some of it is Jura and some of it isn't" bug all over again.
        return Fonts::light(label.getFont().getHeight());
    }

    juce::Font CelineLookAndFeel::getAlertWindowTitleFont() { return Fonts::bold(17.0f); }
    juce::Font CelineLookAndFeel::getAlertWindowMessageFont() { return Fonts::light(15.0f); }
} // namespace SchematicUI
