#pragma once

#include "PluginLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

/**
    The About window: what this is, who wrote it, and under what terms.

    Inside a host this is the only route to the licence notice, so it carries the
    whole thing rather than a trimmed summary — a licence summary edited to fit a
    layout is a licence summary that has been changed.
*/
class AboutPanel : public juce::Component,
                   private juce::ChangeListener
{
public:
    /** Below this the footer's marks start overlapping the Close button. */
    enum { minimumWidth = 620, minimumHeight = 440 };

    AboutPanel();
    ~AboutPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::TextButton close { "Close" };

private:
    /** The colours this takes once rather than reading as it draws. See Theme.h. */
    void applyColours();
    void lookAndFeelChanged() override { applyColours(); }

    /** This is a desktop window of its own, so the editor's `sendLookAndFeelChange`
        never reaches it -- it has to hear about a theme change directly or it sits
        there on the old colours while the window behind it changes. */
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    // Its own, not the editor's. This panel lives in a DialogWindow, which is a
    // desktop window with no parent component, so it inherits nothing from the editor
    // and was drawing itself in JUCE's default grey.
    PluginLookAndFeel lookAndFeel;

    // The graph's ground, carrying everything below the masthead.
    juce::Rectangle<int> panelBounds;

    std::unique_ptr<juce::Drawable> vstMark, auMark, clapMark, lv2Mark;
    juce::Rectangle<int> vstBounds, auBounds, clapBounds, lv2Bounds;

    // The same pair the toolbar wears, drawn the same way: whose it is, then what it
    // is. The heading used to be the product name set as type, which said the right
    // words in the wrong voice.
    std::unique_ptr<juce::Drawable> logo, wordmark;
    juce::Rectangle<int> logoBounds, wordmarkBounds;

    juce::Label subtitle, version, wordmarkText;
    juce::TextEditor body;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutPanel)
};

/** Opens the About window as a resizable, self-deleting dialog. */
void showAboutWindow (juce::Component* associatedComponent);
