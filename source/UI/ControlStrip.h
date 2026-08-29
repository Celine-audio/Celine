#pragma once

#include "../PluginProcessor.h"

#include <functional>
#include <vector>

namespace SchematicUI
{
    //==========================================================================
    /**
        The row of knobs and switches the drawn circuit turned out to have.

        Built from the `LiveControl`s the last build produced, onto the fixed
        pool of `knob1..knobN` parameters -- parameters have to be declared at
        construction, but a schematic's controls come and go, so the drawn part
        and the automatable parameter are matched up here rather than being the
        same object.

        **Which way the positions flow** is the whole of what this has to get
        right, and it goes both ways:

        - a knob moved here, or moved by the host's automation, is written back
          onto every drawn part on that shaft (`onControlMoved`);
        - a part's position changed on the sheet -- retyped in the inspector --
          is pushed to the knob that works it (`pushPositionFor`).

        The parameters are the live truth -- what the host automates and what
        gets saved -- and the part remembers so a knob keeps its meaning when the
        slots shift under it. Backwards, and restoring an older session snaps
        every knob to noon.

        It scrolls rather than shrinking: a fixed cell width in a Viewport, so a
        sheet with more controls than fit is scrolled instead of squeezed.
    */
    class ControlStrip : public juce::Component
    {
       public:
        explicit ControlStrip(PluginProcessor& processor);

        /** Rebuilds from the processor's current live controls. */
        void refresh();

        /** Sheet to knob: a part's position changed, so the control that works
            it follows. Live, because a pot and a switch are the two things that
            take effect without a rebuild. */
        void pushPositionFor(int elementId);

        /** Keeps the switch buttons showing what their parameter actually says.
            They have no attachment to do it for them -- the pool is all float
            parameters, since it cannot know at construction which slots will
            turn out to be switches. Called from the editor's timer rather than
            standing up a second one. */
        void syncToggles();

        /** Knob to sheet: this control moved to `position`, and these are the
            drawn parts on its shaft. More than one when they gang. */
        std::function<void(const std::vector<int>& elementIds, float position)> onControlMoved;

        /** How tall the strip wants to be. */
        static constexpr int preferredHeight = 122;

        /** Fixed rather than divided out of the available width -- that is the
            whole point of scrolling. */
        static constexpr int cellWidth = 81;

        /** The knob cap, at the size the design draws it. */
        static constexpr int knobSize = 58;

        /** The channel picker, which is the one control in the band that is a
            dropdown rather than a knob. */
        static constexpr int comboWidth = 108;
        static constexpr int comboHeight = 34;

        //======================================================================
        /** Caption over control, the pair centred in `cell`, and the control's
            rectangle handed back.

            Shared with the editor -- INPUT, VOLUME and the channel picker are
            the editor's own children rather than the strip's, and the four
            things in the band have to sit on one baseline or the row reads as
            two rows that nearly line up. */
        static juce::Rectangle<int> layOutCell(juce::Rectangle<int> cell, juce::Label* label,
                                               int controlHeight);

        void paint(juce::Graphics&) override;
        void resized() override;

       private:
        /** One control: the widget, the parameter it drives, and the parts it
            works. The ids are kept so a refresh can tell a slot that changed
            hands from one that merely got rebuilt. */
        struct Widget
        {
            std::unique_ptr<juce::Slider> slider;
            std::unique_ptr<juce::ToggleButton> toggle;
            std::unique_ptr<juce::Label> label;
            std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
            juce::RangedAudioParameter* parameter = nullptr;
            std::vector<int> elementIds;
        };

        PluginProcessor& processorRef;

        juce::Viewport viewport;
        juce::Component content;
        std::vector<Widget> widgets;

        /** The caption, and the air between it and what it names. */
        static constexpr int labelHeight = 18;
        static constexpr int labelGap = 3;

        /** A switch's pill, which is shorter than a knob. */
        static constexpr int toggleHeight = 21;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlStrip)
    };
} // namespace SchematicUI
