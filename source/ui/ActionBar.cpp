#include "ActionBar.h"

#include <CelineUI/EmbeddedAssets.h>

namespace Celine
{
    ActionBar::ActionButton::ActionButton (const juce::String& name, const juce::String& fileName, bool isDestructive,
                                           bool isGrouped)
        : juce::Button (name), file (fileName), destructive (isDestructive), grouped (isGrouped)
    {
        setTooltip (name);
        setWantsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        refreshIcon();
    }

    void ActionBar::ActionButton::refreshIcon()
    {
        // Re-read and tinted afresh: tinting writes into the drawable (see Assets::tint).
        // Delete's icon is red, the theme's danger colour, as bypass's is.
        icon = Assets::drawable (file);
        if (icon != nullptr)
            Assets::tint (*icon, destructive ? Theme::danger() : Theme::headerText());
        repaint();
    }

    void ActionBar::ActionButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        const auto area = getLocalBounds().toFloat().reduced (0.5f);
        const auto ground = Theme::consoleBackground();

        // On its own: its own rounded square. In a block: the block is drawn behind.
        if (! grouped)
        {
            g.setColour (ground);
            g.fillRoundedRectangle (area, cornerRadius);
            g.setColour (Theme::headerText().withAlpha (0.1f));
            g.drawRoundedRectangle (area.reduced (0.5f), cornerRadius, 1.0f);
        }

        if (highlighted || down)
        {
            const auto hover = destructive ? Theme::danger().withAlpha (down ? 0.35f : 0.22f)
                                           : Theme::headerText().withAlpha (down ? 0.18f : 0.1f);
            g.setColour (hover);
            g.fillRoundedRectangle (grouped ? area.reduced (3.0f) : area, grouped ? cornerRadius - 3.0f : cornerRadius);
        }

        if (icon != nullptr)
            icon->drawWithin (g, area.reduced (area.getWidth() * 0.27f), juce::RectanglePlacement::centred, 1.0f);
    }

    //==========================================================================
    ActionBar::ActionBar()
    {
        rotate.onClick = [this] { if (onRotate) onRotate(); };
        mirror.onClick = [this] { if (onMirror) onMirror(); };
        flip.onClick = [this] { if (onFlip) onFlip(); };
        duplicate.onClick = [this] { if (onDuplicate) onDuplicate(); };
        remove.onClick = [this] { if (onDelete) onDelete(); };

        for (auto* b : { &rotate, &mirror, &flip, &duplicate, &remove })
            addAndMakeVisible (b);

        // Only the buttons take clicks; the sheet shows, and answers, between the blocks.
        setInterceptsMouseClicks (false, true);
        setOpaque (false);
    }

    int ActionBar::getPreferredWidth() const
    {
        return 2 * shadow + 5 * buttonSize + 2 * gap;
    }

    void ActionBar::resized()
    {
        auto row = getLocalBounds().reduced (shadow);

        // Turning, as one block: the three abut.
        for (auto* b : { &rotate, &mirror, &flip })
            b->setBounds (row.removeFromLeft (buttonSize));

        row.removeFromLeft (gap);
        duplicate.setBounds (row.removeFromLeft (buttonSize));
        row.removeFromLeft (gap);
        remove.setBounds (row.removeFromLeft (buttonSize));
    }

    juce::Rectangle<float> ActionBar::turnBlock() const
    {
        return rotate.getBounds().getUnion (flip.getBounds()).toFloat().reduced (0.5f);
    }

    void ActionBar::lookAndFeelChanged()
    {
        for (auto* b : { &rotate, &mirror, &flip, &duplicate, &remove })
            b->refreshIcon();
    }

    void ActionBar::paint (juce::Graphics& g)
    {
        // A shadow under each block, so they lift off the sheet.
        const juce::DropShadow drop (juce::Colours::black.withAlpha (0.4f), 9, { 0, 3 });

        for (const auto block : { turnBlock(), duplicate.getBounds().toFloat(), remove.getBounds().toFloat() })
        {
            juce::Path shape;
            shape.addRoundedRectangle (block, cornerRadius);
            drop.drawForPath (g, shape);
        }

        // The turning block's ground, with a hairline between its buttons.
        const auto block = turnBlock();
        g.setColour (Theme::consoleBackground());
        g.fillRoundedRectangle (block, cornerRadius);
        g.setColour (Theme::headerText().withAlpha (0.1f));
        g.drawRoundedRectangle (block.reduced (0.5f), cornerRadius, 1.0f);

        g.setColour (Theme::headerText().withAlpha (0.12f));
        for (auto* b : { &mirror, &flip })
            g.fillRect ((float) b->getX() - 0.5f, block.getY() + 9.0f, 1.0f, block.getHeight() - 18.0f);
    }
} // namespace Celine
