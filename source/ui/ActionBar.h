#pragma once

#include <CelineUI/Theme.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace Celine
{
    /** The actions on the selection, floating near it: what a right click brings up, in
        place of rotate, mirror, flip, duplicate and delete buttons in the toolbar. Only for
        a selection holding a part that turns: wires, group boxes and notes have none.

        Three blocks, by what they do, with no panel behind them: turning the part (rotate,
        mirror and flip, one segmented block), copying it, and deleting it -- the last with a
        red icon, the one action that cannot be turned back by doing it again. White icons on
        the console's near-black, rounded squares, a soft shadow under each block. They sit
        clear of the selection (the editor places them above what is selected, never on it),
        so the part you are working on stays in view. They stay up while used -- turning a
        part twice is two clicks -- and go when the selection empties, the sheet moves, or
        anything else is clicked. The row itself takes no clicks: the sheet between the
        blocks is still the sheet. */
    class ActionBar : public juce::Component
    {
    public:
        ActionBar();

        std::function<void()> onRotate, onMirror, onFlip, onDuplicate, onDelete;

        /** Room round the blocks for their shadows; the component's size includes it. */
        static constexpr int shadow = 10;

        static constexpr int buttonSize = 38, gap = 10;
        static constexpr float cornerRadius = 10.0f;
        static constexpr int height = buttonSize + 2 * shadow;

        int getPreferredWidth() const;

        void paint (juce::Graphics&) override;
        void resized() override;
        void lookAndFeelChanged() override;

    private:
        /** One action. On its own it is a near-black rounded square; inside a block it draws
            only its icon and its hover, the block being drawn behind it. */
        class ActionButton : public juce::Button
        {
        public:
            ActionButton (const juce::String& name, const juce::String& fileName, bool isDestructive, bool isGrouped);
            void paintButton (juce::Graphics&, bool highlighted, bool down) override;
            void refreshIcon();

        private:
            juce::String file;
            bool destructive, grouped;
            std::unique_ptr<juce::Drawable> icon;
        };

        /** The turning block, round its three buttons. */
        juce::Rectangle<float> turnBlock() const;

        ActionButton rotate { "Rotate a quarter turn (Ctrl/Cmd-R)", "rotate.svg", false, true };
        ActionButton mirror { "Mirror left to right (Ctrl/Cmd-F)", "mirror.svg", false, true };
        ActionButton flip { "Flip top to bottom", "flip.svg", false, true };
        ActionButton duplicate { "Duplicate", "copy.svg", false, false };
        ActionButton remove { "Delete the selection (Delete key)", "trash-can-solid-full.svg", true, false };
    };
} // namespace Celine
