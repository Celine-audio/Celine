#include "TraceWindow.h"


#include <CelineUI/Fonts.h>
#include <CelineUI/Theme.h>

namespace Celine
{
    namespace
    {
        constexpr int pad = 12, headerHeight = 30;

        // The spans the stepper walks, 1-2-5 up to the 4 s a part may hold
        // (Element::maxScopeSeconds); one more on either side is a click away.
        const double windowChoices[] = { 0.005, 0.01, 0.02, 0.04, 0.1, 0.2, 0.5, 1.0, 2.0, 4.0 };

        juce::String timeText (double seconds)
        {
            if (seconds < 1.0)
                return juce::String (juce::roundToInt (seconds * 1000.0)) + " ms";

            const double whole = std::round (seconds);
            return (std::abs (seconds - whole) < 1.0e-9 ? juce::String (static_cast<int> (whole)) : juce::String (seconds, 1)) + " s";
        }

        SchematicModel::ElementType symbolOf (TraceWindow::Kind kind)
        {
            return kind == TraceWindow::Kind::probes ? SchematicModel::ElementType::Probe : SchematicModel::ElementType::Scope;
        }

        /** A value in volts to three figures with an SI prefix: "4.7 V", "438 mV", "1.2 kV". */
        juce::String volts (double value)
        {
            if (! std::isfinite (value))
                return "--";

            struct Prefix
            {
                double scale;
                const char* text;
            };

            static const Prefix prefixes[] = { { 1.0e3, "k" }, { 1.0, "" }, { 1.0e-3, "m" }, { 1.0e-6, "\xc2\xb5" }, { 1.0e-9, "n" } };

            const double magnitude = std::abs (value);
            const Prefix* chosen = &prefixes[1];

            if (magnitude > 0.0)
            {
                chosen = &prefixes[std::size (prefixes) - 1];
                for (const auto& p : prefixes)
                    if (magnitude >= p.scale * 0.9995)
                    {
                        chosen = &p;
                        break;
                    }
            }

            // Three significant figures, trailing zeros dropped (JUCE prints every digit when
            // asked for none, so whole numbers go as integers).
            const double scaled = value / chosen->scale;
            const double a = std::abs (scaled);
            const int decimals = a >= 99.95 ? 0 : a >= 9.995 ? 1 : 2;
            auto text = decimals > 0 ? juce::String (scaled, decimals) : juce::String (static_cast<juce::int64> (std::llround (scaled)));

            if (text.containsChar ('.'))
                text = text.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");

            return text + " " + juce::String (juce::CharPointer_UTF8 (chosen->text)) + "V";
        }

        /** Gridlines between low and high on a round step (SteadyRange's values), about four. */
        std::vector<double> ticksBetween (double low, double high)
        {
            std::vector<double> ticks;
            if (! (high > low))
                return ticks;

            const double step = SteadyRange::roundUp ((high - low) / 4.0);
            for (double t = std::ceil (low / step - 1.0e-9) * step; t <= high + step * 1.0e-9; t += step)
                ticks.push_back (std::abs (t) < step * 1.0e-9 ? 0.0 : t);

            return ticks;
        }

        /** A column's middle, by its age in the ring (0 the oldest). */
        float middleAt (const ScopeReading& r, int age) noexcept
        {
            const int c = (r.writeColumn + age) % r.count;
            return 0.5f * (r.minimum[c] + r.maximum[c]);
        }

        /** The choice nearest a span, as an index. */
        int nearestChoice (double seconds)
        {
            size_t best = 0;
            for (size_t i = 1; i < std::size (windowChoices); ++i)
                if (std::abs (std::log (windowChoices[i] / seconds)) < std::abs (std::log (windowChoices[best] / seconds)))
                    best = i;
            return static_cast<int> (best);
        }
    } // namespace

    TraceWindow::TraceWindow()
    {
        // The time every trace spans, stepped down and up.
        shorter.onClick = [this] { stepWindow (-1); };
        longer.onClick = [this] { stepWindow (1); };
        addAndMakeVisible (shorter);
        addAndMakeVisible (longer);
        refreshStepper();

        // How the traces are shown: AC, triggered, paused.
        acButton.onClick = [this]
        {
            auto o = options[static_cast<int> (kind)];
            o.acCoupled = ! o.acCoupled;
            setOptions (kind, o);
            if (onOptionsChanged)
                onOptionsChanged (kind, o);
        };
        triggerButton.onClick = [this]
        {
            auto o = options[static_cast<int> (kind)];
            o.triggered = ! o.triggered;
            setOptions (kind, o);
            if (onOptionsChanged)
                onOptionsChanged (kind, o);
        };
        pauseButton.onClick = [this] { setPaused (! paused); };

        for (auto* b : { &acButton, &triggerButton, &pauseButton })
            addAndMakeVisible (b);
        refreshOptionButtons();

        lookAndFeelChanged();
        setOpaque (false);
    }

    juce::Colour TraceWindow::colourFor (int ordinal)
    {
        // Eight hues; the ninth part on takes them again, darkened, so up to sixteen are
        // told apart by hue and shade together -- and by name in the legend, always.
        constexpr int hues = 8;
        const auto hue = Theme::traceColour (ordinal % hues + 1);
        return ordinal < hues ? hue : hue.interpolatedWith (juce::Colours::black, 0.4f);
    }

    void TraceWindow::setTraces (std::vector<Trace> newProbes, std::vector<Trace> newScopes)
    {
        const bool wasEmpty = isEmpty();
        probes = std::move (newProbes);
        scopes = std::move (newScopes);

        // Stay on the kind shown while the sheet still has it; probes first on a fresh start.
        if (wasEmpty || ! has (kind))
            kind = has (Kind::probes) ? Kind::probes : Kind::scopes;

        setKind (kind);
    }

    void TraceWindow::setKind (Kind newKind)
    {
        kind = newKind;
        readings.assign (listFor (kind).size(), {});
        readingsStale = true; // read once even while paused
        range.reset(); // other traces: fit them afresh
        refreshStepper();
        refreshOptionButtons();
        refresh (false);
        repaint();
    }

    void TraceWindow::setOptions (Kind k, Options o)
    {
        const bool acChanged = options[static_cast<int> (k)].acCoupled != o.acCoupled;
        options[static_cast<int> (k)] = o;

        // Other values on the axis: fit them afresh.
        if (acChanged && k == kind)
            range.reset();

        refreshOptionButtons();
        refresh (false);
    }

    void TraceWindow::setPaused (bool shouldPause)
    {
        paused = shouldPause;
        refreshOptionButtons();
        refresh (false);
    }

    void TraceWindow::refreshOptionButtons()
    {
        const auto o = options[static_cast<int> (kind)];
        acButton.setToggleState (o.acCoupled, juce::dontSendNotification);
        triggerButton.setToggleState (o.triggered, juce::dontSendNotification);
        pauseButton.setToggleState (paused, juce::dontSendNotification);
        pauseButton.setTooltip (paused ? "Run the screen again" : "Pause the screen to read it");
    }

    int TraceWindow::staleColumnsAllowed (double spanSeconds, int shown)
    {
        const double columnSeconds = spanSeconds / juce::jmax (1, shown);
        const int quarterSecond = columnSeconds > 0.0 ? static_cast<int> (std::ceil (autoTriggerSeconds / columnSeconds)) : shown;
        return juce::jlimit (1, juce::jmax (1, shown), juce::jmax (shown / 10, quarterSecond));
    }

    std::optional<int> TraceWindow::triggeredStart (const ScopeReading& r, int shown, int maxStaleColumns)
    {
        const int count = r.count, first = r.firstFilled();
        if (shown < 2 || count - first < shown)
            return std::nullopt;

        // The level is the trace's average over what the ring holds, and a crossing only
        // counts once it has been a tenth of its swing below that: noise riding on the level
        // does not set it off.
        double sum = 0.0;
        float lowest = middleAt (r, first), highest = lowest;
        for (int a = first; a < count; ++a)
        {
            const float m = middleAt (r, a);
            sum += m;
            lowest = std::min (lowest, m);
            highest = std::max (highest, m);
        }

        if (highest - lowest < 1.0e-6f)
            return std::nullopt;

        const float level = static_cast<float> (sum / (count - first));
        const float band = 0.1f * (highest - lowest);
        const int lead = shown / 10;

        // The newest crossing with a whole span after it, less the lead, whose span ended
        // recently enough.
        std::optional<int> best;
        bool armed = false;

        for (int a = first; a < count; ++a)
        {
            const float m = middleAt (r, a);

            if (m < level - band)
                armed = true;
            else if (armed && m >= level)
            {
                armed = false;
                if (a - lead >= first && a - lead + shown <= count)
                    best = a - lead;
            }
        }

        if (best && count - (*best + shown) > maxStaleColumns)
            return std::nullopt;

        return best;
    }

    void TraceWindow::setWindowSeconds (Kind k, double seconds)
    {
        windowSeconds[static_cast<int> (k)] = seconds;
        if (k == kind)
            refreshStepper();
    }

    void TraceWindow::stepWindow (int direction)
    {
        const int last = static_cast<int> (std::size (windowChoices)) - 1;
        const int next = juce::jlimit (0, last, nearestChoice (windowSeconds[static_cast<int> (kind)]) + direction);

        windowSeconds[static_cast<int> (kind)] = windowChoices[static_cast<size_t> (next)];
        refreshStepper();

        // Another span: other swings, so fit the range afresh.
        range.reset();

        if (onWindowChanged)
            onWindowChanged (kind, windowSeconds[static_cast<int> (kind)]);
    }

    void TraceWindow::refreshStepper()
    {
        const int at = nearestChoice (windowSeconds[static_cast<int> (kind)]);
        shorter.setEnabled (at > 0);
        longer.setEnabled (at < static_cast<int> (std::size (windowChoices)) - 1);
        repaint (spanArea);
    }

    juce::String TraceWindow::getSpanText() const
    {
        return timeText (windowSeconds[static_cast<int> (kind)]);
    }

    std::vector<TraceWindow::Kind> TraceWindow::tabs() const
    {
        std::vector<Kind> list;
        for (const auto k : { Kind::probes, Kind::scopes })
            if (has (k))
                list.push_back (k);
        return list;
    }

    juce::Rectangle<int> TraceWindow::tabArea (size_t index) const
    {
        return { 0, 14 + static_cast<int> (index) * (tabHeight + 4), tabWidth + 1, tabHeight };
    }

    bool TraceWindow::hitTest (int x, int y)
    {
        if (card().contains (x, y))
            return true;

        for (size_t i = 0; i < tabs().size(); ++i)
            if (tabArea (i).contains (x, y))
                return true;

        return false;
    }

    void TraceWindow::lookAndFeelChanged()
    {
        repaint();
    }

    void TraceWindow::resized()
    {
        auto header = card().reduced (pad, 0).removeFromTop (headerHeight + pad).withTrimmedTop (pad - 2);
        hideArea = header.removeFromRight (24).withSizeKeepingCentre (24, 24);
        header.removeFromRight (6);
        // - 40 ms +: the span between its two steps.
        header.removeFromRight (4);
        longer.setBounds (header.removeFromRight (24).withSizeKeepingCentre (24, 24));
        spanArea = header.removeFromRight (56);
        shorter.setBounds (header.removeFromRight (24).withSizeKeepingCentre (24, 24));

        // AC, Trigger and pause, a group's gap before the span.
        header.removeFromRight (14);
        pauseButton.setBounds (header.removeFromRight (24).withSizeKeepingCentre (24, 22));
        header.removeFromRight (6);
        triggerButton.setBounds (header.removeFromRight (triggerButton.getPreferredWidth()).withSizeKeepingCentre (triggerButton.getPreferredWidth(), 22));
        header.removeFromRight (4);
        acButton.setBounds (header.removeFromRight (acButton.getPreferredWidth()).withSizeKeepingCentre (acButton.getPreferredWidth(), 22));
    }

    void TraceWindow::mouseMove (const juce::MouseEvent& e)
    {
        const bool over = hideArea.contains (e.getPosition());
        bool overTab = false;
        for (size_t i = 0; i < tabs().size(); ++i)
            overTab = overTab || tabArea (i).contains (e.getPosition());

        setMouseCursor (over || overTab || legendItemAt (e.getPosition()) != 0 ? juce::MouseCursor::PointingHandCursor
                                                                             : juce::MouseCursor::NormalCursor);
        if (over != overHide)
        {
            overHide = over;
            repaint (hideArea);
        }

        // Over the plot, the reading cursor follows the pointer.
        const auto wasCursor = cursor;
        cursor = lastPlot.contains (e.position) ? std::optional<juce::Point<int>> (e.getPosition()) : std::nullopt;
        if (cursor != wasCursor)
            repaint();
    }

    void TraceWindow::mouseExit (const juce::MouseEvent&)
    {
        overHide = false;
        cursor.reset();
        repaint();
    }

    void TraceWindow::mouseUp (const juce::MouseEvent& e)
    {
        if (hideArea.contains (e.getPosition()))
        {
            if (onHideRequested)
                onHideRequested();
            return;
        }

        // A legend entry switches its trace.
        if (const int id = legendItemAt (e.getPosition()); id != 0)
        {
            setShown (id, ! isShown (id));
            return;
        }

        const auto list = tabs();
        for (size_t i = 0; i < list.size(); ++i)
            if (tabArea (i).contains (e.getPosition()) && list[i] != kind)
                setKind (list[i]);
    }

    int TraceWindow::nameWidth (const juce::String& name)
    {
        return juce::roundToInt (std::ceil (juce::GlyphArrangement::getStringWidth (Fonts::light (legendFontHeight), name)));
    }

    std::vector<juce::Rectangle<int>> TraceWindow::legendSlots (Kind which, int width) const
    {
        // Each entry as wide as its name and a reading at its widest, so a swing that
        // changes from frame to frame never moves the entries after it.
        // The widest either reading gets: a swing, or (in AC) a DC level, which can be negative.
        static const int readingWidth = std::max (nameWidth ("000 mV p-p"), nameWidth ("-000 mV DC"));
        constexpr int gapAfter = 16;

        std::vector<juce::Rectangle<int>> slots;
        int x = 0, y = 0;

        for (const auto& trace : listFor (which))
        {
            const int w = 8 + 6 + nameWidth (trace.name) + 6 + readingWidth;
            if (x > 0 && x + w > width)
            {
                x = 0;
                y += legendHeight;
            }

            slots.push_back ({ x, y, w, legendHeight });
            x += w + gapAfter;
        }

        return slots;
    }

    int TraceWindow::getPreferredHeight (int width) const
    {
        // The card's height with one row of legend, and a row more for each it wraps to: the
        // larger of the two tabs' legends, so switching tabs never resizes the window.
        int rows = 1;
        for (const auto which : { Kind::probes, Kind::scopes })
        {
            const auto slots = legendSlots (which, width - tabWidth - 2 * pad);
            if (! slots.empty())
                rows = juce::jmax (rows, slots.back().getBottom() / legendHeight);
        }

        return preferredHeight + (rows - 1) * legendHeight;
    }

    int TraceWindow::legendItemAt (juce::Point<int> p) const
    {
        for (const auto& [area, id] : legendItems)
            if (area.contains (p))
                return id;
        return 0;
    }

    void TraceWindow::setShown (int elementId, bool shouldShow)
    {
        if (shouldShow)
            hidden.erase (elementId);
        else
            hidden.insert (elementId);
        range.reset();
        refresh (false);
    }

    void TraceWindow::paint (juce::Graphics& g)
    {
        const auto ink = Theme::textOnPanel();
        const auto body = card().toFloat();
        const auto list = tabs();

        // The card and the shown kind's tab are one fill, as the palette and its tab are; the
        // other tab sits back, a shade lighter.
        juce::Path shape;
        shape.addRoundedRectangle (body, Theme::inspectorCornerRadius);

        for (size_t i = 0; i < list.size(); ++i)
        {
            const auto tab = tabArea (i).toFloat();
            juce::Path p;
            p.addRoundedRectangle (tab.getX(), tab.getY(), tab.getWidth() + 1.0f, tab.getHeight(), 8.0f, 8.0f, true, false, true, false);

            if (list[i] == kind)
                shape.addPath (p);
            else
            {
                g.setColour (Theme::panel().withMultipliedAlpha (0.78f));
                g.fillPath (p);
            }
        }

        g.setColour (Theme::panel());
        g.fillPath (shape);

        for (size_t i = 0; i < list.size(); ++i)
            SymbolPainter::drawPreview (g, symbolOf (list[i]), tabArea (i).toFloat().withTrimmedLeft (2.0f).reduced (3.0f),
                                        ink.withAlpha (list[i] == kind ? 0.9f : 0.5f));

        auto area = card().reduced (pad);
        auto header = area.removeFromTop (headerHeight);

        g.setColour (ink.withAlpha (0.55f));
        g.setFont (Fonts::bold (11.0f).withExtraKerningFactor (0.08f));
        g.drawText (kind == Kind::probes ? "PROBES" : "SCOPES", header.removeFromLeft (80), juce::Justification::centredLeft, false);

        // The span, between its steps.
        g.setColour (ink.withAlpha (0.85f));
        g.setFont (Fonts::light (12.5f));
        g.drawText (getSpanText(), spanArea, juce::Justification::centred, false);

        // The hide button: a chevron pointing down, where the window folds to.
        {
            const auto h = hideArea.toFloat();
            if (overHide)
            {
                g.setColour (Theme::pill());
                g.fillRoundedRectangle (h, 6.0f);
            }

            juce::Path chevron;
            const auto c = h.getCentre();
            chevron.startNewSubPath (c.x - 5.0f, c.y - 2.5f);
            chevron.lineTo (c.x, c.y + 2.5f);
            chevron.lineTo (c.x + 5.0f, c.y - 2.5f);
            g.setColour (ink.withAlpha (overHide ? 0.9f : 0.55f));
            g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        paintTraces (g, area);
    }

    void TraceWindow::frameDrawn()
    {
        if (! isShowing())
            return;

        // Read on the first frame past the interval; a few milliseconds' slack so a 60 Hz
        // display reads every third frame exactly, not now the third and now the fourth.
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (now - lastReadMs < 1000.0 / refreshesPerSecond - 4.0)
            return;

        lastReadMs = now;
        refresh();
    }

    void TraceWindow::refresh (bool readNew)
    {
        const auto& list = listFor (kind);
        const auto o = options[static_cast<int> (kind)];

        // Read every trace on the display's tick, unless paused; a kind not read yet is read
        // once even then.
        if (readings.size() != list.size())
        {
            readings.assign (list.size(), {});
            readingsStale = true;
        }

        if ((readNew && ! paused) || readingsStale)
        {
            for (size_t i = 0; i < list.size(); ++i)
            {
                auto& r = readings[i];
                if (reader == nullptr || ! reader (list[i].elementId, r))
                    r.live = false;
            }

            readingsStale = false;
        }

        // The span shown: the newest half of the ring, or from where the trigger says. The
        // traces of a kind advance together, so one start serves them all.
        int columns = 0;
        frame.firstShown = -1;
        frame.anyLive = false;

        for (size_t i = 0; i < list.size(); ++i)
        {
            if (! readings[i].live)
                continue;

            frame.anyLive = true;
            columns = std::max (columns, readings[i].count);
            if (frame.firstShown < 0 && isShown (list[i].elementId))
                frame.firstShown = static_cast<int> (i);
        }

        shown.columns = columns / 2;
        shown.from = columns - shown.columns;
        frame.triggerAt.reset();

        if (o.triggered && frame.firstShown >= 0 && shown.columns >= 2)
            if (const auto start = triggeredStart (readings[static_cast<size_t> (frame.firstShown)], shown.columns,
                                                   staleColumnsAllowed (windowSeconds[static_cast<int> (kind)], shown.columns)))
            {
                shown.from = *start;
                frame.triggerAt = shown.columns / 10;
            }

        // Each trace's swing and average over what is shown, and its offset: its average in
        // AC, so it sits about zero.
        frame.stats.assign (list.size(), {});
        shown.offsets.assign (list.size(), 0.0f);
        float lowest = 0.0f, highest = 0.0f;
        frame.anyShown = false;

        for (size_t i = 0; i < list.size(); ++i)
        {
            const auto& r = readings[i];
            if (! r.live || shown.columns < 2)
                continue;

            auto& st = frame.stats[i];
            double sum = 0.0;
            int counted = 0;
            st.low = std::numeric_limits<float>::max();
            st.high = std::numeric_limits<float>::lowest();

            // Only columns that hold readings: after a new span or a rebuild the ring fills
            // from the right, and the zeros before that are not 0 V.
            for (int k = std::max (0, r.firstFilled() - shown.from); k < shown.columns; ++k)
            {
                const int c = (r.writeColumn + shown.from + k) % r.count;
                st.low = std::min (st.low, r.minimum[c]);
                st.high = std::max (st.high, r.maximum[c]);
                sum += 0.5 * (r.minimum[c] + r.maximum[c]);
                ++counted;
            }

            if (counted == 0)
            {
                st = {};
                continue;
            }

            st.counted = true;
            st.mean = static_cast<float> (sum / counted);
            shown.offsets[i] = o.acCoupled ? st.mean : 0.0f;

            // A trace switched off takes no part in the range either.
            if (! isShown (list[i].elementId))
                continue;

            const float low = st.low - shown.offsets[i], high = st.high - shown.offsets[i];
            lowest = frame.anyShown ? std::min (lowest, low) : low;
            highest = frame.anyShown ? std::max (highest, high) : high;
            frame.anyShown = true;
        }

        // Held steady, so zero and the traces stay put while a wave's peaks vary frame to frame.
        if (frame.anyShown)
            range.update (lowest, highest, juce::Time::getMillisecondCounterHiRes());

        repaint();
    }

    void TraceWindow::paintTraces (juce::Graphics& g, juce::Rectangle<int> area)
    {
        // Draws what refresh() last worked out, and reads nothing: a repaint the sheet
        // beneath asks for shows the same frame, so the traces move at the display's pace
        // whatever else is redrawn.
        const auto ink = Theme::textOnPanel();
        const auto& list = listFor (kind);
        const auto o = options[static_cast<int> (kind)];
        const auto& stats = frame.stats;

        // The legend: a colour mark, the name and a reading, in ink, every entry in rows that
        // wrap (the window grows to hold them, getPreferredHeight). The reading is the swing,
        // or in AC the DC level, which the axis no longer shows. Each entry is a switch: a
        // trace switched off keeps its entry, its mark hollow and its words faint.
        {
            const auto slots = legendSlots (kind, area.getWidth());
            const int rows = slots.empty() ? 1 : slots.back().getBottom() / legendHeight;
            const auto legend = area.removeFromTop (rows * legendHeight);
            g.setFont (Fonts::light (legendFontHeight));
            legendItems.clear();

            for (size_t i = 0; i < list.size() && i < slots.size(); ++i)
            {
                const bool live = i < readings.size() && i < stats.size() && readings[i].live && stats[i].counted && shown.columns >= 2;
                const bool on = isShown (list[i].elementId);
                auto item = slots[i].translated (legend.getX(), legend.getY());
                legendItems.push_back ({ item, list[i].elementId });

                const auto mark = item.removeFromLeft (8).withSizeKeepingCentre (8, 8).toFloat();
                g.setColour (colourFor (static_cast<int> (i)));
                if (on)
                    g.fillRoundedRectangle (mark, 2.0f);
                else
                    g.drawRoundedRectangle (mark.reduced (0.75f), 2.0f, 1.5f);

                item.removeFromLeft (6);
                g.setColour (ink.withAlpha (on ? 1.0f : 0.4f));
                g.drawText (list[i].name, item.removeFromLeft (nameWidth (list[i].name)), juce::Justification::centredLeft, false);

                const auto reading = ! on    ? juce::String ("off")
                                   : ! live  ? juce::String ("--")
                                   : o.acCoupled ? volts (stats[i].mean) + " DC"
                                                 : volts (stats[i].high - stats[i].low) + " p-p";
                g.setColour (ink.withAlpha (on ? 0.6f : 0.4f));
                g.drawText (reading, item.withTrimmedLeft (6), juce::Justification::centredLeft, false);
            }
        }

        area.removeFromTop (6);

        const auto labelFont = Fonts::light (11.0f);
        const auto plot = area.withTrimmedLeft (52).toFloat();
        lastPlot = plot;

        if (! frame.anyShown)
        {
            g.setColour (ink.withAlpha (0.45f));
            g.setFont (labelFont);
            // Two different empties: nothing running yet, or everything switched off.
            const juce::String what = kind == Kind::probes ? "probe" : "scope";
            const auto text = frame.anyLive ? (list.size() == 1 ? "The " + what + " is off. Click its name above to show it."
                                                           : "Every " + what + " is off. Click a name above to show it.")
                                      : "Rebuild to see the " + what + "s.";
            g.drawFittedText (text, plot.toNearestInt(), juce::Justification::centred, 1);
            return;
        }

        const auto low = static_cast<float> (range.low), high = static_cast<float> (range.high);

        const auto toY = [&] (float v) { return plot.getBottom() - (v - low) / (high - low) * plot.getHeight(); };

        g.setFont (labelFont);
        for (const double t : ticksBetween (range.low, range.high))
        {
            const float y = toY ((float) t);
            g.setColour (ink.withAlpha (std::abs (t) < 1.0e-12 ? 0.25f : 0.08f));
            g.fillRect (plot.getX(), std::round (y), plot.getWidth(), 1.0f);
            g.setColour (ink.withAlpha (0.55f));
            g.drawText (volts (t), juce::Rectangle<float> ((float) area.getX(), y - 7.0f, plot.getX() - 6.0f - (float) area.getX(), 14.0f),
                        juce::Justification::centredRight, false);
        }

        // Ten divisions across, as a scope's graticule.
        for (int d = 0; d <= 10; ++d)
        {
            g.setColour (ink.withAlpha (d == 0 || d == 10 ? 0.25f : 0.08f));
            g.fillRect (std::round (plot.getX() + plot.getWidth() * (float) d / 10.0f), plot.getY(), 1.0f, plot.getHeight());
        }

        // Each trace, oldest column on the left, as one thin line down through every column's
        // swing and on to the next, no more than a point per point of the plot (the columns
        // beyond that are merged, their swing kept): where the wave is smooth the line is the
        // wave, and where it moved within a column the line covers how far.
        const juce::PathStrokeType stroke (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        const float columnWidth = plot.getWidth() / (float) (shown.columns - 1);
        const int stride = juce::jmax (1, static_cast<int> ((float) shown.columns / juce::jmax (1.0f, plot.getWidth())));

        for (size_t i = 0; i < list.size() && i < readings.size() && i < shown.offsets.size(); ++i)
        {
            const auto& r = readings[i];
            if (! r.live || ! isShown (list[i].elementId))
                continue;

            const float offset = shown.offsets[i];
            juce::Path line;
            line.preallocateSpace (6 * (shown.columns / stride + 1));

            // From its first reading: nothing is drawn where the ring has not filled yet.
            const int start = std::max (0, r.firstFilled() - shown.from);
            bool begun = false;

            for (int k = start; k < shown.columns; k += stride)
            {
                float most = std::numeric_limits<float>::lowest(), least = std::numeric_limits<float>::max();
                for (int j = k; j < std::min (shown.columns, k + stride); ++j)
                {
                    const int c = (r.writeColumn + shown.from + j) % r.count;
                    most = std::max (most, r.maximum[c]);
                    least = std::min (least, r.minimum[c]);
                }

                const float x = plot.getX() + columnWidth * (float) k;
                const float top = toY (most - offset), bottom = toY (least - offset);

                if (! begun)
                    line.startNewSubPath (x, top);
                else
                    line.lineTo (x, top);

                begun = true;

                if (bottom - top > 0.5f)
                    line.lineTo (x, bottom);
            }

            g.setColour (colourFor (static_cast<int> (i)));
            g.strokePath (line, stroke);
        }

        // Where it triggered: a small mark at the top, in the colour of the trace it watches.
        if (frame.triggerAt.has_value())
        {
            const float x = plot.getX() + columnWidth * (float) *frame.triggerAt;
            juce::Path mark;
            mark.addTriangle (x - 4.5f, plot.getY() - 6.0f, x + 4.5f, plot.getY() - 6.0f, x, plot.getY());
            g.setColour (colourFor (frame.firstShown));
            g.fillPath (mark);
        }

        paintCursor (g, plot, low, high);
    }

    //==========================================================================
    double SteadyRange::roundUp (double x)
    {
        static constexpr double steps[] = { 1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0 };
        const double decade = std::pow (10.0, std::floor (std::log10 (x)));

        for (const double step : steps)
            if (step * decade >= x * (1.0 - 1.0e-9))
                return step * decade;

        return 10.0 * decade;
    }

    std::pair<double, double> SteadyRange::fitted (double lowest, double highest, double headroom)
    {
        // Never less than 10 mV of swing, so a flat trace is not blown up into noise.
        const double swing = std::max (highest - lowest, 0.01);
        const bool aboutZero = (lowest <= 0.0 && highest >= 0.0) || std::min (std::abs (lowest), std::abs (highest)) < swing;

        if (aboutZero)
        {
            const double peak = roundUp (std::max ({ std::abs (lowest), std::abs (highest), 0.005 }) * (1.0 + headroom));
            return { -peak, peak };
        }

        const double low = lowest - headroom * swing, high = highest + headroom * swing;
        const double step = roundUp ((high - low) / 4.0);
        return { std::floor (low / step) * step, std::ceil (high / step) * step };
    }

    void SteadyRange::update (double lowest, double highest, double nowMs)
    {
        constexpr double tight = 0.05, roomy = 0.25;

        const auto adopt = [this] (std::pair<double, double> r)
        {
            low = r.first;
            high = r.second;
            held = true;
            shrinkingSince = -1.0;
        };

        // Out of the range, or nothing held yet: fit at once.
        if (! held || lowest < low || highest > high)
        {
            adopt (fitted (lowest, highest, tight));
            return;
        }

        // Inside it: shrink only once a smaller range would have held the traces, with room
        // to spare, for a while -- not on one quiet frame.
        const auto smaller = fitted (lowest, highest, roomy);
        if (smaller.second - smaller.first >= (high - low) * (1.0 - 1.0e-9))
        {
            shrinkingSince = -1.0;
            return;
        }

        if (shrinkingSince < 0.0)
            shrinkingSince = nowMs;
        else if (nowMs - shrinkingSince >= shrinkDelayMs)
            adopt (fitted (lowest, highest, tight));
    }

    //==========================================================================
    void TraceWindow::paintCursor (juce::Graphics& g, juce::Rectangle<float> plot, float lowest, float highest)
    {
        if (! cursor.has_value() || ! plot.contains (cursor->toFloat()) || shown.columns < 2)
            return;

        const auto ink = Theme::textOnPanel();
        const auto& list = listFor (kind);
        const float columnWidth = plot.getWidth() / (float) (shown.columns - 1);

        // Snapped to the column under the pointer: what was captured there, exactly.
        const int k = juce::jlimit (0, shown.columns - 1, juce::roundToInt (((float) cursor->x - plot.getX()) / columnWidth));
        const float x = plot.getX() + columnWidth * (float) k;

        g.setColour (ink.withAlpha (0.4f));
        g.fillRect (std::round (x), plot.getY(), 1.0f, plot.getHeight());

        const auto toY = [&] (float v) { return plot.getBottom() - (v - lowest) / (highest - lowest) * plot.getHeight(); };
        const float span = highest - lowest;

        struct Row
        {
            juce::Colour colour;
            juce::String text;
        };

        std::vector<Row> rows;
        int ring = 0;

        for (size_t i = 0; i < list.size() && i < readings.size() && i < shown.offsets.size(); ++i)
        {
            const auto& r = readings[i];
            if (! r.live || ! isShown (list[i].elementId))
                continue;

            ring = std::max (ring, r.count);

            // No reading there yet: nothing to say for this trace.
            if (shown.from + k < r.firstFilled())
                continue;

            const int c = (r.writeColumn + shown.from + k) % r.count;
            const float offset = shown.offsets[i];
            const float lo = r.minimum[c] - offset, hi = r.maximum[c] - offset;
            const auto colour = colourFor (static_cast<int> (i));

            // A column that swung through more than a little of the scale reads as its
            // range; otherwise as the one value it held. As drawn: about its average in AC.
            const auto value = hi - lo > 0.02f * span ? volts (lo) + " to " + volts (hi) : volts (0.5 * (lo + hi));
            rows.push_back ({ colour, list[i].name + "  " + value });

            const juce::Point<float> at (x, toY (0.5f * (lo + hi)));
            g.setColour (Theme::panel());
            g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (at));
            g.setColour (colour);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (at));
        }

        if (rows.empty())
            return;

        // When, counted back from the newest column the ring holds.
        const int age = shown.from + k;
        const double ago = windowSeconds[static_cast<int> (kind)] * (double) (ring - 1 - age) / (double) shown.columns;
        const juce::String heading = ago <= 0.0 ? juce::String ("Now") : "-" + timeText (ago);

        // The box may float over the legend and the header, but never past the card: as
        // many rows as the card is tall, and the rest in further columns beside them.
        constexpr float headingHeight = 16.0f, rowHeight = 15.0f, padX = 8.0f, padY = 5.0f, columnGap = 14.0f;
        const auto room = card().toFloat().reduced (6.0f);
        const int fit = juce::jmax (1, (int) ((room.getHeight() - 2.0f * padY - headingHeight) / rowHeight));
        const int columnsNeeded = (static_cast<int> (rows.size()) + fit - 1) / fit;
        const int perColumn = (static_cast<int> (rows.size()) + columnsNeeded - 1) / columnsNeeded; // balanced

        const auto font = Fonts::light (11.0f);
        float rowWidth = 0.0f;
        for (const auto& r : rows)
            rowWidth = juce::jmax (rowWidth, 14.0f + juce::GlyphArrangement::getStringWidth (font, r.text));

        const float contentWidth = juce::jmax (juce::GlyphArrangement::getStringWidth (Fonts::bold (11.0f), heading),
                                               (float) columnsNeeded * rowWidth + (float) (columnsNeeded - 1) * columnGap);
        const float width = contentWidth + 2.0f * padX;
        const float height = 2.0f * padY + headingHeight + rowHeight * (float) perColumn;

        // Beside the line, on whichever side it fits, kept inside the card.
        const float bx = x + 12.0f + width <= room.getRight() ? x + 12.0f : x - 12.0f - width;
        const float by = juce::jmin (plot.getY() + 4.0f, room.getBottom() - height);
        const auto box = juce::Rectangle<float> (juce::jlimit (room.getX(), juce::jmax (room.getX(), room.getRight() - width), bx),
                                                 juce::jmax (room.getY(), by), width, height);

        g.setColour (Theme::panel());
        g.fillRoundedRectangle (box, 6.0f);
        g.setColour (ink.withAlpha (0.15f));
        g.drawRoundedRectangle (box.reduced (0.5f), 6.0f, 1.0f);

        auto inner = box.reduced (padX, padY);
        g.setColour (ink);
        g.setFont (Fonts::bold (11.0f));
        g.drawText (heading, inner.removeFromTop (headingHeight), juce::Justification::centredLeft, false);

        g.setFont (font);
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const int column = static_cast<int> (i) / perColumn, row = static_cast<int> (i) % perColumn;
            auto line = juce::Rectangle<float> (inner.getX() + (float) column * (rowWidth + columnGap),
                                                inner.getY() + (float) row * rowHeight, rowWidth, rowHeight);
            g.setColour (rows[i].colour);
            g.fillRoundedRectangle (line.removeFromLeft (8.0f).withSizeKeepingCentre (8.0f, 8.0f), 2.0f);
            line.removeFromLeft (6.0f);
            g.setColour (ink);
            g.drawText (rows[i].text, line, juce::Justification::centredLeft, false);
        }
    }

    //==========================================================================
    TraceWindow::HeaderToggle::HeaderToggle (const juce::String& word, const juce::String& tip, bool isPause)
        : juce::Button (word.isNotEmpty() ? word : juce::String ("Pause")), text (word), pauseGlyph (isPause)
    {
        setTooltip (tip);
        setWantsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    int TraceWindow::HeaderToggle::getPreferredWidth() const
    {
        return pauseGlyph ? 24 : juce::roundToInt (std::ceil (juce::GlyphArrangement::getStringWidth (Fonts::light (11.5f), text))) + 18;
    }

    void TraceWindow::HeaderToggle::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        const auto bounds = getLocalBounds().toFloat();
        const bool on = getToggleState();

        // Lit as the palette's armed entry is; else bare, the pill only under the pointer.
        if (on || highlighted || down)
        {
            g.setColour (on ? Theme::accent() : down ? Theme::pill().darker (0.08f) : Theme::pill());
            g.fillRoundedRectangle (bounds, Theme::paletteEntryCornerRadius + 2.0f);
        }

        const auto ink = on ? Theme::text() : Theme::textOnPanel().withAlpha (highlighted || down ? 0.95f : 0.7f);
        g.setColour (ink);

        if (! pauseGlyph)
        {
            g.setFont (Fonts::light (11.5f));
            g.drawText (text, getLocalBounds(), juce::Justification::centred, false);
            return;
        }

        // Two bars, or once paused, the triangle that runs it again.
        const auto c = bounds.getCentre();
        if (on)
        {
            juce::Path play;
            play.addTriangle (c.x - 3.5f, c.y - 5.0f, c.x - 3.5f, c.y + 5.0f, c.x + 5.0f, c.y);
            g.fillPath (play);
        }
        else
        {
            g.fillRoundedRectangle (juce::Rectangle<float> (2.4f, 10.0f).withCentre ({ c.x - 2.6f, c.y }), 1.0f);
            g.fillRoundedRectangle (juce::Rectangle<float> (2.4f, 10.0f).withCentre ({ c.x + 2.6f, c.y }), 1.0f);
        }
    }

    //==========================================================================
    TraceWindowButton::TraceWindowButton() : juce::Button ("Show traces")
    {
        setWantsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void TraceWindowButton::setContents (bool probes, bool scopes)
    {
        withProbes = probes;
        withScopes = scopes;
        setTooltip ("Opens the " + text().fromFirstOccurrenceOf ("Show ", false, false) + " screen");
        repaint();
    }

    juce::String TraceWindowButton::text() const
    {
        return withProbes && withScopes ? "Show probes and scopes" : withScopes ? "Show scopes" : "Show probes";
    }

    int TraceWindowButton::getPreferredWidth() const
    {
        const int icons = (withProbes ? 1 : 0) + (withScopes ? 1 : 0);
        return 14 + icons * 24 + 6 + juce::roundToInt (std::ceil (juce::GlyphArrangement::getStringWidth (Fonts::light (12.0f), text()))) + 20;
    }

    void TraceWindowButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        const auto ink = Theme::textOnPanel();
        const auto bounds = getLocalBounds().toFloat();

        g.setColour (down ? Theme::pill() : highlighted ? Theme::panel().darker (0.03f) : Theme::panel());
        g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);

        auto area = getLocalBounds().reduced (14, 0);

        if (withProbes)
            SymbolPainter::drawPreview (g, SchematicModel::ElementType::Probe, area.removeFromLeft (24).toFloat(), ink);
        if (withScopes)
            SymbolPainter::drawPreview (g, SchematicModel::ElementType::Scope, area.removeFromLeft (24).toFloat(), ink);

        area.removeFromLeft (6);
        g.setColour (ink);
        g.setFont (Fonts::light (12.0f));
        g.drawText (text(), area, juce::Justification::centredLeft, false);
    }
} // namespace Celine

