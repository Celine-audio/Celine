#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** How things move in this window: one duration and one curve, everywhere.

    Every panel that comes, goes, folds or grows -- the palette folding, the inspector
    resizing, the probes' screen opening, the floating actions -- moves with a Tween, so
    they all feel like one thing. The rules the numbers keep:

    - Quick. 220 ms is long enough to see where a panel went and short enough that nobody
      waits for it; nothing ever holds input back while it plays.
    - Eased in and out (a cubic), so nothing starts or stops with a jolt.
    - Interruptible. Asking again mid-move sets off from where it is, never from the end.
    - Never on a window nobody sees: off screen (the tests, the plugin's manifest helper)
      a change is applied at once, since no display refresh would come to drive it. */
namespace Celine::Motion
{
    inline constexpr double durationMs = 220.0;

    /** Cubic ease in and out, 0..1 to 0..1. */
    inline float ease (float t) noexcept
    {
        t = juce::jlimit (0.0f, 1.0f, t);
        return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow (-2.0f * t + 2.0f, 3.0f) / 2.0f;
    }

    /** A value on its way to a target, eased. Its owner ticks it from a VBlankAttachment and
        lays out again while it moves. */
    class Tween
    {
    public:
        explicit Tween (float initial = 0.0f) noexcept : from (initial), to (initial), value (initial) {}

        /** Heads for `target`: over the shared duration when `animate`, else at once. */
        void setTarget (float target, bool animate) noexcept
        {
            if (! animate)
            {
                from = to = value = target;
                moving = false;
                return;
            }

            if (juce::exactlyEqual (target, to) && (moving || juce::exactlyEqual (value, to)))
                return;

            from = value;
            to = target;
            startMs = juce::Time::getMillisecondCounterHiRes();
            moving = true;
        }

        /** Advances to now. True if it moved this frame. */
        bool tick() noexcept
        {
            if (! moving)
                return false;

            const auto t = static_cast<float> ((juce::Time::getMillisecondCounterHiRes() - startMs) / durationMs);
            value = from + (to - from) * ease (t);

            if (t >= 1.0f)
            {
                value = to;
                moving = false;
            }

            return true;
        }

        float get() const noexcept { return value; }
        float target() const noexcept { return to; }
        bool isMoving() const noexcept { return moving; }

    private:
        float from, to, value;
        double startMs = 0.0;
        bool moving = false;
    };
} // namespace Celine::Motion
