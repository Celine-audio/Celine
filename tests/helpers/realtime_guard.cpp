#include "realtime_guard.h"

#include <juce_core/juce_core.h>

#include <cstdlib>
#include <new>

#if JUCE_WINDOWS
 #include <malloc.h>
#endif

namespace
{
    // Per-thread, so only the thread under test is watched and the harness around it
    // can allocate freely.
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
        // Both the size and the pointer go through volatiles, and that is not
        // decoration. A release build is allowed to replace a new/delete pair with a
        // stack slot whenever it can bound the size and see the whole lifetime, and it
        // does -- the first two attempts at this test failed against a guard that was
        // working, which is the most misleading result a self-check can give. A size it
        // cannot fold and a pointer it cannot prove dies here leave it nothing to elide.
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

        // Zero-sized allocations still have to return a distinct pointer.
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

    // Over-aligned allocation is the one part of this with no portable spelling.
    // posix_memalign does not exist on Windows, and the memory _aligned_malloc returns
    // there must go back through _aligned_free rather than free -- handing it to the
    // ordinary one is heap corruption rather than a diagnostic. std::aligned_alloc is
    // no answer either: MSVC's runtime has never provided it.
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

// Aligned forms, which std::vector of an over-aligned type and JUCE's SIMD types use.
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
