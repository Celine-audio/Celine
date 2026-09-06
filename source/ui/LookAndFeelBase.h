#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** Marks a ToggleButton as one of ours, so it draws as the design's pill rather
    than JUCE's tick box. A property rather than a subclass because the look and
    feel is the only thing that cares. */
inline constexpr const char* pillSwitchProperty = "celinePillSwitch";

/** Marks a Label as keeping the font it was given.

    getLabelFont is otherwise a net under every label in the window, forcing the
    design's face on all of them — which is what stops JUCE's own labels, the ones
    inside a combo box or a slider, from coming out in the platform sans. The cost
    is that it also overrode a face chosen on purpose: the wordmark's font, set on
    the fader names, was being replaced at paint time and the setter looked as
    though it had done nothing. */
inline constexpr const char* keepFontProperty = "celineKeepFont";

/** Marks a TextButton whose label is a single glyph rather than a word.

    A word wants to sit comfortably inside its button; a lone × or arrow wants to fill
    it, or it reads as a speck in a large target. The two cannot share one rule, and
    which of them a button is, only the button knows. */
inline constexpr const char* largeGlyphProperty = "celineLargeGlyph";

/**
    Applies Celine's palette and typeface to everything JUCE draws for us.

    Two jobs, the same two Celine's own look and feel has. The first is colour:
    sliders, combo boxes, popup menus and the file dialogs are drawn by the
    LookAndFeel and not by us, so without this half the window is the design and
    the other half is JUCE's default grey.

    The second is the two controls the design draws its own way — the knob, which
    is `knob.svg` rotated by the value rather than an arc and a pointer, and the
    switch, a pill with a travelling dot.

    Lives in a .cpp, unlike the palette it reads, because it needs the artwork out
    of BinaryData, and Theme.h is included nearly everywhere.
*/
class LookAndFeelBase : public juce::LookAndFeel_V4
{
public:
    LookAndFeelBase();
    ~LookAndFeelBase() override;

    /** Re-reads every colour out of the theme.

        Its own function because it has to run twice: once at construction, and again
        whenever the palette moves. Everything JUCE draws for us is told its colours
        rather than asked for them, so a theme change that did not come back through
        here would repaint the window in half the new colours and half the old. */
    virtual void applyPalette();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    static void drawSliderGroove (juce::Graphics&, juce::Rectangle<float> track,
                                  float radius, juce::Slider&);
    static void drawSliderFill (juce::Graphics&, juce::Rectangle<float> filled,
                                float radius, juce::Slider&);
    static void drawHorizontalSlider (juce::Graphics&, juce::Rectangle<float> bounds,
                                      float sliderPos, juce::Slider&);
    static void drawVerticalSlider (juce::Graphics&, juce::Rectangle<float> bounds,
                                    float sliderPos, juce::Slider&);

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    /** Centred optically rather than geometrically — see opticalRise in the .cpp
        for why the two are not the same in Jura. */
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    /** A text field, rounded at the same radius as every button and dropdown beside
        it. V4 fills and outlines a bare rectangle, which reads as a system control
        dropped into the window. */
    void fillTextEditorBackground (juce::Graphics&, int width, int height,
                                   juce::TextEditor&) override;

    void drawTextEditorOutline (juce::Graphics&, int width, int height,
                                juce::TextEditor&) override;

    //==========================================================================
    /** The list a dropdown opens.

        JUCE's own is a grey panel with a hairline border, a column of space reserved
        for tick marks whether or not anything is ticked, and rows tight enough that
        the highlight is a stripe rather than a shape. Ours is the same dark slate the
        button was, with room around the words and the selected row filled in the
        accent -- so opening a list looks like the button growing rather than like a
        system menu arriving on top of it. */
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

    /** Room inside the rounded corners, so the top and bottom rows are not clipped by
        them. */
    int getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&) override;

    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText,
                            const juce::Drawable* icon,
                            const juce::Colour* textColour) override;

    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator,
                                    int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;

    /** The list lines up under its button rather than being centred on it, and is at
        least as wide -- a two-word list popping up narrower than the control it came
        from reads as a different control. */
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox&,
                                                             juce::Label&) override;

    /** The one JUCE actually calls. Overridden so nothing further up the chain can
        add a rule of its own: LookAndFeel_V2's version -- which is what this would
        otherwise fall through to -- draws one on every platform but macOS. */
    void drawPopupMenuBackgroundWithOptions (juce::Graphics&, int width, int height,
                                             const juce::PopupMenu::Options&) override;

    //==========================================================================
    /** A tooltip, in the house face and without a rule around it.

        Both need saying explicitly. LookAndFeel_V4 draws an outline whatever colour
        it is given, and lays the text out with `FontOptions (13.0f, Font::bold)` --
        a font carrying no typeface, which is resolved at render time against the
        *default* look and feel rather than this one. So every tooltip in the plugin
        came out in the platform sans inside a bordered box, which is the one place
        the window stopped looking like itself. */
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText,
                                           juce::Point<int> screenPosition,
                                           juce::Rectangle<int> parentArea) override;

    /** The bubble the Export options open in. Without this it is JUCE's default,
        which is a light grey panel with a light border — the one bright rectangle
        in a dark window, and it read as a system dialog rather than as part of
        the plugin. */
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&,
                                   const juce::Path&, juce::Image&) override;

    //==========================================================================
    /** Jura, for everything JUCE picks a font for itself.

        These matter more than they look like they should. A font built from
        FontOptions(height) carries no typeface, so it is resolved at render time
        against the *default* LookAndFeel — not the one the component is using. */
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    /** No focus ring, on anything.

        JUCE draws one as a separate desktop window around whatever has keyboard focus,
        and its default is a yellow rounded rectangle at a fixed radius of three -- so on
        a field rounded to the house radius it traced a shape the control does not have,
        floating slightly off its corners. It also survives only as long as that window
        does, which is why it came back on one launch and not the next.

        Nothing here needs it: a field being edited already says so through its caret and
        its selection, and every other control shows focus by being the thing under the
        pointer. */
    std::unique_ptr<juce::FocusOutline> createFocusOutlineForComponent (juce::Component&) override;

    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;

    /** A menu's section headers. Overridden because JUCE draws them as
        getPopupMenuFont().boldened(), and boldening is the one thing this font
        cannot be asked for: Jura-Light has no bold face to find, so the request
        falls off Jura altogether. */
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area,
                                     const juce::String& sectionName) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LookAndFeelBase)
};
