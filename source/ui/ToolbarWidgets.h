#pragma once

#include <CelineUI/EmbeddedAssets.h>
#include <CelineUI/Fonts.h>
#include <CelineUI/IconButton.h>
#include <CelineUI/Theme.h>

#include <juce_gui_basics/juce_gui_basics.h>

namespace Celine
{
    //==========================================================================
    /** The toolbar's preset field: a caption, the loaded preset's name and a chevron.
        A dot shows when the sheet differs from the loaded file. */
    class PresetButton : public juce::Button
    {
       public:
        PresetButton() : juce::Button ("Presets")
        {
            // Never take focus, or the canvas loses its keyboard shortcuts.
            setWantsKeyboardFocus (false);
        }

        /** The loaded preset, or {} for none. `factory` picks the caption. */
        void setPresetName (const juce::String& name, bool factory = false)
        {
            if (presetName == name && isFactory == factory)
                return;

            presetName = name;
            isFactory = factory;
            repaint();
        }

        bool isFactoryPreset() const noexcept { return isFactory; }

        /** Space kept free at the right end for the folder icon drawn over the field. */
        void setTrailingSpace (int pixels)
        {
            if (trailingSpace == pixels)
                return;

            trailingSpace = pixels;
            repaint();
        }

        /** Whether the sheet has been edited since the preset was loaded. */
        void setModified (bool nowModified)
        {
            if (modified == nowModified)
                return;

            modified = nowModified;
            repaint();
        }

        const juce::String& getPresetName() const noexcept { return presetName; }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced (Theme::borderWidth * 0.5f);
            const bool loaded = presetName.isNotEmpty();

            g.setColour (down || highlighted ? Theme::surfaceBright() : Theme::button());
            g.fillRoundedRectangle (bounds, Theme::cornerRadius);

            auto content = bounds.reduced (6.0f, 3.0f);
            content.removeFromRight ((float) trailingSpace);

            if (loaded && modified)
            {
                auto dot = content.removeFromRight (12.0f);
                g.setColour (Theme::pending());
                g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot.getCentre()));
            }

            // Caption above the name, left aligned.
            const auto caption = content.removeFromTop (content.getHeight() * 0.5f);

            g.setFont (Fonts::light (11.0f));
            g.setColour (isFactory && loaded ? Theme::accent() : Theme::comment());
            g.drawText (isFactory && loaded ? "FACTORY" : "PRESET", caption,
                        juce::Justification::bottomLeft, false);

            g.setColour (loaded ? Theme::textDim() : Theme::comment());
            g.drawText (loaded ? presetName.toUpperCase() : "NONE", content,
                        juce::Justification::topLeft, true);
        }

       private:
        juce::String presetName;
        bool isFactory = false;
        bool modified = false;
        int trailingSpace = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetButton)
    };

    //==========================================================================

    //==========================================================================
    /** The bypass button: drawn like the other buttons while running, red when bypassed.
        getToggleState() is true when bypassed. */
    class PowerButton : public IconButton
    {
       public:
        PowerButton (const juce::String& name, std::unique_ptr<juce::Drawable> drawable)
            : IconButton (name, std::move (drawable))
        {
            // Lets the parameter attachment see clicks.
            setClickingTogglesState (true);
        }

        /** Bypassed is drawn by the base class as an active button in another colour. */
        void buttonStateChanged() override
        {
            setActive (getToggleState());
            IconButton::buttonStateChanged();
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PowerButton)
    };
} // namespace Celine
