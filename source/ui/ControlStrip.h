#pragma once

#include "../PluginProcessor.h"

#include <functional>
#include <vector>

namespace Celine
{
    //==========================================================================
    /** A three-position lever for ON-OFF-ON and ON-ON-ON switches (two-position switches
        use a toggle). Click or drag to a stop. Unlit on the position that connects nothing. */
    class LeverSwitch : public juce::Component
    {
       public:
        /** The current stop, 0 to positions - 1; the top is throw A. */
        int getPosition() const noexcept { return position; }
        void setPosition(int newPosition, juce::NotificationType notification);

        /** The stop drawn unlit, or -1. */
        int offPosition = -1;

        std::function<void(int)> onChange;

        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;

        static constexpr int positions = 3;

       private:
        juce::Rectangle<float> trackBounds() const;
        int stopAt(float x) const;

        int position = positions - 1;
    };

    //==========================================================================
    /** The row of knobs and switches for the circuit's live controls, mapped onto the fixed
        `knob1..knobN` parameters.

        Positions flow both ways: a knob moved here or by automation is written back to the
        drawn parts (`onControlMoved`); a part edited on the sheet is pushed to its knob
        (`pushPositionFor`). The parameters are the live truth.

        Scrolls horizontally when the controls don't fit. */
    class ControlStrip : public juce::Component
    {
       public:
        explicit ControlStrip(PluginProcessor& processor);

        /** Rebuilds from the processor's current live controls. */
        void refresh();

        /** Pushes a part's position to the control that works it. */
        void pushPositionFor(int elementId);

        /** Updates the switch widgets from their parameters (they have no attachment). Called
            from the editor's timer. */
        void syncToggles();

        /** A control moved to `position`; these are the drawn parts it works. */
        std::function<void(const std::vector<int>& elementIds, float position)> onControlMoved;

        /** Height of the strip: caption, knob and value, plus room for the scrollbar. */
        static constexpr int preferredHeight = 92;

        /** Space between the bar's edge and its controls. */
        static constexpr int padding = 6;

        /** Width needed to show every control without scrolling. */
        int getPreferredWidth() const noexcept
        {
            return static_cast<int>(widgets.size()) * cellWidth + 2 * padding;
        }

        /** True when the circuit has no controls. */
        bool isEmpty() const noexcept { return widgets.empty(); }

        /** Called after a refresh, so the editor can resize the bar to fit. */
        std::function<void()> onContentChanged;

        /** Fixed cell width; the strip scrolls instead of squeezing. */
        static constexpr int cellWidth = 69;

        /** Knob cap size. */
        static constexpr int knobSize = 42;

        /** Height of the value under each knob. */
        static constexpr int valueHeight = 15;

        /** Shows an editable value under `knob`, written by `format` and read by `parse`.
            Call after attaching the parameter, which would otherwise replace these. */
        static void showValueBelow(juce::Slider& knob, std::function<juce::String(double)> format,
                                   std::function<double(const juce::String&)> parse);

        /** A pot's position as a percentage (the same as the inspector shows). */
        static juce::String formatPosition(double position);

        /** Signed gain in dB. */
        static juce::String formatDecibels(double decibels);

        /** Signed level in dBu, to two decimals. */
        static juce::String formatDbu(double dbu);

        /** Width of the channel picker. */
        static constexpr int comboWidth = 97;
        static constexpr int comboHeight = 27;

        //======================================================================
        /** Lays out a caption over a control, centred in `cell`, and returns the control's
            area. Shared with the editor so everything in the band shares one baseline. */
        static juce::Rectangle<int> layOutCell(juce::Rectangle<int> cell, juce::Label* label,
                                               int controlHeight);

        void paint(juce::Graphics&) override;
        void resized() override;

       private:
        /** One control: its widget, the parameter it drives and the parts it works. The ids
            let a refresh tell whether a slot changed hands. */
        struct Widget
        {
            std::unique_ptr<juce::Slider> slider;
            std::unique_ptr<juce::ToggleButton> toggle;
            std::unique_ptr<LeverSwitch> lever;
            std::unique_ptr<juce::Label> label;
            std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
            juce::RangedAudioParameter* parameter = nullptr;
            std::vector<int> elementIds;
        };

        PluginProcessor& processorRef;

        juce::Viewport viewport;
        juce::Component content;
        std::vector<Widget> widgets;

        /** Caption height. */
        static constexpr int labelHeight = 15;
        static constexpr int labelGap = 2;

        /** Height of a switch. */
        static constexpr int toggleHeight = 18;
        static constexpr int toggleWidth = 53;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlStrip)
    };
} // namespace Celine
