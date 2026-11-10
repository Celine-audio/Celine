#pragma once

#include <juce_core/juce_core.h>

#include <optional>

/** Checks GitHub once per launch for a newer Céline release.

    Off until the user enables it. The choice is stored in the per-machine settings
    file (see PresetLibrary). At most one request per process, never retried. Only a
    strictly newer version is announced; an unreadable version never is. The request
    sends nothing about the user. */
namespace UpdateCheck
{
    /** GitHub's latest release, which excludes drafts and pre-releases. */
    inline constexpr auto latestReleaseApi = "https://api.github.com/repos/Celine-audio/Celine/releases/latest";

    /** Timeout for connecting, and again for reading the reply. */
    inline constexpr int timeoutMs = 5000;

    /** The releases page "Download" opens. */
    juce::URL releasesPage();

    /** This build's version, from the VERSION file. */
    juce::String installedVersion();

    //==========================================================================
    /** A release worth announcing. */
    struct Release
    {
        /** Without the leading "v", e.g. "1.5.0". */
        juce::String version;
    };

    /** "v1.2.0" -> {1, 2, 0}. Trailing text in a component is ignored ("0-beta" is 0).
        Empty if a component has no digits. */
    juce::Array<int> parseVersion (const juce::String& text);

    /** True if `candidate` is strictly newer than `installed`. Missing components count
        as zero. False if either cannot be read. */
    bool isNewer (const juce::String& candidate, const juce::String& installed);

    /** The release a `releases/latest` reply describes, or nothing for an error, a
        draft, a pre-release or a tag that is not a version. */
    std::optional<Release> parseLatestRelease (const juce::String& json);

    //==========================================================================
    /** Whether to check, and whether the user has been asked. */
    class Preferences
    {
    public:
        /** Uses the real settings file, or the test redirect. */
        Preferences();

        /** Uses the given settings file. For tests. */
        explicit Preferences (const juce::File& settingsFile);

        /** Off unless the user enabled it. */
        bool isEnabled() const;
        void setEnabled (bool shouldCheck);

        /** Whether the user has been asked yet. Asked only once. */
        bool hasBeenOffered() const;
        void markOffered();

    private:
        juce::File settingsFile;
    };

    //==========================================================================
    /** The single check of this process, shared by all windows. */
    namespace Session
    {
        /** Starts the check on a background thread the first time; does nothing afterwards. */
        void startIfNeeded();

        /** Whether startIfNeeded() has been called. */
        bool hasStarted();

        /** The newer release the check found, returned once per process, then never again. */
        std::optional<Release> takeUpdateToAnnounce();
    } // namespace Session
} // namespace UpdateCheck
