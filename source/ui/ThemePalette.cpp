#include "ThemePalette.h"

#include "../ProductInfo.h"

namespace Celine::Theme
{
    namespace
    {
        /** The file's own version. Bumped only if the meaning of what is already in one
            changes -- a new colour does not need it, because an unknown key is ignored
            and a missing one keeps its shipped value. */
        constexpr int fileVersion = 1;

        constexpr auto versionKey = "celineTheme";
        constexpr auto nameKey = "name";
        constexpr auto coloursKey = "colours";

        /** "#rrggbb", which is what somebody pasting a colour into a message expects to
            see. The alpha is dropped: every colour in the palette is opaque, and a theme
            file offering a transparency nobody can set would be a promise the editor
            does not keep. */
        juce::String toHex (juce::Colour colour)
        {
            return "#" + colour.toDisplayString (false).toLowerCase();
        }

        /** Accepts what people actually write: with or without the hash, three digits or
            six. Anything else is left alone by the caller. */
        bool fromHex (juce::String text, juce::Colour& out)
        {
            text = text.trim().removeCharacters ("#").toLowerCase();

            if (text.length() == 3)
                text = juce::String::charToString (text[0]) + juce::String::charToString (text[0])
                     + juce::String::charToString (text[1]) + juce::String::charToString (text[1])
                     + juce::String::charToString (text[2]) + juce::String::charToString (text[2]);

            if (text.length() != 6 || ! text.containsOnly ("0123456789abcdef"))
                return false;

            out = juce::Colour (0xff000000u | (juce::uint32) text.getHexValue32());
            return true;
        }
    }

    //==========================================================================
    const std::array<RoleInfo, numRoles>& info() noexcept
    {
        static const std::array<RoleInfo, numRoles> all {
            #define CELINE_THEME_ROLE_INFO(name, label, group, value) RoleInfo { #name, label, group, value },
            CELINE_THEME_ROLES (CELINE_THEME_ROLE_INFO)
            #undef CELINE_THEME_ROLE_INFO
        };

        return all;
    }

    //==========================================================================
    Palette::Palette()
    {
        // Neither of these is a change anybody made: the first is what the design ships
        // and the second is what is already on disk. Guarded so building a palette does
        // not schedule a write of the file it has just read -- and so it does not reach
        // for a Timer, which matters because reading any colour builds this, and the
        // first read can come from a static initialiser, before there is a message loop.
        const juce::ScopedValueSetter<bool> guard (restoring, true);

        reset();
        restore();
    }

    void Palette::set (Role role, juce::Colour colour)
    {
        auto& held = colours[(size_t) role];

        // Silent when it has not moved. A colour picker sends a change per mouse move,
        // and most of those land on the value it is already showing.
        if (held == colour)
            return;

        held = colour;
        changed();
    }

    void Palette::reset()
    {
        auto moved = false;

        for (size_t i = 0; i < numRoles; ++i)
        {
            const juce::Colour shipped { info()[i].shipped };

            moved |= colours[i] != shipped;
            colours[i] = shipped;
        }

        moved |= name.isNotEmpty();
        name = {};

        if (moved)
            changed();
    }

    bool Palette::isShipped() const noexcept
    {
        for (size_t i = 0; i < numRoles; ++i)
            if (colours[i] != juce::Colour (info()[i].shipped))
                return false;

        return true;
    }

    void Palette::setName (juce::String newName)
    {
        newName = newName.trim();

        if (name == newName)
            return;

        name = std::move (newName);
        changed();
    }

    //==========================================================================
    juce::var Palette::toVar() const
    {
        auto* entries = new juce::DynamicObject();

        for (size_t i = 0; i < numRoles; ++i)
            entries->setProperty (info()[i].key, toHex (colours[i]));

        auto* root = new juce::DynamicObject();
        root->setProperty (versionKey, fileVersion);
        root->setProperty (nameKey, name);
        root->setProperty (coloursKey, juce::var (entries));

        return juce::var (root);
    }

    juce::Result Palette::fromVar (const juce::var& source)
    {
        const auto* root = source.getDynamicObject();

        if (root == nullptr || ! root->hasProperty (coloursKey))
            return juce::Result::fail (
                juce::String::fromUTF8 ("That is not a C\xc3\xa9line theme file."));

        const auto* entries = root->getProperty (coloursKey).getDynamicObject();

        if (entries == nullptr)
            return juce::Result::fail ("That theme has no colours in it.");

        // Everything the file does not mention keeps the shipped value rather than
        // whatever happened to be on screen: loading a theme has to give the same result
        // whatever was loaded before it.
        for (size_t i = 0; i < numRoles; ++i)
        {
            const auto& entry = info()[i];
            juce::Colour parsed { entry.shipped };

            if (entries->hasProperty (entry.key))
                fromHex (entries->getProperty (entry.key).toString(), parsed);

            colours[i] = parsed;
        }

        name = root->getProperty (nameKey).toString().trim();

        changed();
        return juce::Result::ok();
    }

    juce::Result Palette::loadFrom (const juce::File& file)
    {
        if (! file.existsAsFile())
            return juce::Result::fail ("That theme file no longer exists.");

        juce::var parsed;

        if (juce::JSON::parse (file.loadFileAsString(), parsed).failed())
            return juce::Result::fail ("That file could not be read as a theme.");

        const auto result = fromVar (parsed);

        if (result.wasOk() && name.isEmpty())
            name = file.getFileNameWithoutExtension();

        return result;
    }

    juce::Result Palette::saveTo (const juce::File& file) const
    {
        if (! file.getParentDirectory().createDirectory())
            return juce::Result::fail ("That folder could not be written to.");

        return file.replaceWithText (juce::JSON::toString (toVar(), false, 6))
                   ? juce::Result::ok()
                   : juce::Result::fail ("That file could not be written to.");
    }

    //==========================================================================
    namespace
    {
        juce::File& storedFileOverride()
        {
            static juce::File file;
            return file;
        }
    }

    juce::File Palette::storedFile()
    {
        if (const auto& override_ = storedFileOverride(); override_ != juce::File{})
            return override_;

        juce::PropertiesFile::Options options;
        options.applicationName = PRODUCT_NAME_WITHOUT_VERSION;
        options.filenameSuffix = fileExtension;
        options.folderName = juce::String (juce::CharPointer_UTF8 (ProductInfo::companyName));
        options.osxLibrarySubFolder = "Application Support";

        return options.getDefaultFile();
    }

    namespace
    {
        /** Where the theme lived when every Céline plugin shared one. Read once, if this
            plugin has no file of its own yet, so splitting them up does not throw away
            the colours somebody had already chosen. */
        juce::File legacySharedFile()
        {
            juce::PropertiesFile::Options options;
            options.applicationName = "theme";
            options.filenameSuffix = Palette::fileExtension;
            options.folderName = juce::String (juce::CharPointer_UTF8 (ProductInfo::companyName));
            options.osxLibrarySubFolder = "Application Support";

            return options.getDefaultFile();
        }
    }

    void Palette::useFileForTesting (const juce::File& file)
    {
        storedFileOverride() = file;
    }

    Palette::~Palette() = default;

    void Palette::changed()
    {
        sendChangeMessage();

        if (! restoring)
            dirty = true;
    }

    void Palette::restore()
    {
        auto stored = storedFile();

        if (! stored.existsAsFile())
        {
            // Nothing of this plugin's own yet. Inherit what the house shared before the
            // themes were split up, if there is any.
            //
            // Only when reading the real location: a test points storedFile() somewhere
            // disposable precisely so it does not touch what the person at this machine
            // has, and falling back to the shared file would walk straight around that.
            if (storedFileOverride() != juce::File{})
                return;

            const auto legacy = legacySharedFile();

            if (! legacy.existsAsFile() || legacy == stored)
                return;

            stored = legacy;
        }

        const juce::ScopedValueSetter<bool> guard (restoring, true);
        loadFrom (stored);

        lastSeenOnDisk = storedFile().getLastModificationTime();
        dirty = false;
    }

    void Palette::refreshFromDisk()
    {
        // Colours somebody is in the middle of choosing are not overwritten by what
        // another instance happened to save.
        if (dirty)
            return;

        const auto stored = storedFile();
        const auto stamp = stored.getLastModificationTime();

        if (stamp == lastSeenOnDisk)
            return;

        lastSeenOnDisk = stamp;

        if (! stored.existsAsFile())
            return;

        // Guarded, so reading somebody else's theme tells this window to repaint without
        // scheduling a write back of what was just read.
        const juce::ScopedValueSetter<bool> guard (restoring, true);
        loadFrom (stored);
    }

    void Palette::store()
    {
        // Best effort. A theme that cannot be written is worth neither an alert in the
        // middle of a session nor losing the colours already on screen over.
        saveTo (storedFile());

        // Remembered so refreshFromDisk does not read our own write back as somebody
        // else's change.
        lastSeenOnDisk = storedFile().getLastModificationTime();
        dirty = false;
    }

    //==========================================================================
    Palette& palette()
    {
        static Palette instance;
        return instance;
    }
} // namespace Celine::Theme
