#pragma once

#include "../Schematic/Schematic.h"
#include "../dsp/InputFilePlayer.h"
#include "../Schematic/SchematicBuilder.h"
#include "SchematicSymbols.h"

#include <functional>

namespace Celine
{
    //==========================================================================
    /**
        The parts bin: one button per element type, each drawing its own symbol
        so the palette reads as a set of parts rather than a list of words.

        A panel floating down the left of the sheet, two columns wide. Each row
        is one of the pairs written on a line of `paletteOrder` -- Input beside
        Output, Capacitor beside Resistor -- and the names are drawn at the one
        size at which the longest still fits its column, so every row holds two
        entries and the grid stays even.

        The tab on its right-hand edge folds it to symbols alone and back. That
        is the user's choice, never the window's: an earlier version folded on
        its own whenever the names did not fit, and a panel that changes what
        it shows as you resize is one you cannot learn. A window too short for
        the list scrolls it instead.
    */
    class ElementPalette : public juce::Component
    {
       public:
        ElementPalette();

        /** Declared here and defined in the .cpp, where Entry is a complete
            type -- OwnedArray can't destroy what it can't see the size of. */
        ~ElementPalette() override;

        std::function<void(SchematicModel::ElementType)> onTypeChosen;

        /** The wire row was clicked. Wire lives here rather than in the toolbar
            because drawing one is *placing something on the sheet*, which is
            what this panel is for -- the toolbar is for what you do to what is
            already there. It is a tool rather than an element type only because
            of how it is implemented, which is not a distinction worth making the
            user learn. */
        std::function<void()> onWireChosen;

        /** Lights whichever row is armed -- a part, the wire, or neither. One
            call rather than two setters, so the two can't disagree. */
        void setActive(const SchematicModel::ElementType* type, bool wire);

        /** Symbols alone, or symbols and names. */
        void setCollapsed(bool shouldBeCollapsed);
        bool isCollapsed() const noexcept { return collapsed; }

        /** The tab was clicked, so the panel has changed width and whoever
            places it should place it again. */
        std::function<void()> onCollapsedChanged;

        /** How big the panel wants to be, tab included, when it may be no
            taller than `maxHeight`: as wide as its two columns, and as tall as
            its rows -- or `maxHeight`, scrolling, with the scrollbar's width
            added so the rows keep theirs. */
        juce::Point<int> getPreferredSize(int maxHeight = std::numeric_limits<int>::max()) const;

        /** Whether the list is taller than the panel and scrolls. */
        bool isScrolling() const;

        /** The widest the panel can be, folded or not: unfolded, with its
            scrollbar showing. What the window's minimum width allows for. */
        int getMaximumWidth() const;

        /** The size every name is drawn at: Theme::paletteFontHeight, or less
            if that is what it takes for the longest name to fit its column. */
        float getNameFontHeight() const noexcept { return nameFontHeight; }

        /** The panel itself, without the tab standing off its edge. */
        juce::Rectangle<int> getBodyBounds() const;

        /** Only the panel and its tab, not the empty corners beside the tab,
            so the sheet under those still takes its clicks. */
        bool hitTest(int x, int y) override;

        void resized() override;
        void paint(juce::Graphics&) override;

       private:
        class Entry;
        class CollapseTab;

        /** Where every entry goes in the list, in order, for the current mode. */
        std::vector<juce::Rectangle<int>> layOutGrid() const;

        /** Works out getNameFontHeight() from the names. */
        float fittingFontHeight() const;

        float nameFontHeight = 12.0f;

        int listWidth() const;
        int listHeight() const;

        bool collapsed = false;

        /** The rows live in here, so a window too short for them scrolls the
            list rather than the panel running into the band below. */
        juce::Viewport viewport;
        juce::Component content;

        juce::OwnedArray<Entry> entries;
        std::unique_ptr<CollapseTab> tab;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ElementPalette)
    };

    //==========================================================================
    /**
        Everything about the selected part that can be typed or picked.

        Edits go straight into the Element and then announce themselves; the
        editor decides whether that means a rebuild. Values accept schematic
        notation -- "4k7", "100n", "2.2M" -- because that is how they are written
        on paper and typing 0.0000001 for a capacitor is a good way to get it
        wrong.

        A card floating over the sheet, sized to what it is showing: a resistor
        is a few fields and gets a short card, the shortcut list a tall one.
        The editor asks getPreferredSize() and caps it to the room there is;
        past that cap the fields scroll.
    */
    class ElementInspector : public juce::Component
    {
       public:
        ElementInspector();

        /** Points the inspector at a part, or at nothing. */
        void setElement(SchematicModel::Element* element);

        /** The card that shows everything currently visible without scrolling,
            padding included. Fields are laid out at the fixed inspector width;
            the shortcut list is as wide as its longest line. */
        juce::Point<int> getPreferredSize();

        /** What the inspector shows changed in a way that may change its size --
            a different part, or a field shown or hidden. */
        std::function<void()> onPreferredSizeChanged;

        /** A field changed. The part is already updated. */
        std::function<void()> onEdited;

        /** A live control's position changed -- a pot's knob or a switch's
            throw. Separate from onEdited because these are the two things on
            the sheet the running circuit already follows: telling someone to
            press Rebuild to hear a change they can already hear is how a build
            button stops meaning anything. */
        std::function<void()> onControlEdited;

        /** Where a selected scope's picture comes from. Same callback the canvas
            uses, so the two draw the same probe the same way. */
        ScopeReader scopeReader;

        /** The selected part's mean current, mean power and peak current. False
            when there is no reading -- see PluginProcessor::readInspectedCurrent
            for the two ways that happens. */
        std::function<bool (float& current, float& power, float& peak)> currentReader;

        /** Tells the processor which part to measure, so nothing is measured
            when nobody is looking. Called on every selection change, including
            to nothing. */
        std::function<void (int elementId)> onInspectedElementChanged;

        /** A scope's time base changed. Its own callback because it is the one
            thing on that panel the audio thread reads -- it sets how many
            samples go into a column -- while the rest is read by the renderer
            alone. Not `onEdited`: nothing here reaches the matrix, so lighting
            the Rebuild button would be asking for a rebuild that changes
            nothing about the circuit. */
        std::function<void()> onScopeTimebaseChanged;

        /** Repaints only the live readouts -- the scope's trace and the
            selected part's current. Called from the editor's timer, and
            deliberately not `repaint()`: these have to be asked for again
            several times a second, and repainting a panel of text editors at
            that rate to animate one box is most of a core spent on nothing.

            Each panel declines in a line when it is not showing, so calling
            this on every tick costs nothing when nothing live is selected. */
        void repaintReadouts();

        /** The Output terminal's cabinet changed -- the switch was thrown, or a
            different impulse response was chosen.

            Its own callback rather than either of the two above, because it is
            neither of the things they mean. It is not `onEdited`: nothing about
            a cabinet reaches the matrix, so lighting the Rebuild button for it
            would be asking someone to rebuild a circuit that has not changed.
            And it is not `onControlEdited`: that one pushes a position into the
            control strip, and a cabinet has no knob there. */
        std::function<void()> onCabChanged;

        /** The user asked to pick an impulse response file. The editor owns the
            dialog, the same way it owns Save and Load -- a panel that draws
            fields has no business keeping a FileChooser alive across a modal
            call. */
        std::function<void()> onCabFileRequested;

        /** The Input terminal's file player, which its panel drives directly:
            every call on it returns at once and none of it is part of the sheet,
            so nothing here needs the editor to decide anything -- not undo, not
            Rebuild, not the preset's modified mark. Null shows the panel's
            controls disabled. */
        InputFilePlayer* filePlayer = nullptr;

        /** The user asked to pick a file to play. The editor owns the dialog,
            as it does the cabinet's. */
        std::function<void()> onInputFileRequested;

        /** The delete button was pressed. */
        std::function<void()> onDeleteRequested;

        /** The rotate button was pressed. */
        std::function<void()> onRotateRequested;

        /** The flip button was pressed. */
        std::function<void()> onFlipRequested;

        void resized() override;
        void paint(juce::Graphics&) override;

        /** Every colour this panel hands to a child rather than reading as it paints.
            Called from the constructor and again on every theme change. */
        void applyColours();
        void lookAndFeelChanged() override { applyColours(); }

       private:
        /** The captions, as one list. Two loops need it -- the constructor's, which
            sets the face, and applyColours', which runs again whenever the theme
            moves -- and two hand-written copies of thirteen names is one place for a
            new caption to be added to only one of them. */
        std::array<juce::Label*, 14> captionLabels();

        void rebuildForElement();

        /** Lays the fields out again and tells the editor the card's size may
            have moved. */
        void contentChanged();

        /** The width of the field column inside the card, padding excluded, for
            what is showing now. */
        int contentWidth() const;

        /** The shortcut list's height, one line per line of text. Measured
            rather than given whatever the panel had left, which is what it was
            when the panel was a fixed column -- a card sized to its contents
            cannot also size its contents to the card. */
        int hintHeight() const;

        /** The shortcut list's longest line, which is what sets the card's
            width when nothing is selected. */
        int hintWidth() const;

        void showNothingSelected();
        void fillFieldsFrom(const SchematicModel::Element& part);
        void populateModelBox(const SchematicModel::Element& part);
        void showFieldsFor();
        /** Places every visible field for a given width, returning the height
            they came to. */
        int layOutFields(int width);
        static juce::String valueTextFor(const SchematicModel::Element& e);
        static juce::String secondValueText(const SchematicModel::Element& e);
        void commitValue();
        void commitOrder();
        void commitKnob();
        void commitSize();

        SchematicModel::Element* element = nullptr;

        juce::Label titleLabel;
        juce::Label valueCaption, labelCaption, modelCaption, taperCaption;
        juce::Label valueCaptionB;
        juce::TextEditor valueEditorB;
        juce::Label orderCaption;
        juce::TextEditor orderEditor;

        /** A pot's knob position, as a percentage. The strip is where you play
            with it; this is where you say where it should *start*, which is a
            property of the drawing rather than a thing you reach for mid-take.

            A switch has no counterpart here any more. Its throw is live and
            nothing else -- there is no starting position to state that the
            strip does not already hold -- so it lives in the strip alone. */
        juce::Label knobCaption;
        juce::TextEditor knobEditor;

        /** A group box's size, in grid squares. Typed as well as dragged: the
            corner handle is quicker, but two boxes only line up if you can say
            what they are, and a box dragged off the visible sheet has no corner
            left to grab. */
        juce::Label widthCaption, heightCaption;
        juce::TextEditor widthEditor, heightEditor;
        juce::TextEditor valueEditor, labelEditor;
        juce::ComboBox modelBox, taperBox;

        /** What the chosen model actually is, in a sentence. The dropdown has
            room for a part number and no more, which is not enough to tell a
            BC109C from a 2N3904 or a GZ34 from a 5Y3GT. */
        juce::Label modelDescription;
        juce::ToggleButton polarisedButton{"Electrolytic"};

        /** Output only: the cabinet switch, and the file it plays through.

            The button says which file rather than "Browse", because a cabinet
            you cannot identify without opening a dialog is one you will load
            twice. When the named file has gone it says that too -- the console
            reports it as well, but the panel is where you are looking when you
            wonder why nothing changed. */
        /** Scope only: the picture and the two numbers under it.

            Its own component rather than painted into the panel, so it can be
            repainted on its own several times a second while the fields around
            it stay still -- see repaintReadouts. */
        struct ScopeView : juce::Component
        {
            explicit ScopeView(ElementInspector& o) : owner(o) {}
            void paint(juce::Graphics&) override;

            /** Volts up the side, time along the bottom, drawn against the scale
                the trace actually used rather than one worked out again here. */
            static void drawScopeAxes(juce::Graphics& g, juce::Rectangle<float> plot,
                                      juce::Rectangle<float> gutter,
                                      juce::Rectangle<float> timeAxis,
                                      const ScopeScale& scale, const ScopeReading& reading);

            ElementInspector& owner;
        };

        ScopeView scopeView{*this};

        /** Live current and power through the selected part.

            Its own component for the same reason ScopeView is: it changes
            several times a second and the fields around it do not, so repainting
            it must not repaint them. */
        struct CurrentView : juce::Component
        {
            explicit CurrentView(ElementInspector& o) : owner(o) {}
            void paint(juce::Graphics&) override;
            ElementInspector& owner;
        };

        CurrentView currentView{*this};

        /** A scope's axes. Display only -- none of it reaches the matrix, so
            none of it lights the Rebuild button. */
        juce::ToggleButton scopeAutoButton{"Auto scale"};
        juce::Label scopeMinCaption, scopeMaxCaption, scopeSecondsCaption;
        juce::TextEditor scopeMinEditor, scopeMaxEditor, scopeSecondsEditor;

        void commitScopeAxes();

        /** Input only: the file player. Live or file, which file, once or
            round and round, and the transport.

            Kept in step with the player by repaintReadouts() rather than told,
            because the player changes on its own -- a file plays to its end, a
            load finishes -- and the panel is rebuilt whenever the selection
            moves, so the player is the only one of the two that remembers. */
        juce::ToggleButton fileInputButton{"Play a file"};
        juce::Label inputFileCaption;
        juce::TextButton inputFileButton;
        juce::ToggleButton loopButton{"Loop"};
        juce::TextButton playButton{"Play"}, stopButton{"Stop"};

        /** Where the file has got to, as numbers and a bar. Its own component
            for the same reason the scope's is: it moves while nothing else on
            the panel does. */
        struct TransportView : juce::Component
        {
            explicit TransportView(ElementInspector& o) : owner(o) {}
            void paint(juce::Graphics&) override;
            ElementInspector& owner;
        };

        TransportView transportView{*this};

        /** Brings the player's controls into line with what it says. Cheap when
            nothing changed, so it can run on every timer tick: one read of the
            player, and a repaint of the transport only when something it draws
            has moved. */
        void syncFilePlayer();

        /** What the player said at the last sync, which the transport draws
            from rather than asking again. */
        InputFilePlayer::Status fileStatus;

        juce::ToggleButton cabButton{"Cab"};
        juce::Label cabFileCaption;
        juce::TextButton cabFileButton;
        juce::TextButton rotateButton{"Rotate"}, flipButton{"Flip"};
        juce::TextButton deleteButton{"Delete"};
        juce::Label hintLabel;

        /** The fields live inside this, so a part with more of them than the
            window has room for -- a scope, with its picture and three axes --
            scrolls rather than running off the bottom of the card. */
        juce::Viewport viewport;
        juce::Component content;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ElementInspector)
    };

    //==========================================================================
    /**
        Everything the last build had to say, as a list you can work through.

        A status line holds one sentence, which is the right size for "Built: 42
        parts" and the wrong size for six separate problems -- those arrive
        joined by double spaces into a paragraph nobody reads to the end of.
        Here each message is its own row, coloured by severity, carrying the
        part it is about and where that part sits, and clicking one selects it
        on the sheet. Finding the resistor at 35,-67 is then a click rather than
        a hunt.

        A pill floating above the bottom band, as wide and tall as what it has to
        say. It comes up whenever there is something new and goes again
        `hideDelayMs` later -- unless what it is showing includes an error or a
        warning, in which case it stays until a message without one replaces it,
        since a problem that hides itself is one you only find by ear. It also
        waits while the pointer is over it, so a row is never pulled out from
        under a click.
    */
    class MessageConsole : public juce::Component, private juce::Timer
    {
       public:
        MessageConsole();

        /** How long a message with nothing wrong in it stays up. */
        static constexpr int hideDelayMs = 5000;

        /** The narrowest the pill goes -- its glyph, its padding, and a line of
            text wide enough not to be a column of single words. Whoever places
            it has to leave it at least this much room. */
        static constexpr int minimumWidth = 201;

        /** The pill that shows everything without scrolling, wrapping only
            where a line is wider than `maxWidth`. */
        juce::Point<int> getPreferredSize(int maxWidth);

        /** The text changed, so the pill's size may have. */
        std::function<void()> onPreferredSizeChanged;

        /** True while what is showing includes an error or a warning -- the
            failed build's headline counts -- which is what keeps the console up. */
        bool hasProblems() const;

        /** True between a message arriving and the console hiding itself. False
            while a problem is holding it up, and once it has gone. */
        bool isWaitingToHide() const { return isTimerRunning(); }

        /** Replaces everything shown. `headline` is the one-line summary that
            goes at the top -- "Built: 42 parts" or the reason it didn't. */
        void setMessages(juce::String status, bool statusIsError,
                         const std::vector<SchematicModel::Diagnostic>& messages);

        /** Just the top line -- "Saved to fuzz.celsch", "press Rebuild to hear
            it" -- leaving whatever the last build said in place beneath it. */
        void setHeadline(juce::String text, bool isError);

        /** A row was clicked and it names a part. */
        std::function<void(int elementId)> onMessageClicked;

        void paint(juce::Graphics&) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent&) override;

        /** The prompt mark, re-tinted. Artwork rather than a colour handed to a child,
            but the same rule applies: tinting is destructive, so a theme change has to
            start again from what is in the binary. */
        void applyColours();
        void lookAndFeelChanged() override { applyColours(); }

       private:
        /** Something new arrived: bring the console up, and start the clock
            unless a problem is holding it there. */
        void announce();

        /** The clock ran out. Hides the console, or looks again shortly if the
            pointer is resting on it. */
        void timerCallback() override;

        /** One laid-out row: what to draw, in what colour, and what it points
            at. Laid out once when the messages change rather than per paint,
            because a message can wrap to several lines and the wrapping decides
            both the row height and which row a click landed in. */
        struct Row
        {
            juce::String text;
            juce::Colour colour;
            int elementId = -1;
            int y = 0;
            int height = 0;
        };

        /** Lays the rows out for a given text width, returning the total
            height they came to. */
        int measureRows(int width);
        void layOutRows();

        juce::String status;
        bool statusIsError = false;
        std::vector<SchematicModel::Diagnostic> messages;
        std::vector<Row> rows;

        juce::Viewport viewport;
        juce::Component content;

        /** The prompt glyph in the left gutter, which is the whole of what names
            this panel. It said "Console" in words before, which is a heading
            spending a row of a panel on a fact you can see. */
        std::unique_ptr<juce::Drawable> promptIcon;

        /** The list is inside the viewport, so it draws the rows; the console
            itself only draws the gutter. */
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
