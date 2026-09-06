#pragma once

#include "EmbeddedAssets.h"
#include "Fonts.h"
#include "IconButton.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace Celine
{
    //==========================================================================
    /**
        The preset control in the toolbar.

        A button that says what is loaded rather than what it does. "Presets"
        told you a menu was behind it and nothing else -- which patch you were
        hearing, whether you had edited it since, whether you had loaded one at
        all, none of that was anywhere on screen.

        So it reads as a dropdown: a small caption, the name of the loaded
        preset, and a chevron. The dot appears once the drawing no longer matches
        the file it came from.
    */
    class PresetButton : public juce::Button
    {
       public:
        PresetButton() : juce::Button ("Presets")
        {
            // The canvas needs the keyboard for its part and tool shortcuts, and
            // a toolbar button that takes focus on click silently kills them.
            setWantsKeyboardFocus (false);
        }

        /** The preset now loaded, or {} for none.

            `factory` picks the caption: a shipped example and a file in the
            user's folder are both presets, but only one of them can be
            overwritten by Save, so the button says which you are looking at. */
        void setPresetName (const juce::String& name, bool factory = false)
        {
            if (presetName == name && isFactory == factory)
                return;

            presetName = name;
            isFactory = factory;
            repaint();
        }

        bool isFactoryPreset() const noexcept { return isFactory; }

        /** Whether the sheet has been edited since that preset was loaded. */
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

            // Fill only. This is a button that opens a menu, and it is filled with the
            // same colour as the buttons either side of it -- a rule around it drew a
            // second edge where one was already doing the job.

            auto content = bounds.reduced (6.0f, 3.0f);

            // The dot goes right, where it does not shift the name about as it
            // comes and goes.
            if (loaded && modified)
            {
                auto dot = content.removeFromRight (12.0f);
                g.setColour (Theme::pending());
                g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot.getCentre()));
            }

            // Two lines, left aligned, both in the label weight -- the design
            // stacks the caption above the name rather than putting them side by
            // side, which is what lets the field be mostly empty and still read.
            const auto caption = content.removeFromTop (content.getHeight() * 0.5f);

            g.setFont (Fonts::light (11.0f));
            g.setColour (isFactory && loaded ? Theme::violet() : Theme::comment());
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

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetButton)
    };

    //==========================================================================

    //==========================================================================
    /**
        The bypass control: a power button that only speaks up when it has
        something to say.

        Running is the ordinary state, so it draws exactly like every other
        button in the row -- a toolbar where one button is permanently lit
        teaches you to stop seeing it. Bypassed is the state worth noticing,
        because the plugin is passing audio through untouched and that is
        usually not what you meant, so that is the one that goes red.

        Note the parameter is called *bypass*: getToggleState() is true when the
        plugin is doing nothing. Reading it the other way round is the kind of
        bug you stare straight through, so it is unpacked into a named bool.
    */
    class PowerButton : public IconButton
    {
       public:
        PowerButton (const juce::String& name, std::unique_ptr<juce::Drawable> drawable)
            : IconButton (name, std::move (drawable))
        {
            // The APVTS attachment drives the toggle state, and a click has to
            // move it or the attachment never sees anything.
            setClickingTogglesState (true);
        }

        /** Bypass is just an active button in a different colour, so it is drawn by
            the base class rather than here. It had its own inset, its own radius and
            its own border, so engaging bypass visibly reshaped the button -- a control
            that changes shape when you press it reads as a rendering fault. */
        void buttonStateChanged() override
        {
            setActive (getToggleState());
            IconButton::buttonStateChanged();
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PowerButton)
    };
} // namespace Celine
