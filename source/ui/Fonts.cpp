#include "Fonts.h"

#include <BinaryData.h>

#include <array>

namespace Celine::Fonts
{
    namespace
    {
        const char* fileFor(Weight weight)
        {
            switch (weight)
            {
                case Weight::Light: return "Jura-Light.ttf";
                case Weight::Bold:  return "Jura-Bold.ttf";
                case Weight::Mono:  return "JetBrainsMono-Regular.ttf";
                case Weight::Logo:  return "NicoMoji-Regular.ttf";
            }

            return "Jura-Light.ttf";
        }

        /** Found by original filename, not by the identifier JUCE derives from
            it -- the same rule the element artwork follows, and for the same
            reason: the derivation drops and mangles characters, and getting it
            wrong fails silently. */
        juce::Typeface::Ptr load(const char* filename)
        {
            for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
            {
                const auto* resource = BinaryData::namedResourceList[i];

                if (juce::String(filename) != BinaryData::getNamedResourceOriginalFilename(resource))
                    continue;

                int size = 0;

                if (const auto* data = BinaryData::getNamedResource(resource, size);
                    data != nullptr && size > 0)
                    return juce::Typeface::createSystemTypefaceFor(data, (size_t) size);

                break;
            }

            return {};
        }

        //======================================================================
        /**
            The parsed typefaces, kept for the life of the library.

            A `DeletedAtShutdown` rather than a function-local static, and that
            is not a style preference. A static `Typeface::Ptr` is released on the
            way out of main, *after* JUCE has torn down the font subsystem the
            typeface belongs to, and the process then dies with "mutex lock
            failed: Invalid argument" from inside the C++ runtime: a crash on
            exit, long after the last useful line ran, with a message that says
            nothing about fonts. This hands the objects back while JUCE is still
            up. It cost one confusing crash to find out.

            Message thread only -- nothing in the audio path draws text.
        */
        struct FontCache final : public juce::DeletedAtShutdown
        {
            static FontCache& get()
            {
                if (instance == nullptr)
                    instance = new FontCache();

                return *instance;
            }

            ~FontCache() override { instance = nullptr; }

            /** `tried` matters as much as the pointer: a weight that is not
                embedded must not be re-parsed on every repaint just because the
                answer came back null. */
            std::array<juce::Typeface::Ptr, 4> faces {};
            std::array<bool, 4> tried { false, false, false, false };

            static FontCache* instance;
        };

        FontCache* FontCache::instance = nullptr;
    } // namespace

    juce::Typeface::Ptr typeface(Weight weight)
    {
        auto& cache = FontCache::get();
        const auto slot = static_cast<size_t>(weight);

        if (! cache.tried[slot])
        {
            cache.tried[slot] = true;
            cache.faces[slot] = load(fileFor(weight));
        }

        return cache.faces[slot];
    }

    juce::Font logo(float heightInPixels)
    {
        if (auto face = typeface(Weight::Logo))
            return juce::Font(juce::FontOptions(face).withHeight(heightInPixels));

        return bold(heightInPixels);
    }

    juce::Font font(Weight weight, float heightInPixels)
    {
        if (auto face = typeface(weight))
            return juce::Font(juce::FontOptions(face).withHeight(heightInPixels));

        // Not embedded. JUCE's default is wrong-looking but readable, which beats
        // refusing to draw.
        return juce::Font(juce::FontOptions(heightInPixels));
    }
} // namespace Celine::Fonts
