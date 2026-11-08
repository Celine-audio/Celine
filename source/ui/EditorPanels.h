#pragma once

#include "../Schematic/Schematic.h"
#include "../dsp/InputFilePlayer.h"
#include "../Schematic/SchematicBuilder.h"
#include "SchematicSymbols.h"

#include <functional>

namespace Celine
{
    //==========================================================================
    /** The parts palette, floating on the left of the sheet: two columns of entries, each
        drawing its symbol, in the pairs listed in `paletteOrder`. Names share one font
        size, the largest at which the longest fits. The tab on the right edge folds it to
        symbols only. Scrolls when the window is too short. */
    class ElementPalette : public juce::Component
    {
       public:
        ElementPalette();

        /** Defined in the .cpp, where Entry is complete. */
        ~ElementPalette() override;

        std::function<void(SchematicModel::ElementType)> onTypeChosen;

        /** The wire entry was clicked. */
        std::function<void()> onWireChosen;

        /** Lights the armed entry: a part, the wire, or neither. */
        void setActive(const SchematicModel::ElementType* type, bool wire);

        /** Symbols only, or symbols and names. */
        void setCollapsed(bool shouldBeCollapsed);
        bool isCollapsed() const noexcept { return collapsed; }

        /** The tab was clicked; the panel changed width. */
        std::function<void()> onCollapsedChanged;

        /** Preferred size, tab included, no taller than `maxHeight`. When it must scroll, the
            scrollbar's width is added. */
        juce::Point<int> getPreferredSize(int maxHeight = std::numeric_limits<int>::max()) const;

        /** Whether the list scrolls. */
        bool isScrolling() const;

        /** The widest the panel can be: unfolded, with its scrollbar. */
        int getMaximumWidth() const;

        /** The font size every name uses. */
        float getNameFontHeight() const noexcept { return nameFontHeight; }

        /** The panel without its tab. */
        juce::Rectangle<int> getBodyBounds() const;

        /** The folded panel's width (tab excluded), whether or not it is folded now. The
            bottom band's INPUT bar uses it. */
        static int getFoldedBodyWidth() noexcept;

        /** Only the panel and its tab take clicks. */
        bool hitTest(int x, int y) override;

        void resized() override;
        void paint(juce::Graphics&) override;

       private:
        class Entry;
        class CollapseTab;

        /** Each entry's area, in order, for the current mode. */
        std::vector<juce::Rectangle<int>> layOutGrid() const;

        /** Computes getNameFontHeight() from the names. */
        float fittingFontHeight() const;

        float nameFontHeight = 12.0f;

        int listWidth() const;
        int listHeight() const;

        bool collapsed = false;

        /** Holds the rows, so a short window scrolls them. */
        juce::Viewport viewport;
        juce::Component content;

        juce::OwnedArray<Entry> entries;
        std::unique_ptr<CollapseTab> tab;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ElementPalette)
    };

    //==========================================================================
    /** Edits the selected part's fields. Edits go straight into the Element, then fire a
        callback; the editor decides whether to rebuild. Values accept schematic notation
        ("4k7", "100n", "2.2M").

        A fixed-width card over the sheet, as tall as its contents. The editor caps its
        height; beyond that the fields scroll. */
    class ElementInspector : public juce::Component
    {
       public:
        ElementInspector();

        /** Shows a part, or nothing. */
        void setElement(SchematicModel::Element* element);

        /** Whether the card shows the shortcut list when nothing is selected. When off, the
            card hides itself while nothing is selected. */
        void setShortcutsShown(bool shouldShow);

        /** A live reading in engineering notation with exactly two decimals, so the width
            stays steady as the value changes. */
        static juce::String formatReading(double value, const juce::String& unit);

        /** Size that shows everything without scrolling. Width is always
            Theme::inspectorWidth. */
        juce::Point<int> getPreferredSize();

        /** The content changed in a way that may change the size. */
        std::function<void()> onPreferredSizeChanged;

        /** A field changed. The part is already updated. */
        std::function<void()> onEdited;

        /** A pot or switch position changed. These are live, so they don't need a rebuild. */
        std::function<void()> onControlEdited;

        /** Where a selected scope's trace comes from (same as the canvas). */
        ScopeReader scopeReader;

        /** The selected part's mean current, mean power and peak current. False when there is
            no reading (see PluginProcessor::readInspectedCurrent). */
        std::function<bool (float& current, float& power, float& peak)> currentReader;

        /** Tells the processor which part to measure. Called on every selection change. */
        std::function<void (int elementId)> onInspectedElementChanged;

        /** A scope's time base changed. The audio thread reads it; no rebuild needed. */
        std::function<void()> onScopeTimebaseChanged;

        /** Repaints only the live readouts (scope trace, current). Called from the editor's
            timer; cheap when nothing live is shown. */
        void repaintReadouts();

        /** The Output terminal's cabinet was switched or changed. No rebuild needed. */
        std::function<void()> onCabChanged;

        /** The user wants to pick an impulse response; the editor shows the dialog. */
        std::function<void()> onCabFileRequested;

        /** The input file player, driven directly by the Input panel. Null disables those
            controls. */
        InputFilePlayer* filePlayer = nullptr;

        /** The user wants to pick a file to play; the editor shows the dialog. */
        std::function<void()> onInputFileRequested;

        void resized() override;
        void paint(juce::Graphics&) override;

        /** Colours handed to children with setColour. Called again on every theme change. */
        void applyColours();
        void lookAndFeelChanged() override { applyColours(); }

       private:
        /** Every caption label. */
        std::array<juce::Label*, 15> captionLabels();

        void rebuildForElement();

        /** Lays the fields out again and reports a possible size change. */
        void contentChanged();

        /** Width of the field column, padding excluded. */
        int contentWidth() const;

        /** Height of the shortcut list: one line per text line. */
        int hintHeight() const;

        /** Width of the shortcut list's longest line. */
        int hintWidth() const;

        /** Shrinks the shortcut list's font until its longest line fits, scrollbar included. */
        void fitHintToWidth();

        void showNothingSelected();

        /** The line under the part's name: its model's description, or nothing. */
        static juce::String headerSubtitleFor(const SchematicModel::Element& part);

        /** Turns the value fields red while they are unset. */
        void markUnsetFields();
        void fillFieldsFrom(const SchematicModel::Element& part);
        void populateModelBox(const SchematicModel::Element& part);
        void showFieldsFor();
        /** Places the visible fields at `width` and returns their total height. */
        int layOutFields(int width);
        static juce::String valueTextFor(const SchematicModel::Element& e);
        static juce::String secondValueText(const SchematicModel::Element& e);
        void commitValue();
        void commitOrder();
        void commitKnob();
        void commitSize();

        SchematicModel::Element* element = nullptr;
        bool shortcutsShown = true;

        juce::Label valueCaption, labelCaption, modelCaption, taperCaption;
        juce::Label valueCaptionB;
        juce::TextEditor valueEditorB;
        juce::Label orderCaption;
        juce::TextEditor orderEditor;

        /** A pot's starting position, in percent. Switch positions live in the strip only. */
        juce::Label knobCaption;
        juce::TextEditor knobEditor;

        /** A group box's size, in grid squares. */
        juce::Label widthCaption, heightCaption;
        juce::TextEditor widthEditor, heightEditor;
        juce::TextEditor valueEditor, labelEditor;
        juce::ComboBox modelBox, taperBox;

        /** A changeover switch's action: ON-ON, ON-OFF-ON, ON-ON-ON. */
        juce::Label actionCaption;
        juce::ComboBox actionBox;

        /** Changes the action, keeping the lever on the same throw if the new action has it. */
        void reshapeSwitch(SchematicModel::SwitchAction action);

        /** Shown under the model box only when the sheet names a model this build lacks. */
        juce::Label modelDescription;
        juce::ToggleButton polarisedButton{"Electrolytic"};

        /** Scope only: the trace and the two readings under it. Its own component so it can
            repaint without the fields. */
        struct ScopeView : juce::Component
        {
            explicit ScopeView(ElementInspector& o) : owner(o) {}
            void paint(juce::Graphics&) override;

            /** Voltage axis on the side, time along the bottom, using the trace's own scale. */
            static void drawScopeAxes(juce::Graphics& g, juce::Rectangle<float> plot,
                                      juce::Rectangle<float> gutter,
                                      juce::Rectangle<float> timeAxis,
                                      const ScopeScale& scale, const ScopeReading& reading);

            ElementInspector& owner;
        };

        ScopeView scopeView{*this};

        /** Live current and power through the selected part. Its own component so it can
            repaint without the fields. */
        struct CurrentView : juce::Component
        {
            explicit CurrentView(ElementInspector& o) : owner(o) {}
            void paint(juce::Graphics&) override;
            ElementInspector& owner;
        };

        CurrentView currentView{*this};

        /** A scope's axes. Display only, no rebuild. */
        juce::ToggleButton scopeAutoButton{"Auto scale"};
        juce::Label scopeMinCaption, scopeMaxCaption, scopeSecondsCaption;
        juce::TextEditor scopeMinEditor, scopeMaxEditor, scopeSecondsEditor;

        void commitScopeAxes();

        /** Input only: the file player controls. Synced from the player by repaintReadouts(). */
        juce::ToggleButton fileInputButton{"Play a file"};
        juce::Label inputFileCaption;
        juce::TextButton inputFileButton;
        juce::ToggleButton loopButton{"Loop"};
        juce::TextButton playButton{"Play"}, stopButton{"Stop"};

        /** Playback position, as numbers and a bar. */
        struct TransportView : juce::Component
        {
            explicit TransportView(ElementInspector& o) : owner(o) {}
            void paint(juce::Graphics&) override;
            ElementInspector& owner;
        };

        TransportView transportView{*this};

        /** Updates the player controls from the player. Cheap when nothing changed. */
        void syncFilePlayer();

        /** The player status at the last sync. */
        InputFilePlayer::Status fileStatus;

        juce::ToggleButton cabButton{"Speaker cabinet"};
        juce::Label cabFileCaption;
        juce::TextButton cabFileButton;
        juce::Label hintLabel;

        /** The card's heading: the part type and its model, painted beside its symbol. */
        juce::String headerTitle, headerSubtitle;
        bool headerHasSymbol = false;
        SchematicModel::ElementType headerType{};

        /** The heading area, including the line under it. */
        juce::Rectangle<int> getHeaderBounds() const;

        /** Holds the fields, so a tall part scrolls inside the card. */
        juce::Viewport viewport;
        juce::Component content;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ElementInspector)
    };

    //==========================================================================
    /** The last build's messages, one row each, coloured by severity. Clicking a row that
        names a part selects it.

        A pill above the bottom band, sized to its contents. Shows on new messages and
        hides after `hideDelayMs`, unless it shows an error or warning, and never while the
        pointer is over it. */
    class MessageConsole : public juce::Component, private juce::Timer
    {
       public:
        MessageConsole();

        /** How long a message without problems stays up. */
        static constexpr int hideDelayMs = 5000;

        /** Narrowest the pill gets; callers must leave this much room. */
        static constexpr int minimumWidth = 201;

        /** Size that shows everything, wrapping lines longer than `maxWidth`. */
        juce::Point<int> getPreferredSize(int maxWidth);

        /** The text changed; the size may have too. */
        std::function<void()> onPreferredSizeChanged;

        /** True while an error or warning is shown, which keeps the console up. */
        bool hasProblems() const;

        /** True between a message arriving and the console hiding. */
        bool isWaitingToHide() const { return isTimerRunning(); }

        /** Replaces everything. `headline` is the summary line at the top. */
        void setMessages(juce::String status, bool statusIsError,
                         const std::vector<SchematicModel::Diagnostic>& messages);

        /** Replaces only the top line, keeping the build's messages below. */
        void setHeadline(juce::String text, bool isError);

        /** A row naming a part was clicked. */
        std::function<void(int elementId)> onMessageClicked;

        void paint(juce::Graphics&) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent&) override;

        /** Re-tints the prompt icon from the original artwork. */
        void applyColours();
        void lookAndFeelChanged() override { applyColours(); }

       private:
        /** New content: show the console and start the hide timer unless a problem is shown. */
        void announce();

        /** Hides the console, or retries shortly if the pointer is over it. */
        void timerCallback() override;

        /** One laid-out row: text, colour and the part it points at. Laid out when the
            messages change, since wrapping decides row heights and click targets. */
        struct Row
        {
            juce::String text;
            juce::Colour colour;
            int elementId = -1;
            int y = 0;
            int height = 0;
        };

        /** Lays the rows out at `width` and returns their total height. */
        int measureRows(int width);
        void layOutRows();

        juce::String status;
        bool statusIsError = false;
        std::vector<SchematicModel::Diagnostic> messages;
        std::vector<Row> rows;

        juce::Viewport viewport;
        juce::Component content;

        /** The prompt icon in the left gutter. */
        std::unique_ptr<juce::Drawable> promptIcon;

        /** Draws the rows inside the viewport; the console draws the gutter. */
        struct Rows : juce::Component
        {
            explicit Rows(MessageConsole& o) : owner(o) {}
            void paint(juce::Graphics& g) override;
            void mouseDown(const juce::MouseEvent& e) override;
            MessageConsole& owner;
        };

        Rows rowList{*this};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MessageConsole)
    };
} // namespace Celine
