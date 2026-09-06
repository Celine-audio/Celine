#include "EmbeddedAssets.h"

#include <algorithm>
#include <limits>
#include <vector>

#include <BinaryData.h>

namespace Celine::Assets
{
    const char* find(const juce::String& filename, int& sizeInBytes, IfMissing ifMissing)
    {
        sizeInBytes = 0;

        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            const auto* resource = BinaryData::namedResourceList[i];

            if (filename != BinaryData::getNamedResourceOriginalFilename(resource))
                continue;

            return BinaryData::getNamedResource(resource, sizeInBytes);
        }

        // Nothing draws without artwork, so say so in a debug build rather than
        // leaving a blank button to be discovered by eye -- unless the caller has
        // said the file is optional and has something to show without it.
        jassert(ifMissing == IfMissing::returnNull);
        juce::ignoreUnused(ifMissing);
        return nullptr;
    }

    std::unique_ptr<juce::Drawable> drawable(const juce::String& filename, IfMissing ifMissing)
    {
        int size = 0;

        if (const auto* data = find(filename, size, ifMissing); data != nullptr && size > 0)
            return juce::Drawable::createFromImageData(data, (size_t) size);

        return {};
    }

    void tint(juce::Drawable& drawable, juce::Colour colour)
    {
        if (auto* path = dynamic_cast<juce::DrawablePath*>(&drawable))
        {
            const auto fill = path->getFill();

            if (! (fill.isColour() && fill.colour.isTransparent()))
                path->setFill(colour);

            if (path->getStrokeType().getStrokeThickness() > 0.0f)
                path->setStrokeFill(colour);
        }

        // By index: JUCE 9 stopped deriving Drawable from Component, so there is
        // no Component::getChildren() to walk and no downcast to do.
        if (auto* composite = dynamic_cast<juce::DrawableComposite*>(&drawable))
            for (int i = 0; i < composite->getNumChildren(); ++i)
                tint(composite->getChild(i), colour);
    }

    //==========================================================================
    namespace
    {
        /** How far below the centre of `drawable`'s bounding box its letters are
            actually centred, as a proportion of that box's height.

            The baseline is taken as the *median* of the glyph bottoms, not the
            lowest: descenders are the minority by construction -- that is what makes
            them descenders -- so the middle value is the line the word stands on
            however many of them there are. */
        /** The band an ordinary letter occupies: baseline to x-height.

            Both edges are medians rather than extremes, and that is the whole point. An
            "l" reaches higher than the rest and a "g" drops below them, so the ink box a
            wordmark reports is taller than the letters actually look -- by different
            amounts in different words. Measuring from the extremes made "gallery",
            which has both, sit differently from "aura", which has neither, and read as
            a different size at the same nominal height.

            Returns false when there are too few glyphs to take a median of, which is
            the case for a wordmark drawn as one merged path. */
        bool xHeightBand(juce::Drawable& drawable, float& top, float& baseline)
        {
            auto* composite = dynamic_cast<juce::DrawableComposite*>(&drawable);

            if (composite == nullptr || composite->getNumChildren() < 2)
                return false;

            std::vector<float> tops, bottoms;

            for (int i = 0; i < composite->getNumChildren(); ++i)
            {
                const auto glyph = composite->getChild(i).getDrawableBounds();

                if (glyph.isEmpty())
                    continue;

                tops.push_back(glyph.getY());
                bottoms.push_back(glyph.getBottom());
            }

            if (bottoms.size() < 2)
                return false;

            const auto middleOf = [](std::vector<float>& values)
            {
                const auto middle = values.begin() + (std::ptrdiff_t)(values.size() / 2);
                std::nth_element(values.begin(), middle, values.end());
                return *middle;
            };

            top = middleOf(tops);
            baseline = middleOf(bottoms);

            return baseline > top;
        }

        float opticalCentreOffset(juce::Drawable& drawable)
        {
            float top = 0.0f, baseline = 0.0f;

            if (! xHeightBand(drawable, top, baseline))
                return 0.0f;

            const auto whole = drawable.getDrawableBounds();

            if (whole.getHeight() <= 0.0f)
                return 0.0f;

            return (whole.getCentreY() - 0.5f * (top + baseline)) / whole.getHeight();
        }
    }

    float xHeightFraction(juce::Drawable& drawable)
    {
        float top = 0.0f, baseline = 0.0f;

        if (! xHeightBand(drawable, top, baseline))
            return 1.0f;

        const auto whole = drawable.getDrawableBounds();

        if (whole.getHeight() <= 0.0f)
            return 1.0f;

        return (baseline - top) / whole.getHeight();
    }

    void drawWordmark(juce::Graphics& g, juce::Drawable& drawable, juce::Rectangle<float> area)
    {
        const auto whole = drawable.getDrawableBounds();

        if (whole.isEmpty() || area.isEmpty())
            return;

        // drawWithin preserves the aspect ratio, so the artwork is only as tall as
        // the narrower of the two fits allow. The shift has to be a proportion of
        // *that*, not of the area it was given, or a wide slot would push the word
        // off its own baseline.
        const auto scale = juce::jmin(area.getWidth() / whole.getWidth(),
                                      area.getHeight() / whole.getHeight());

        const auto shift = opticalCentreOffset(drawable) * whole.getHeight() * scale;

        drawable.drawWithin(g, area.translated(0.0f, shift),
                            juce::RectanglePlacement::centred, 1.0f);
    }
} // namespace Celine::Assets
