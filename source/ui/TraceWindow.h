#pragma once

#include "SchematicSymbols.h"
#include "GlyphButton.h"

#include <optional>
#include <set>

namespace Celine
{
    /** The live traces' screen: the probes on the sheet, or its scopes, each drawn on one
        display in its own colour.

        A probe is one pin read against ground; a scope reads between its two pins. The point
        of either is to see a circuit's points together and in step, so a kind's traces share
        an axis and a time base, set here for all of them at once (and saved on each part).
        Their captures start on the same sample and advance on the same count
        (PluginProcessor::sampleScopes), so they line up column for column. The range fits
        whatever the traces swing, held steady (SteadyRange).

        A tab on the left edge per kind the sheet has, with its symbol, switches between
        them. Colours follow the part, in drawing order (colourFor),
        and the legend names every one. The window's component includes its tab strip; the
        card is to the right of it. */
    /** The traces' vertical range, held still the way a scope's is.

        Fitted afresh to every frame, the range followed each frame's extremes, so an
        asymmetric wave -- whose peaks a frame catches differently each time -- moved its zero
        line up and down a little at every repaint, and the trace with it. So the range only
        takes round values (1, 1.5, 2, 2.5, 3, 4, 5, 6 or 8 times a power of ten), and it
        changes in two ways only: at once, when a trace goes outside it, and back in, once
        the traces have stayed inside a smaller one with room to spare for `shrinkDelayMs`.

        A swing about zero, or nearer zero than it is wide, is shown symmetric about it, so
        zero is the middle line and stays there. A swing far from zero (a valve's plate at
        200 V moving a volt) is shown as itself, its ends rounded to a grid step, since
        holding zero on screen would flatten it to a line. */
    struct SteadyRange
    {
        double low = -1.0, high = 1.0;

        static constexpr double shrinkDelayMs = 1500.0;

        /** The traces' extremes this frame, and the time in milliseconds. */
        void update (double lowest, double highest, double nowMs);

        /** Forget what was held: the next update fits from scratch. */
        void reset() noexcept { held = false; }

        /** The round range that holds this swing, with `headroom` (a fraction of it) to
            spare at either end. */
        static std::pair<double, double> fitted (double lowest, double highest, double headroom);

        /** The smallest round value at or above x (> 0). */
        static double roundUp (double x);

    private:
        bool held = false;
        double shrinkingSince = -1.0;
    };

    class TraceWindow : public juce::Component
    {
    public:
        enum class Kind
        {
            probes,
            scopes
        };

        struct Trace
        {
            int elementId = 0;
            juce::String name;
        };

        TraceWindow();

        /** The probes and scopes on the sheet, in drawing order. */
        void setTraces (std::vector<Trace> probeList, std::vector<Trace> scopeList);
        bool isEmpty() const noexcept { return probes.empty() && scopes.empty(); }
        bool has (Kind which) const noexcept { return ! listFor (which).empty(); }

        Kind getKind() const noexcept { return kind; }
        void setKind (Kind);

        /** A kind's time span, shared by its traces, set from 5 ms to 4 s with the stepper. */
        void setWindowSeconds (Kind, double seconds);

        /** The span shown, as the stepper writes it ("40 ms", "2 s"). */
        juce::String getSpanText() const;

        /** Reads one part's trace (the editor's readScopeTrace). */
        std::function<bool (int elementId, ScopeReading&)> reader;

        /** The time base was changed here: write it to every part of that kind. */
        std::function<void (Kind, double seconds)> onWindowChanged;

        /** The hide button was clicked. */
        std::function<void()> onHideRequested;

        /** The colour of the n-th part of a kind (0-based), here and on the sheet. */
        static juce::Colour colourFor (int ordinal);

        /** Whether a part's trace is shown; clicking its legend entry switches it. */
        bool isShown (int elementId) const { return hidden.count (elementId) == 0; }
        void setShown (int elementId, bool shown);

        /** How a kind is shown, switched in the window's header and kept per kind.

            AC: each trace about its own average over the span, so traces resting at very
            different levels (a grid at 0 V, a plate at 200 V) share the screen without
            flattening one another; the legend then reads each one's DC level instead of its
            swing, which the axis already shows.

            Trigger: the span starts where the first trace shown last rose through its
            average, a tenth of the way in, so a repeating wave stands still rather than
            sliding past. With no such crossing (silence, a trace too slow for the span) it
            runs free, as a scope's auto trigger does. */
        struct Options
        {
            bool acCoupled = false;
            bool triggered = true;
        };

        Options getOptions (Kind k) const noexcept { return options[static_cast<int> (k)]; }
        void setOptions (Kind, Options);

        /** An option was switched here: kept by the owner, so a reopened window has it. */
        std::function<void (Kind, Options)> onOptionsChanged;

        /** Paused, the screen keeps the traces it last read, for reading at leisure. Not kept:
            a window opens running. */
        bool isPaused() const noexcept { return paused; }
        void setPaused (bool shouldPause);

        /** Reads the traces again (unless paused, or `readNew` is false: then only works the
            frame out again from what it has) and redraws. Called refreshesPerSecond times a
            second while the window shows, on the display's refresh, so the traces move at
            one steady pace whatever else on screen is redrawn; painting itself reads
            nothing. */
        void refresh (bool readNew = true);

        /** How often the traces are read again: a pace the eye can follow. At every frame
            (60 a second) a trace changed too quickly to read. */
        static constexpr double refreshesPerSecond = 20.0;

        /** Where a trace's span starts, as a column's age in the ring (0 the oldest), when it
            triggers: `shown` columns from there, its last rising crossing of its average a
            tenth of the way in. Nothing when it has no such crossing, or when the span that
            crossing starts ended more than `maxStaleColumns` before the newest column: the
            wave has stopped crossing (a note died away, the node settled), and holding that
            old picture until it left the ring froze the screen for up to a whole span. */
        static std::optional<int> triggeredStart (const ScopeReading&, int shown, int maxStaleColumns);

        /** How long the trigger may hold its last picture before it runs free, as a scope's
            auto trigger does: a quarter of a second, or a tenth of the span when that is
            longer, never more than the span. A wave crossing at least this often (4 Hz)
            always triggers. */
        static constexpr double autoTriggerSeconds = 0.25;
        static int staleColumnsAllowed (double spanSeconds, int shown);
        static constexpr int tabWidth = 30, tabHeight = 34;
        /** The width it would like, and its height with one row of legend. */
        static constexpr int preferredWidth = 560 + tabWidth, preferredHeight = 176;

        /** Its height at this width, the legend wrapped to hold every entry. */
        int getPreferredHeight (int width) const;

        void paint (juce::Graphics&) override;
        void resized() override;
        void lookAndFeelChanged() override;
        bool hitTest (int x, int y) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:

        const std::vector<Trace>& listFor (Kind k) const noexcept { return k == Kind::probes ? probes : scopes; }
        juce::Rectangle<int> card() const { return getLocalBounds().withTrimmedLeft (tabWidth); }

        /** The kinds with a tab, top down, and where each tab is. */
        std::vector<Kind> tabs() const;
        juce::Rectangle<int> tabArea (size_t index) const;

        void paintTraces (juce::Graphics&, juce::Rectangle<int> area);

        std::vector<Trace> probes, scopes;
        Kind kind = Kind::probes;
        double windowSeconds[2] = { 0.04, 0.04 };

        // The time span's stepper: shorter, the span, longer.
        GlyphButton shorter { "Shorter time span", false, 9.0f }, longer { "Longer time span", true, 9.0f };
        juce::Rectangle<int> spanArea;
        void stepWindow (int direction);
        void refreshStepper();
        juce::Rectangle<int> hideArea;
        bool overHide = false;

        /** The pointer over the plot, for the reading cursor; and where the plot was last
            drawn, to know. */
        std::optional<juce::Point<int>> cursor;
        juce::Rectangle<float> lastPlot;

        /** What the last paint showed, for the cursor to read the same columns: the span's
            start in the ring and its length, and each trace's offset (its DC in AC). */
        struct Shown
        {
            int from = 0, columns = 0;
            std::vector<float> offsets;
        };

        Shown shown;

        /** The rest of what refresh() works out for painting: each trace's swing and average
            over the span, whether any trace runs and any is shown, the first shown (the one
            the trigger watches), and where it triggered, in columns from the left. */
        struct Stats
        {
            float low = 0.0f, high = 0.0f, mean = 0.0f;
            bool counted = false; // any column held a reading
        };

        struct Frame
        {
            std::vector<Stats> stats;
            bool anyLive = false, anyShown = false;
            int firstShown = -1;
            std::optional<int> triggerAt;
        };

        Frame frame;

        void paintCursor (juce::Graphics&, juce::Rectangle<float> plot, float lowest, float highest);

        /** One reading per trace, refreshed each paint unless paused. */
        std::vector<ScopeReading> readings;
        bool readingsStale = true;

        Options options[2];
        bool paused = false;

        /** A small switch in the header: a word, or the pause glyph. Lit (the accent) when on. */
        class HeaderToggle : public juce::Button
        {
        public:
            HeaderToggle (const juce::String& word, const juce::String& tip, bool isPause = false);
            void paintButton (juce::Graphics&, bool highlighted, bool down) override;
            int getPreferredWidth() const;

        private:
            juce::String text;
            bool pauseGlyph;
        };

        HeaderToggle acButton { "AC", "AC coupling: each trace about its own average, its DC level in the legend" },
            triggerButton { "Trigger", "Hold a repeating wave still: start where the first trace rises through its average" },
            pauseButton { {}, "Pause the screen to read it", true };

        void refreshOptionButtons();

        /** The vertical range, held while the traces fit it: see SteadyRange. */
        SteadyRange range;

        /** Traces switched off from the legend, by element id: kept while the window lives,
            so a part keeps its state across a rebuild or a change of tab. */
        std::set<int> hidden;

        /** Where each legend entry was drawn, and whose it is, for the clicks. */
        std::vector<std::pair<juce::Rectangle<int>, int>> legendItems;

        int legendItemAt (juce::Point<int> p) const;

        static constexpr int legendHeight = 18;
        static constexpr float legendFontHeight = 11.5f;
        static int nameWidth (const juce::String& name);

        /** Every legend entry of the kind shown, flowed into rows across this width, from the
            legend's top left. */
        std::vector<juce::Rectangle<int>> legendSlots (Kind which, int width) const;

        /** The display's frames, and when the traces were last read: each read lands on a
            frame, every third at 60 Hz, rather than wherever a timer happens to fire. */
        void frameDrawn();
        double lastReadMs = 0.0;

        /** Ticks every frame the display draws, and reads only while the window is on screen.
            Last, so it goes first and nothing it reads is gone before it. */
        juce::VBlankAttachment frames { this, [this] { frameDrawn(); } };
    };

    //==========================================================================
    /** The window folded away: a pill above the control strip that brings it back, drawn
        with the symbols of what it holds and "Show probes", "Show scopes" or both. */
    class TraceWindowButton : public juce::Button
    {
    public:
        TraceWindowButton();

        void setContents (bool probes, bool scopes);
        int getPreferredWidth() const;

        static constexpr int height = 28;

        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        bool withProbes = false, withScopes = false;
        juce::String text() const;
    };
} // namespace Celine
