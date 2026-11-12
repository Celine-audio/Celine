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
        // Keys in the shared settings file.
        const juce::String enabledKey { "updateChecksEnabled" };
        const juce::String offeredKey { "updateChecksOffered" };

        //======================================================================
        /** The check's result, kept in statics so "once per launch" survives JUCE being shut
            down and restarted inside one process. */
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

        /** The largest reply read. A release's JSON is a few kilobytes. */
        constexpr int maximumReplyBytes = 1 << 20;

        /** The thread running the request. Deleted at JUCE shutdown, which waits for it:
            the destructor cancels the request first, so that wait is short even when the
            network has stalled mid-reply. */
        class Worker final : public juce::Thread, private juce::DeletedAtShutdown
        {
        public:
            Worker() : juce::Thread ("Celine update check") {}

            ~Worker() override
            {
                signalThreadShouldExit();

                {
                    const juce::ScopedLock hold (streamLock);
                    if (stream != nullptr)
                        stream->cancel();
                }

                stopThread (timeoutMs + 1000);
            }

            void run() override
            {
                const auto reply = fetchLatestRelease();

                if (threadShouldExit())
                    return;

                const auto release = parseLatestRelease (reply);

                if (release.has_value() && isNewer (release->version, installedVersion()))
                {
                    const std::scoped_lock hold (outcome().lock);
                    outcome().newer = release;
                }
            }

        private:
            /** Fetches the latest release. Empty on any failure, or when asked to stop. */
            juce::String fetchLatestRelease()
            {
                juce::WebInputStream request (juce::URL (latestReleaseApi), false);

                // GitHub requires a User-Agent. It names the product only.
                request.withExtraHeaders ("Accept: application/vnd.github+json\r\n"
                                          "User-Agent: Celine-update-check")
                    .withConnectionTimeout (timeoutMs);

                // Published before the exit check, so the destructor either sees the flag
                // checked here or finds the stream to cancel.
                {
                    const juce::ScopedLock hold (streamLock);
                    stream = &request;
                }

                juce::MemoryOutputStream reply;

                if (! threadShouldExit() && request.connect (nullptr) && request.getStatusCode() == 200)
                {
                    // In pieces, so a stop request is seen between them; a read blocked on
                    // a stalled connection is ended by the destructor's cancel().
                    char buffer[4096];
                    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);

                    while (! threadShouldExit() && ! request.isExhausted()
                           && reply.getDataSize() < static_cast<size_t> (maximumReplyBytes)
                           && juce::Time::getMillisecondCounter() < deadline)
                    {
                        const int got = request.read (buffer, sizeof (buffer));
                        if (got <= 0)
                            break;
                        reply.write (buffer, static_cast<size_t> (got));
                    }
                }

                {
                    const juce::ScopedLock hold (streamLock);
                    stream = nullptr;
                }

                if (threadShouldExit() || ! request.isExhausted())
                    return {};

                return reply.toUTF8();
            }

            juce::CriticalSection streamLock;
            juce::WebInputStream* stream = nullptr;
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

            // A component with no digits ("1..2", "1.x") is not a version.
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
            // Missing components count as zero.
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

        // releases/latest already excludes these; checked anyway.
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

    // Opened for each read and write, like PresetLibrary, since every instance shares it.
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
            // exchange, so two windows cannot both start a check.
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
