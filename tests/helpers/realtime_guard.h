#pragma once

#include <atomic>
#include <cstddef>

/** Catches allocation on the audio thread.

    Global operator new and delete are replaced (realtime_guard.cpp) with versions that
    count calls while a guard is in scope, per thread. Only linked into the test binary.

    Usage:

        RealtimeGuard guard;
        processor.processBlock (buffer, midi);
        CHECK (guard.allocations() == 0); */
namespace RealtimeCheck
{
    /** Allocation counters for the calling thread. */
    struct Counts
    {
        std::size_t allocations = 0;
        std::size_t deallocations = 0;
        std::size_t bytes = 0;
    };

    /** True when this build actually replaced the allocator. */
    bool isArmed() noexcept;

    void beginWatching() noexcept;
    Counts endWatching() noexcept;

    /** Allocates once and frees it, so a test can prove the guard sees it. In its own
        translation unit so the compiler can't elide the pair. */
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
