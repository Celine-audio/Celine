#include "UpdateCheck.h"

#include "PresetLibrary.h"
#include "ProductInfo.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <mutex>

namespace UpdateCheck
{
    namespace
    {
        // Keys in the shared settings file. Strings rather than an enum because they
        // end up in a file someone may one day open in a text editor.
        const juce::String enabledKey { "updateChecksEnabled" };
        const juce::String offeredKey { "updateChecksOffered" };

        //======================================================================
        /** What the check found, kept in plain statics rather than in an object JUCE
            deletes at shutdown: the plugin's JUCE can be shut down and started again
            within one process -- a host removing the last instance and adding one
            back -- and "once per launch" has to survive that, or the second start
            would ask again. */
        struct Outcome
        {
            std::atomic<bool> started { false };
            std::mutex lock;
            std::optional<Release> newer;
            bool announced = false;
        };

        Outcome& outcome()
        {
            static Outcome o;
            return o;
        }

        /** Asks GitHub for the newest release. Empty on any failure at all -- no
            connection, a timeout, a status other than 200 -- since the check gives up
            on every one of them the same way. */
        juce::String fetchLatestRelease (const juce::Thread& thread)
        {
            int status = 0;

            // GitHub's API refuses a request with no User-Agent. The one sent names the
            // product and nothing else -- not the version, which is compared here
            // rather than told to anyone.
            const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                     .withConnectionTimeoutMs (timeoutMs)
                                     .withExtraHeaders ("Accept: application/vnd.github+json\r\n"
                                                        "User-Agent: Celine-update-check")
                                     .withStatusCode (&status);

            const auto stream = juce::URL (latestReleaseApi).createInputStream (options);

            if (stream == nullptr || status != 200 || thread.threadShouldExit())
                return {};

            return stream->readEntireStreamAsString();
        }

        /** The thread the one request runs on.

            Deleted at JUCE's shutdown rather than owned by anything: nothing needs it
            once it has finished, and a plugin's code must not be running on a thread
            of its own after the host has unloaded it, which a detached thread could
            not promise. Shutting down waits for it -- at worst the request's timeout,
            should the network go quiet at exactly that moment. */
        class Worker final : public juce::Thread, private juce::DeletedAtShutdown
        {
        public:
            Worker() : juce::Thread ("Celine update check") {}

            ~Worker() override { stopThread (timeoutMs + 1000); }

            void run() override
            {
                const auto reply = fetchLatestRelease (*this);

                if (threadShouldExit())
                    return;

                const auto release = parseLatestRelease (reply);

                if (release.has_value() && isNewer (release->version, installedVersion()))
                {
                    const std::scoped_lock hold (outcome().lock);
                    outcome().newer = release;
                }
            }
        };
    } // namespace

    //==========================================================================
    juce::URL releasesPage()
    {
        return juce::URL (juce::String (ProductInfo::repositoryUrl) + "/releases");
    }

    juce::String installedVersion()
    {
        return JucePlugin_VersionString;
    }

    juce::Array<int> parseVersion (const juce::String& text)
    {
        auto trimmed = text.trim();

        if (trimmed.startsWithIgnoreCase ("v"))
            trimmed = trimmed.substring (1);

        juce::Array<int> numbers;

        for (const auto& part : juce::StringArray::fromTokens (trimmed, ".", ""))
        {
            const auto digits = part.trim().initialSectionContainingOnly ("0123456789");

            // A component with no number in it -- "1..2", "1.x" -- is not a version.
            if (digits.isEmpty())
                return {};

            numbers.add (digits.getIntValue());
        }

        return numbers;
    }

    bool isNewer (const juce::String& candidate, const juce::String& installed)
    {
        const auto theirs = parseVersion (candidate);
        const auto ours = parseVersion (installed);

        if (theirs.isEmpty() || ours.isEmpty())
            return false;

        for (int i = 0; i < juce::jmax (theirs.size(), ours.size()); ++i)
        {
            // Missing components count as zero, so 1.2 and 1.2.0 are one version.
            const int a = theirs[i];
            const int b = ours[i];

            if (a != b)
                return a > b;
        }

        return false;
    }

    std::optional<Release> parseLatestRelease (const juce::String& json)
    {
        const auto parsed = juce::JSON::parse (json);

        if (! parsed.isObject())
            return std::nullopt;

        // releases/latest leaves these out already; checked anyway, because a reply
        // that included one would otherwise announce a build nobody meant to ship.
        if (static_cast<bool> (parsed.getProperty ("draft", false))
            || static_cast<bool> (parsed.getProperty ("prerelease", false)))
            return std::nullopt;

        auto tag = parsed.getProperty ("tag_name", {}).toString().trim();

        if (tag.startsWithIgnoreCase ("v"))
            tag = tag.substring (1);

        if (parseVersion (tag).isEmpty())
            return std::nullopt;

        return Release { tag };
    }

    //==========================================================================
    Preferences::Preferences() : settingsFile (PresetLibrary().getSettingsFile())
    {
    }

    Preferences::Preferences (const juce::File& file) : settingsFile (file)
    {
    }

    // Opened for each read and write, as PresetLibrary does and for its reason: every
    // instance of the plugin shares the file, and one holding it open would answer
    // from a stale copy and write that copy back over a newer choice.
    bool Preferences::isEnabled() const
    {
        const juce::PropertiesFile file (settingsFile, PresetLibrary::getSettingsOptions());
        return file.getBoolValue (enabledKey, false);
    }

    void Preferences::setEnabled (bool shouldCheck)
    {
        juce::PropertiesFile file (settingsFile, PresetLibrary::getSettingsOptions());
        file.setValue (enabledKey, shouldCheck);
        file.saveIfNeeded();
    }

    bool Preferences::hasBeenOffered() const
    {
        const juce::PropertiesFile file (settingsFile, PresetLibrary::getSettingsOptions());
        return file.getBoolValue (offeredKey, false);
    }

    void Preferences::markOffered()
    {
        juce::PropertiesFile file (settingsFile, PresetLibrary::getSettingsOptions());
        file.setValue (offeredKey, true);
        file.saveIfNeeded();
    }

    //==========================================================================
    namespace Session
    {
        void startIfNeeded()
        {
            // exchange, so two windows opening together cannot both start one.
            if (outcome().started.exchange (true))
                return;

            (new Worker())->startThread (juce::Thread::Priority::low);
        }

        bool hasStarted()
        {
            return outcome().started.load();
        }

        std::optional<Release> takeUpdateToAnnounce()
        {
            auto& o = outcome();
            const std::scoped_lock hold (o.lock);

            if (o.announced || ! o.newer.has_value())
                return std::nullopt;

            o.announced = true;
            return o.newer;
        }
    } // namespace Session
} // namespace UpdateCheck
