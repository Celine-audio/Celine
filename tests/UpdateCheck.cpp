#include "helpers/test_helpers.h"
#include <UpdateCheck.h>
#include <catch2/catch_test_macros.hpp>

// The network request isn't made here; everything that decides what the user is told
// is pure and tested.

TEST_CASE ("Update check: versions read as numbers", "[updates]")
{
    using UpdateCheck::parseVersion;

    CHECK (parseVersion ("1.4.0") == juce::Array<int> { 1, 4, 0 });
    CHECK (parseVersion ("v1.1.0") == juce::Array<int> { 1, 1, 0 });
    CHECK (parseVersion ("V2.0") == juce::Array<int> { 2, 0 });

    // The remote's VERSION has been written this way.
    CHECK (parseVersion ("1.2.02") == juce::Array<int> { 1, 2, 2 });

    // A suffix after the digits is ignored.
    CHECK (parseVersion ("1.3.0-beta") == juce::Array<int> { 1, 3, 0 });

    // Nothing to read is empty, not zero.
    CHECK (parseVersion ("").isEmpty());
    CHECK (parseVersion ("latest").isEmpty());
    CHECK (parseVersion ("1..2").isEmpty());
    CHECK (parseVersion ("1.x.0").isEmpty());

    // This build's version must parse.
    CHECK (! parseVersion (UpdateCheck::installedVersion()).isEmpty());
}

TEST_CASE ("Update check: only a newer release counts", "[updates]")
{
    using UpdateCheck::isNewer;

    // A dev build ahead of the latest release isn't told about it.
    CHECK_FALSE (isNewer ("1.2.0", "1.4.0"));
    CHECK_FALSE (isNewer ("v1.3.0", "1.4.0"));

    CHECK (isNewer ("v1.5.0", "1.4.0"));
    CHECK (isNewer ("1.4.1", "1.4.0"));
    CHECK (isNewer ("2.0.0", "1.9.9"));

    // The same version is not newer.
    CHECK_FALSE (isNewer ("1.4.0", "1.4.0"));
    CHECK_FALSE (isNewer ("v1.4.0", "1.4.0"));
    CHECK_FALSE (isNewer ("1.4", "1.4.0"));
    CHECK_FALSE (isNewer ("1.4.0", "1.4"));
    CHECK (isNewer ("1.4.1", "1.4"));

    // Compared as numbers: 10 > 9.
    CHECK (isNewer ("1.10.0", "1.9.0"));
    CHECK_FALSE (isNewer ("1.9.0", "1.10.0"));

    // Leading zeros are ignored.
    CHECK (isNewer ("1.2.02", "1.2.1"));
    CHECK_FALSE (isNewer ("1.2.02", "1.2.2"));

    // Unreadable is never newer.
    CHECK_FALSE (isNewer ("", "1.4.0"));
    CHECK_FALSE (isNewer ("latest", "1.4.0"));
    CHECK_FALSE (isNewer ("9.9.9", ""));
    CHECK_FALSE (isNewer ("9.9.9", "dev"));
}

TEST_CASE ("Update check: GitHub's reply", "[updates]")
{
    using UpdateCheck::parseLatestRelease;

    // The relevant fields of releases/latest. UTF-8, for the accented name.
    const auto release = juce::String::fromUTF8 (R"({
        "html_url": "https://github.com/Celine-audio/Celine/releases/tag/v1.1.0",
        "tag_name": "v1.1.0",
        "name": "Céline 1.1.0",
        "draft": false,
        "prerelease": false
    })");

    const auto parsed = parseLatestRelease (release);
    REQUIRE (parsed.has_value());
    CHECK (parsed->version == "1.1.0");

    // A tag without the v reads the same.
    CHECK (parseLatestRelease (R"({ "tag_name": "1.2.0" })")->version == "1.2.0");

    // Not announced: pre-releases, drafts, non-version tags, GitHub's error body and
    // non-JSON.
    CHECK_FALSE (parseLatestRelease (R"({ "tag_name": "v2.0.0", "prerelease": true })").has_value());
    CHECK_FALSE (parseLatestRelease (R"({ "tag_name": "v2.0.0", "draft": true })").has_value());
    CHECK_FALSE (parseLatestRelease (R"({ "tag_name": "nightly" })").has_value());
    CHECK_FALSE (parseLatestRelease (R"({ "message": "Not Found", "documentation_url": "https://docs.github.com" })").has_value());
    CHECK_FALSE (parseLatestRelease ("<html>Service Unavailable</html>").has_value());
    CHECK_FALSE (parseLatestRelease ("").has_value());
}

TEST_CASE ("Update check: off until asked, and asked once", "[updates]")
{
    const juce::TemporaryFile settings ("update-check-settings");

    // Fresh machine: not asked, not checking.
    {
        const UpdateCheck::Preferences preferences (settings.getFile());
        CHECK_FALSE (preferences.isEnabled());
        CHECK_FALSE (preferences.hasBeenOffered());
    }

    // The answer persists: a fresh object re-reads it.
    {
        UpdateCheck::Preferences preferences (settings.getFile());
        preferences.markOffered();
        preferences.setEnabled (true);
    }

    {
        UpdateCheck::Preferences preferences (settings.getFile());
        CHECK (preferences.hasBeenOffered());
        CHECK (preferences.isEnabled());

        // Turning checks off keeps them asked.
        preferences.setEnabled (false);
    }

    const UpdateCheck::Preferences reopened (settings.getFile());
    CHECK_FALSE (reopened.isEnabled());
    CHECK (reopened.hasBeenOffered());
}

TEST_CASE ("Update check: Download goes to the releases page", "[updates]")
{
    CHECK (UpdateCheck::releasesPage().toString (false) == "https://github.com/Celine-audio/Celine/releases");
}

TEST_CASE ("Update check: nothing is announced without a check", "[updates]")
{
    // The test editor is never shown, so no check starts and nothing is announced.
    runWithinPluginEditor ([] (PluginProcessor&) {
        CHECK_FALSE (UpdateCheck::Session::hasStarted());
        CHECK_FALSE (UpdateCheck::Session::takeUpdateToAnnounce().has_value());
    });
}
