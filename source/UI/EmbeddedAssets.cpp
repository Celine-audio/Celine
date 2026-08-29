#include "EmbeddedAssets.h"

#include <BinaryData.h>

namespace SchematicUI::Assets
{
    const char* find(const juce::String& filename, int& sizeInBytes)
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
        // leaving a blank button to be discovered by eye.
        jassertfalse;
        return nullptr;
    }

    std::unique_ptr<juce::Drawable> drawable(const juce::String& filename)
    {
        int size = 0;

        if (const auto* data = find(filename, size); data != nullptr && size > 0)
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
} // namespace SchematicUI::Assets
