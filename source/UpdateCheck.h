#pragma once

#include <juce_core/juce_core.h>

#include <optional>

/**
    Whether a newer Céline has been released, asked of GitHub once per launch.

    **Off until the user turns it on.** The first time a window opens on a machine the
    user is asked once (see PluginEditor::offerUpdateChecksOnFirstRun); closing that
    question unanswered leaves it off, and the Settings menu can change it later. The
    choice lives in the same per-machine settings file as the preset folder -- see
    PresetLibrary -- so it describes the computer, never a session or a sheet.

    **One request per process, at most.** Session::startIfNeeded() is a no-op after its
    first call whatever happened: offline, timed out, GitHub answering with an error, a
    reply that does not parse. Nothing retries. A plugin is a guest in someone else's
    program, and one that keeps knocking on the network because it did not get an
    answer is one that gets noticed for the wrong reasons.

    **Only newer counts.** isNewer() is a strict "greater than", component by component,
    so a build ahead of the newest release -- a dev build at 1.4.0 while the release is
    1.2.0 -- never hears that 1.2.0 is "available". A version either side cannot read
    is never newer: a prompt nobody can act on is worse than no prompt.

    What goes over the wire is one HTTPS GET to GitHub's public API, with nothing of the
    user's in it -- no version, no identifier. The comparison happens here.
*/
namespace UpdateCheck
{
    /** The newest published release: GitHub's `releases/latest`, which leaves drafts
        and pre-releases out by itself. */
    inline constexpr auto latestReleaseApi = "https://api.github.com/repos/Celine-audio/Celine/releases/latest";

    /** How long the one request may take before it is given up on, for good. */
    inline constexpr int timeoutMs = 5000;

    /** Where "Download" sends the user: the repository's releases page, built from
        ProductInfo::repositoryUrl so the two cannot point at different places. */
    juce::URL releasesPage();

    /** This build's version, as CMake read it from the VERSION file. */
    juce::String installedVersion();

    //==========================================================================
    /** A release worth announcing. */
    struct Release
    {
        /** Without the tag's leading "v": "1.5.0". */
        juce::String version;
    };

    /** "v1.2.0", "1.2.02" and "1.4" as numbers -- {1, 2, 0}, {1, 2, 2}, {1, 4}.
        Anything after a component's leading digits is ignored ("0-beta" is 0).
        Empty when there is nothing to read, or a component has no digits at all. */
    juce::Array<int> parseVersion (const juce::String& text);

    /** True only if `candidate` is strictly newer than `installed`, comparing
        component by component with missing ones counting as zero, so 1.2 and
        1.2.0 are the same version. False whenever either cannot be read. */
    bool isNewer (const juce::String& candidate, const juce::String& installed);

    /** The release a `releases/latest` reply describes, or nothing if it is not one --
        an error body, a draft, a pre-release, or a tag that is not a version. */
    std::optional<Release> parseLatestRelease (const juce::String& json);

    //==========================================================================
    /** The user's say in it: whether to check at all, and whether they have been asked. */
    class Preferences
    {
    public:
        /** The machine's real settings file, or the test redirect if one is in force
            -- the same file PresetLibrary uses, and for the same reasons. */
        Preferences();

        /** Points at a settings file of your choosing. For tests. */
        explicit Preferences (const juce::File& settingsFile);

        /** Off unless the user has turned it on. */
        bool isEnabled() const;
        void setEnabled (bool shouldCheck);

        /** Whether the user has been asked yet. Asked once, ever, like the preset
            folder: declining is an answer. */
        bool hasBeenOffered() const;
        void markOffered();

    private:
        juce::File settingsFile;
    };

    //==========================================================================
    /** The one check this process makes, shared by every window in it. */
    namespace Session
    {
        /** Starts the check on a background thread, the first time it is called in
            this process; does nothing every time after, whatever became of it. */
        void startIfNeeded();

        /** Whether startIfNeeded() has been called in this process. */
        bool hasStarted();

        /** A newer release, the first time it is asked for after the check found one,
            and never again in this process: "Remind me later" means not until the
            next launch, and with several windows open only one of them asks. */
        std::optional<Release> takeUpdateToAnnounce();
    } // namespace Session
} // namespace UpdateCheck
