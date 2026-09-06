#include <ui/Theme.h>
#include <ui/ThemePalette.h>

#include <catch2/catch_test_macros.hpp>

#include <set>

/*
    The theming engine. Two things are worth protecting here and neither is visible by
    looking at the window: that a theme somebody shares survives the round trip through
    a file, and that the list every part of the engine is generated from has no two
    roles claiming the same key.
*/

using namespace Celine;

namespace
{
    /** Puts the palette back however the test leaves, so one case cannot colour the
        next -- there is one palette per process and these all share it. */
    struct ShippedPalette
    {
        ShippedPalette() { Theme::palette().reset(); }
        ~ShippedPalette() { Theme::palette().reset(); }
    };
}

//==============================================================================
TEST_CASE ("Every role has its own key, label and group", "[theme]")
{
    // The keys are what a .celthm file is written and read by, so two roles sharing one
    // would quietly make them a single colour -- saved from whichever was written last,
    // and loaded into both. A copy-and-paste in ThemeRoles.h is all it takes.
    std::set<juce::String> keys;

    for (size_t i = 0; i < Theme::numRoles; ++i)
    {
        const auto& entry = Theme::info()[i];

        INFO ("role " << i << " is " << entry.key);

        CHECK (juce::String (entry.key).isNotEmpty());
        CHECK (juce::String (entry.label).isNotEmpty());
        CHECK (juce::String (entry.group).isNotEmpty());
        CHECK (keys.insert (entry.key).second);
    }

    CHECK (keys.size() == Theme::numRoles);
}

TEST_CASE ("The shipped colours are what the accessors answer", "[theme]")
{
    ShippedPalette shipped;

    CHECK (Theme::palette().isShipped());
    CHECK (Theme::chrome() == juce::Colour (0xff3b334b));
    CHECK (Theme::text() == juce::Colour (0xfff9fbff));
}

TEST_CASE ("A theme survives the round trip through a file", "[theme]")
{
    ShippedPalette shipped;
    auto& palette = Theme::palette();

    palette.set (Theme::Role::chrome, juce::Colour (0xff102030));
    palette.set (Theme::Role::accent, juce::Colour (0xffabcdef));
    palette.setName ("Round trip");

    juce::TemporaryFile temporary (Theme::Palette::fileExtension);
    REQUIRE (palette.saveTo (temporary.getFile()).wasOk());

    palette.reset();
    REQUIRE (Theme::chrome() == juce::Colour (0xff3b334b));

    REQUIRE (palette.loadFrom (temporary.getFile()).wasOk());

    CHECK (Theme::chrome() == juce::Colour (0xff102030));
    CHECK (Theme::accent() == juce::Colour (0xffabcdef));
    CHECK (palette.getName() == "Round trip");
    CHECK_FALSE (palette.isShipped());
}

TEST_CASE ("A theme file forgives what it can and refuses what it cannot", "[theme]")
{
    ShippedPalette shipped;
    auto& palette = Theme::palette();

    SECTION ("a key this build does not know is ignored")
    {
        // Which is what lets a theme written by a plugin with more colours than this one
        // still load. GALLERY has cabinets; SPACE has none, and must not choke on them.
        juce::var parsed;
        REQUIRE (juce::JSON::parse (R"({"colours":{"chrome":"#112233","somethingElse":"#445566"}})",
                                    parsed).wasOk());

        CHECK (palette.fromVar (parsed).wasOk());
        CHECK (Theme::chrome() == juce::Colour (0xff112233));
    }

    SECTION ("a key the file omits comes back shipped, not left as it was")
    {
        palette.set (Theme::Role::accent, juce::Colour (0xff00ff00));

        juce::var parsed;
        REQUIRE (juce::JSON::parse (R"({"colours":{"chrome":"#112233"}})", parsed).wasOk());
        REQUIRE (palette.fromVar (parsed).wasOk());

        // Loading a theme has to give the same result whatever was loaded before it.
        CHECK (Theme::accent() == juce::Colour (Theme::info()[(size_t) Theme::Role::accent].shipped));
    }

    SECTION ("hex is read the way people write it")
    {
        for (const auto* spelling : { R"({"colours":{"chrome":"#112233"}})",
                                      R"({"colours":{"chrome":"112233"}})",
                                      R"({"colours":{"chrome":"  #112233  "}})" })
        {
            juce::var parsed;
            REQUIRE (juce::JSON::parse (spelling, parsed).wasOk());
            REQUIRE (palette.fromVar (parsed).wasOk());

            INFO (spelling);
            CHECK (Theme::chrome() == juce::Colour (0xff112233));
        }
    }

    SECTION ("three digits mean what they do everywhere else")
    {
        juce::var parsed;
        REQUIRE (juce::JSON::parse (R"({"colours":{"chrome":"#abc"}})", parsed).wasOk());
        REQUIRE (palette.fromVar (parsed).wasOk());

        CHECK (Theme::chrome() == juce::Colour (0xffaabbcc));
    }

    SECTION ("nonsense in a value leaves that colour shipped rather than black")
    {
        juce::var parsed;
        REQUIRE (juce::JSON::parse (R"({"colours":{"chrome":"not a colour"}})", parsed).wasOk());
        REQUIRE (palette.fromVar (parsed).wasOk());

        CHECK (Theme::chrome() == juce::Colour (0xff3b334b));
    }

    SECTION ("a file that is not a theme is refused")
    {
        juce::var parsed;
        REQUIRE (juce::JSON::parse (R"({"something":"else"})", parsed).wasOk());

        CHECK (palette.fromVar (parsed).failed());
    }
}

TEST_CASE ("Every colour is reachable through the file", "[theme]")
{
    // A role that is in the enum but never written is a colour the editor can change and
    // a theme cannot carry -- which is the failure somebody discovers by sharing a theme
    // and being told it looks wrong.
    ShippedPalette shipped;
    auto& palette = Theme::palette();

    for (size_t i = 0; i < Theme::numRoles; ++i)
        palette.set ((Theme::Role) i, juce::Colour (0xff123456));

    juce::TemporaryFile temporary (Theme::Palette::fileExtension);
    REQUIRE (palette.saveTo (temporary.getFile()).wasOk());

    palette.reset();
    REQUIRE (palette.loadFrom (temporary.getFile()).wasOk());

    for (size_t i = 0; i < Theme::numRoles; ++i)
    {
        INFO ("role " << Theme::info()[i].key);
        CHECK (palette.get ((Theme::Role) i) == juce::Colour (0xff123456));
    }
}

TEST_CASE ("editing a colour does not touch the disk", "[theme]")
{
    /*
        The whole of why Save exists. A colour picker sends a change per mouse move, and
        a preference is not worth a file per mouse move -- so editing changes what is on
        screen and nothing else, and the disk is touched when somebody asks for it.
    */
    using namespace Celine::Theme;

    const auto suiteFile = Palette::storedFile();
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("celine-theme-explicit.celthm");
    file.deleteFile();
    Palette::useFileForTesting (file);

    {
        Palette editing;
        CHECK_FALSE (editing.hasUnsavedChanges());

        editing.set (Role::accent, juce::Colour (0xff123456));

        CHECK (editing.hasUnsavedChanges());
        CHECK_FALSE (file.existsAsFile());       // nothing written, and nothing scheduled

        // Not even on the way out: closing without saving is how you discard a theme.
    }

    CHECK_FALSE (file.existsAsFile());

    Palette::useFileForTesting (suiteFile);
    file.deleteFile();
}

TEST_CASE ("a saved theme is still there next time", "[theme]")
{
    using namespace Celine::Theme;

    const auto suiteFile = Palette::storedFile();
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("celine-theme-persistence.celthm");
    file.deleteFile();
    Palette::useFileForTesting (file);

    const auto picked = juce::Colour (0xff123456);

    {
        Palette chooser;
        chooser.set (Role::accent, picked);
        chooser.store();

        CHECK_FALSE (chooser.hasUnsavedChanges());
    }

    REQUIRE (file.existsAsFile());

    Palette nextSession;
    CHECK (nextSession.get (Role::accent) == picked);
    CHECK_FALSE (nextSession.hasUnsavedChanges());

    // And the rest of the palette came back with it rather than being reset.
    CHECK (nextSession.get (Role::chrome) == juce::Colour (info()[(size_t) Role::chrome].shipped));

    Palette::useFileForTesting (suiteFile);
    file.deleteFile();
}

TEST_CASE ("one instance's saved theme reaches another", "[theme]")
{
    /*
        Each plugin format is its own loaded module with its own copy of everything
        static, so the VST3 and the AU open in one session are two palettes that never
        meet. They are brought into step when a window opens, not by watching the disk.
    */
    using namespace Celine::Theme;

    const auto suiteFile = Palette::storedFile();
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("celine-theme-sync.celthm");
    file.deleteFile();
    Palette::useFileForTesting (file);

    Palette asVst3;
    Palette asAu;

    const auto picked = juce::Colour (0xff654321);

    asVst3.set (Role::accent, picked);
    asVst3.store();

    CHECK (asAu.get (Role::accent) != picked);   // it has not looked yet

    asAu.refreshFromDisk();
    CHECK (asAu.get (Role::accent) == picked);

    SECTION ("colours being chosen are not overwritten by another instance")
    {
        // Opening a second window must not take away what somebody is in the middle of
        // picking in this one.
        const auto mine = juce::Colour (0xffabcdef);
        asAu.set (Role::danger, mine);

        asVst3.set (Role::danger, juce::Colours::lime);
        asVst3.store();

        asAu.refreshFromDisk();
        CHECK (asAu.get (Role::danger) == mine);
    }

    Palette::useFileForTesting (suiteFile);
    file.deleteFile();
}

TEST_CASE ("each plugin keeps its own theme", "[theme]")
{
    // Per plugin, not per house: a cab loader and a circuit designer are not obliged to
    // look the same. Cross-compatible on import is a separate promise, kept by the
    // forgiving-file rules above.
    const auto suiteFile = Celine::Theme::Palette::storedFile();
    Celine::Theme::Palette::useFileForTesting (juce::File{});

    const auto real = Celine::Theme::Palette::storedFile();
    CHECK (real.getFileNameWithoutExtension() == juce::String (PRODUCT_NAME_WITHOUT_VERSION));
    CHECK (real.getFileExtension() == juce::String (Celine::Theme::Palette::fileExtension));

    Celine::Theme::Palette::useFileForTesting (suiteFile);
}
