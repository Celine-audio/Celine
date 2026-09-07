#pragma once

#include <atomic>
#include <cstddef>

/**
    Catches the things a plugin must never do on the audio thread.

    A DAW calls processBlock from a thread with a hard deadline: a few milliseconds,
    every few milliseconds, for as long as the session is open. Miss it once and the
    user hears a click. Allocation is the usual way to miss it -- malloc can take a lock
    held by another thread, and under memory pressure it can go to the kernel -- so the
    rule is that the audio thread does not allocate, and this is how that rule gets
    checked rather than asserted.

    Global operator new and delete are replaced (in realtime_guard.cpp) with versions
    that count what happens while a guard is in scope. The count is per-thread, so tests
    running on the message thread are unaffected, and the replacement is only linked into
    the test binary -- the plugin itself is untouched.

    Usage:

        RealtimeGuard guard;
        processor.processBlock (buffer, midi);
        CHECK (guard.allocations() == 0);
*/
namespace RealtimeCheck
{
    /** Allocation counters for the calling thread. */
    struct Counts
    {
        std::size_t allocations = 0;
        std::size_t deallocations = 0;
        std::size_t bytes = 0;
    };

    /** True when this build actually replaced the allocator. If the platform did not
        let us, tests say so rather than passing silently. */
    bool isArmed() noexcept;

    void beginWatching() noexcept;
    Counts endWatching() noexcept;

    /** Allocates once and frees it again, so a test can prove the guard sees it.

        Lives in the guard's own translation unit deliberately. A compiler is allowed to
        elide a new/delete pair whose whole lifetime it can see, and a release build does
        exactly that -- which made the self-test fail while the guard was working
        perfectly, the most misleading result a check like this can give. Across a
        translation unit boundary, with no LTO, there is nothing to elide. */
    void makeOneAllocation();
}

/** Scoped form of the above. */
class RealtimeGuard
{
public:
    RealtimeGuard() { RealtimeCheck::beginWatching(); }

    ~RealtimeGuard() { if (! stopped) RealtimeCheck::endWatching(); }

    /** Stops watching and returns what happened. Safe to call once. */
    RealtimeCheck::Counts stop() noexcept
    {
        stopped = true;
        return RealtimeCheck::endWatching();
    }

    std::size_t allocations() noexcept { return stop().allocations; }

    RealtimeGuard (const RealtimeGuard&) = delete;
    RealtimeGuard& operator= (const RealtimeGuard&) = delete;

private:
    bool stopped = false;
};
