#include "realtime_guard.h"

#include <juce_core/juce_core.h>

#include <cstdlib>
#include <new>

#if JUCE_WINDOWS
 #include <malloc.h>
#endif

namespace
{
    // Per-thread, so only the thread under test is watched.
    thread_local int watching = 0;
    thread_local RealtimeCheck::Counts counts;

    bool armed = false;

    struct Arm
    {
        Arm() { armed = true; }
    };

    const Arm arm;
}

namespace RealtimeCheck
{
    bool isArmed() noexcept { return armed; }

    void beginWatching() noexcept
    {
        if (watching++ == 0)
            counts = {};
    }

    Counts endWatching() noexcept
    {
        if (watching > 0)
            --watching;

        return counts;
    }

    void makeOneAllocation()
    {
        // Size and pointer go through volatiles so the optimiser can't elide the new/delete
        // pair.
        static volatile std::size_t count = 64;
        static void* volatile escape = nullptr;

        escape = new int[count];

        delete[] static_cast<int*> (const_cast<void*> (escape));
        escape = nullptr;
    }
}

namespace
{
    void* allocate (std::size_t size)
    {
        if (watching > 0)
        {
            ++counts.allocations;
            counts.bytes += size;
        }

        // Zero-sized allocations still return a distinct pointer.
        if (auto* p = std::malloc (size == 0 ? 1 : size))
            return p;

        return nullptr;
    }

    void release (void* p) noexcept
    {
        if (p != nullptr && watching > 0)
            ++counts.deallocations;

        std::free (p);
    }

    // Over-aligned allocation has no portable spelling: Windows needs
    // _aligned_malloc/_aligned_free, and MSVC has no std::aligned_alloc.
    void* alignedAllocate (std::size_t bytes, std::size_t alignment)
    {
       #if JUCE_WINDOWS
        return _aligned_malloc (bytes, alignment);
       #else
        void* p = nullptr;
        return posix_memalign (&p, alignment, bytes) == 0 ? p : nullptr;
       #endif
    }

    void alignedRelease (void* p) noexcept
    {
        if (p != nullptr && watching > 0)
            ++counts.deallocations;

       #if JUCE_WINDOWS
        _aligned_free (p);
       #else
        std::free (p);
       #endif
    }
}

void* operator new (std::size_t size)
{
    if (auto* p = allocate (size))
        return p;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    if (auto* p = allocate (size))
        return p;

    throw std::bad_alloc();
}

void* operator new (std::size_t size, const std::nothrow_t&) noexcept { return allocate (size); }
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept { return allocate (size); }

void operator delete (void* p) noexcept { release (p); }
void operator delete[] (void* p) noexcept { release (p); }
void operator delete (void* p, std::size_t) noexcept { release (p); }
void operator delete[] (void* p, std::size_t) noexcept { release (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept { release (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { release (p); }

// Aligned forms, used by over-aligned types and JUCE's SIMD types.
void* operator new (std::size_t size, std::align_val_t align)
{
    if (watching > 0)
    {
        ++counts.allocations;
        counts.bytes += size;
    }

    if (auto* p = alignedAllocate (size == 0 ? 1 : size,
                                   juce::jmax (sizeof (void*), static_cast<std::size_t> (align))))
        return p;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size, std::align_val_t align) { return operator new (size, align); }

void operator delete (void* p, std::align_val_t) noexcept { alignedRelease (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { alignedRelease (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { alignedRelease (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { alignedRelease (p); }
