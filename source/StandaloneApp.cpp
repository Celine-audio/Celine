#include <juce_core/system/juce_TargetPlatform.h>

#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace
{
    /** JUCE's own Standalone app (juce_audio_plugin_client_Standalone.cpp), with one thing
        added: a circuit file the system asks it to open (double-clicked in the Finder, dropped
        on the Dock icon, or named on the command line) is loaded as the Load button would.

        macOS hands an open file to the running app through `anotherInstanceStarted`, before or
        after launch; JUCE's app ignores it. Elsewhere the file comes on the command line of a
        new instance, so `initialise` reads it too. The settings file is named as JUCE's app
        names it, so the audio device and the last sheet carry over. */
    class CelineStandaloneApp final : public juce::JUCEApplication
    {
    public:
        CelineStandaloneApp()
        {
            juce::PropertiesFile::Options options;

            options.applicationName     = juce::CharPointer_UTF8 (JucePlugin_Name);
            options.filenameSuffix      = ".settings";
            options.osxLibrarySubFolder = "Application Support";
           #if JUCE_LINUX || JUCE_BSD
            options.folderName          = "~/.config";
           #else
            options.folderName          = "";
           #endif

            appProperties.setStorageParameters (options);
        }

        const juce::String getApplicationName() override    { return juce::CharPointer_UTF8 (JucePlugin_Name); }
        const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }

        /** On Windows and Linux a double-clicked file starts the app again, with the file on
            its command line; one instance only means that reaches the window already open
            (as `anotherInstanceStarted`) rather than opening a second, which would compete for
            the audio device. macOS never starts a second copy for a file, so it keeps JUCE's. */
        bool moreThanOneInstanceAllowed() override
        {
           #if JUCE_MAC
            return true;
           #else
            return false;
           #endif
        }

        void initialise (const juce::String& commandLine) override
        {
            if (! juce::Desktop::getInstance().getDisplays().displays.isEmpty())
            {
                mainWindow = std::make_unique<juce::StandaloneFilterWindow> (
                    getApplicationName(),
                    juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
                    createPluginHolder());

                mainWindow->setVisible (true);
            }
            else
            {
                jassertfalse; // no display, so no window
                pluginHolder = createPluginHolder();
            }

            openCircuitFiles (commandLine);
        }

        void anotherInstanceStarted (const juce::String& commandLine) override
        {
            openCircuitFiles (commandLine);
        }

        void shutdown() override
        {
            pluginHolder = nullptr;
            mainWindow = nullptr;
            appProperties.saveIfNeeded();
        }

        void systemRequestedQuit() override
        {
            if (pluginHolder != nullptr)
                pluginHolder->savePluginState();

            if (mainWindow != nullptr)
                mainWindow->pluginHolder->savePluginState();

            if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
            {
                juce::Timer::callAfterDelay (100, []
                {
                    if (auto* app = juce::JUCEApplicationBase::getInstance())
                        app->systemRequestedQuit();
                });
            }
            else
            {
                quit();
            }
        }

    private:
        std::unique_ptr<juce::StandalonePluginHolder> createPluginHolder()
        {
           #ifdef JucePlugin_PreferredChannelConfigurations
            constexpr juce::StandalonePluginHolder::PluginInOuts channels[] { JucePlugin_PreferredChannelConfigurations };
            const juce::Array<juce::StandalonePluginHolder::PluginInOuts> channelConfig (channels, juce::numElementsInArray (channels));
           #else
            const juce::Array<juce::StandalonePluginHolder::PluginInOuts> channelConfig;
           #endif

            return std::make_unique<juce::StandalonePluginHolder> (appProperties.getUserSettings(), false, juce::String{},
                                                                   nullptr, channelConfig, false);
        }

        /** Loads the last circuit file named, as the Load button does (one sheet is open at a
            time), and brings the window forward. Anything else on the command line is left
            alone. */
        void openCircuitFiles (const juce::String& commandLine)
        {
            juce::File circuit;

            for (auto token : juce::StringArray::fromTokens (commandLine, true))
            {
                const auto file = juce::File::createFileWithoutCheckingPath (token.unquoted());

                if (juce::File::isAbsolutePath (token.unquoted()) && file.existsAsFile()
                    && file.hasFileExtension (PluginProcessor::circuitFileExtension))
                    circuit = file;
            }

            if (mainWindow == nullptr)
                return;

            if (circuit != juce::File{})
                if (auto* editor = dynamic_cast<PluginEditor*> (mainWindow->pluginHolder->processor->getActiveEditor()))
                    editor->loadPresetFile (circuit);

            // Launched again, with or without a file: the window already open comes forward.
            mainWindow->toFront (true);
        }

        juce::ApplicationProperties appProperties;
        std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;
        std::unique_ptr<juce::StandalonePluginHolder> pluginHolder;
    };
} // namespace

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new CelineStandaloneApp(); }

#endif
