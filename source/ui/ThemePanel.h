#pragma once

#include "PluginLookAndFeel.h"
#include "ThemePalette.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>   // the colour picker

#include <memory>
#include <vector>

namespace Celine
{
    //==========================================================================
    /**
        The theme editor: every colour the interface uses, laid out in the groups
        `ThemeRoles.h` puts them in, and a file to carry them between machines.

        Live rather than previewed. A colour changed here reaches the window behind this
        one on the next repaint, because the palette is what everything draws from --
        which means the honest way to judge a colour is to watch the plugin change while
        you drag, and there is no Apply button to forget to press.

        Laid out as a list rather than a grid: the labels are what somebody scans for,
        and a grid of swatches is a colour hunt.
    */
    class ThemePanel : public juce::Component,
                       private juce::ChangeListener
    {
    public:
        ThemePanel();
        ~ThemePanel() override;

        /** Below this the group headings and the footer start crowding each other. */
        enum { minimumWidth = 460, minimumHeight = 520 };

        void paint (juce::Graphics&) override;
        void resized() override;

        juce::TextButton close { "Close" };

    private:
        //======================================================================
        /** One colour: what it is called, the swatch that opens a picker, and the hex
            somebody can read off or paste into. */
        class Row : public juce::Component
        {
        public:
            Row (Theme::Role, ThemePanel&);

            void paint (juce::Graphics&) override;
            void resized() override;
            void mouseDown (const juce::MouseEvent&) override;

            /** Pulls the swatch and the field back out of the palette, for when the
                colour moved somewhere other than this row -- a file being loaded, or a
                reset. */
            void refresh();

        private:
            void applyTypedText();

            Theme::Role role;
            ThemePanel& owner;

            juce::Label name;
            juce::TextEditor hex;
            juce::Rectangle<int> swatch;
        };

        friend class Row;

        void openPickerFor (Theme::Role, juce::Rectangle<int> swatchOnScreen);

        /** Which row's swatch the open picker belongs to, and the box holding it. Only
            one is ever open, so one of each is enough -- and the box is dismissed when
            this panel goes, or the picker would outlive the listener it reports to. */
        Theme::Role picking = Theme::Role::count;
        juce::Component::SafePointer<juce::CallOutBox> picker;

        void importTheme();
        void exportTheme();
    /** Writes the colours to this plugin's theme file. Nothing else does. */
    void saveTheme();
    void resetTheme();

        void refreshRows();
        void changeListenerCallback (juce::ChangeBroadcaster*) override;

        /** Says what went wrong with a file, where the person is looking rather than in
            a modal box over the whole window. */
        void report (const juce::String& message);

        juce::Label title, subtitle;

        juce::Viewport scroller;
        juce::Component rows;

        std::vector<std::unique_ptr<Row>> colourRows;

        /** The group headings, drawn by the panel rather than being components of their
            own: they carry no behaviour, and a Label apiece is a hundred components
            for a hundred words. */
        struct Heading { juce::String text; juce::Rectangle<int> bounds; };
        std::vector<Heading> headings;

        juce::TextButton importButton { juce::String::fromUTF8 ("Import\xe2\x80\xa6") };
        juce::TextButton exportButton { juce::String::fromUTF8 ("Export\xe2\x80\xa6") };
        juce::TextButton saveButton { "Save" };
        juce::TextButton resetButton { "Reset" };

        juce::Label status;

        std::unique_ptr<juce::FileChooser> chooser;

        /** The panel's own, like the About window's: this lives in a DialogWindow, which
            has no parent to inherit one from. */
        PluginLookAndFeel lookAndFeel;

        /** True while this panel is the one writing to the palette, so its own change
            message does not send it round rebuilding the rows it has just edited. */
        bool editing = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThemePanel)
    };

    /** Opens the theme editor as a resizable, self-deleting dialog. */
    void showThemeWindow (juce::Component* associatedComponent);
} // namespace Celine
