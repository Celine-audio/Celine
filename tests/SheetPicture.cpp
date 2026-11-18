#include "helpers/test_helpers.h"

#include <PluginProcessor.h>
#include <Schematic/ExampleSchematics.h>
#include <ui/SheetPicture.h>

#include <CelineUI/Theme.h>

#include <catch2/catch_test_macros.hpp>

// The picture Quick Look shows of a .celsch: the sheet as the canvas draws it.

using namespace SchematicModel;

TEST_CASE ("A sheet file becomes the picture Quick Look shows", "[preview]")
{
    juce::TemporaryFile temporary (".celsch");
    const auto file = temporary.getFile();

    for (int example = 0; example < Examples::getNames().size(); ++example)
    {
        INFO (Examples::getNames()[example]);

        PluginProcessor plugin;
        Examples::load (plugin.getSchematic(), example);
        REQUIRE (plugin.createDocument().createXml()->writeTo (file));

        juce::MemoryBlock png;
        int width = 0, height = 0;
        REQUIRE (Celine::SheetPicture::previewPng (file, png, width, height));

        // The drawing's own shape, 1200 points along its long side, at twice that in pixels.
        CHECK (juce::jmax (width, height) == 1200);
        CHECK (juce::jmin (width, height) >= 600);

        // PREVIEW_KEEP names a folder to keep each picture in, to look at.
        if (const auto keep = juce::SystemStats::getEnvironmentVariable ("PREVIEW_KEEP", {}); keep.isNotEmpty())
            juce::File (keep).getChildFile ("example" + juce::String (example) + ".png").replaceWithData (png.getData(), png.getSize());

        const auto image = juce::ImageFileFormat::loadFrom (png.getData(), png.getSize());
        REQUIRE (image.isValid());
        CHECK (image.getWidth() == 2 * width);
        CHECK (image.getHeight() == 2 * height);

        // The sheet's ground, with the circuit drawn on it: a good share of the pixels are
        // something else (wires, parts, captions, the grid's dots).
        const auto ground = Celine::Theme::background();
        int drawn = 0, sampled = 0;
        for (int y = 0; y < image.getHeight(); y += 3)
            for (int x = 0; x < image.getWidth(); x += 3)
            {
                ++sampled;
                if (image.getPixelAt (x, y) != ground)
                    ++drawn;
            }

        CHECK (image.getPixelAt (2, 2) == ground);
        CHECK (drawn > sampled / 100);
    }
}

TEST_CASE ("Anything that is not a sheet has no picture", "[preview]")
{
    juce::TemporaryFile temporary (".celsch");
    const auto file = temporary.getFile();
    juce::MemoryBlock png;
    int width = 0, height = 0;

    REQUIRE (file.replaceWithText ("not a sheet"));
    CHECK_FALSE (Celine::SheetPicture::previewPng (file, png, width, height));

    REQUIRE (file.replaceWithText ("<OTHERDOCUMENT><SCHEMATIC/></OTHERDOCUMENT>"));
    CHECK_FALSE (Celine::SheetPicture::previewPng (file, png, width, height));

    CHECK_FALSE (Celine::SheetPicture::previewPng (juce::File(), png, width, height));
}
