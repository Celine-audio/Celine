#pragma once

#include "ThemeRoles.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace Celine::Theme
{
    //==========================================================================
    /** Every colour the interface can be told to use, in the order `ThemeRoles.h`
        lists them. */
    enum class Role
    {
        #define CELINE_DECLARE_THEME_ROLE(name, label, group, value) name,
        CELINE_THEME_ROLES (CELINE_DECLARE_THEME_ROLE)
        #undef CELINE_DECLARE_THEME_ROLE
        count
    };

    inline constexpr auto numRoles = (size_t) Role::count;

    /** One role, as data: what a `.celthm` file calls it, what the editor calls it,
        which group it is edited under, and the value the design ships with. */
    struct RoleInfo
    {
        const char* key;
        const char* label;
        const char* group;
        std::uint32_t shipped;
    };

    /** All of them, in `Role` order, so `info()[(size_t) role]` is that role's. */
    const std::array<RoleInfo, numRoles>& info() noexcept;

    //==========================================================================
    /**
        The colours in force, and the file they can be written to and read from.

        A ChangeBroadcaster because a colour is not something anything polls: the window
        listens, and repaints itself and re-applies the look and feel when told. There is
        one of these per process -- see `palette()` -- so a plugin instance changing a
        colour changes it in every instance in the session, which is what somebody
        picking a colour expects to happen.

        Nothing here is realtime-safe, and nothing needs to be: this is the message
        thread's, read at paint time and written from the theme editor.
    */
    class Palette : public juce::ChangeBroadcaster
    {
    public:
        Palette();
        ~Palette() override;

        juce::Colour get (Role role) const noexcept { return colours[(size_t) role]; }

        /** Sets one colour and tells the interface. Silent when the colour has not
            actually moved, so dragging a colour picker across a value it is already on
            does not repaint the window. */
        void set (Role, juce::Colour);

        /** Back to what the design ships with. */
        void reset();
        bool isShipped() const noexcept;

        /** What this theme is called. Carried in the file so a shared theme arrives
            with its name rather than as the filename somebody happened to save it as. */
        juce::String getName() const { return name; }
        void setName (juce::String);

        //======================================================================
        // The .celthm file: JSON, one object of key to "#rrggbb".
        //
        // Readable and diffable on purpose -- a theme is a thing people pass around and
        // paste into messages. Keys a build does not know are ignored and keys it knows
        // but the file omits keep their shipped value, so a theme written by a plugin
        // with more colours than this one still loads, and still loads after a colour is
        // added here.

        static constexpr auto fileExtension = ".celthm";
        static constexpr auto fileWildcard = "*.celthm";

        juce::var toVar() const;
        juce::Result fromVar (const juce::var&);

        juce::Result loadFrom (const juce::File&);
        juce::Result saveTo (const juce::File&) const;

        /** Where this plugin's theme is kept between sessions: one file per plugin,
            under the company folder, so every instance of it on the machine wears the
            same colours whatever host or format it is loaded as.

            Per plugin rather than one file for the house, because a cab loader and a
            circuit designer are not obliged to look the same -- and a theme exported
            from either still loads into the other, since a key a build does not know is
            ignored and one it knows but the file omits keeps its shipped value. */
        static juce::File storedFile();

        /** Points `storedFile()` somewhere disposable. A test writes themes; without
            this it would write over whatever colours the person running it had chosen
            for themselves. Same hook, and the same reason, as PresetLibrary's. */
        static void useFileForTesting (const juce::File&);

        /** Reads `storedFile()` if it is there. Anything wrong with it is not worth
            interrupting a session for -- the plugin opens on the shipped colours. */
        void restore();

        /** Writes the theme to `storedFile()`. Explicit: editing a colour changes what
            is on screen and nothing else, so the only thing that ever touches the disk
            is somebody pressing Save. */
        void store();

        /** Colours have moved since the last save. What the editor's status line reads,
            and the whole of why Save exists. */
        bool hasUnsavedChanges() const noexcept { return dirty; }

        /** Picks up a theme another instance has saved.

            Each plugin format is its own loaded module with its own copy of everything
            static, so the VST3 and the AU in one session are two palettes that never
            meet -- which is why theming the VST3 left the AU beside it unchanged until
            it was reloaded.

            Called at the two moments it can matter -- a window opening, and the theme
            editor opening -- rather than on a timer. Nothing here runs in the
            background: the disk is touched when somebody does something, and not
            otherwise. Unsaved changes are left alone, so this cannot take away colours
            you are in the middle of choosing. */
        void refreshFromDisk();

    private:
        /** Tells the interface, and marks the theme as needing a save.

            Every path that changes a colour comes through here, which is what keeps the
            unsaved flag honest however the colour was picked. It writes nothing: a
            colour picker sends a change per mouse move, and a file per mouse move is not
            a thing worth doing for a preference. */
        void changed();

        /** Reading the stored theme is not a change worth writing back. */
        bool restoring = false;

        /** Colours have moved since the last save, or since they were read. */
        bool dirty = false;

        /** The file's timestamp as this palette last left it. Anything newer was
            written by another instance, and is worth reading; recording it after our
            own writes is what stops us reloading what we just saved. */
        juce::Time lastSeenOnDisk;


        std::array<juce::Colour, numRoles> colours;
        juce::String name;

    };

    //==========================================================================
    /** The one in force.

        A function-local static for the same reason the colours themselves used to be:
        nothing orders one translation unit's initialisation against another's, and a
        palette built at namespace scope could be read by a look and feel constructed
        before it. This one cannot exist before its first use. */
    Palette& palette();

    /** The colour a role is wearing right now. Every accessor in Theme.h is one of
        these, and paint code calls them freely -- it is an array index behind a guard
        the compiler reduces to a relaxed load. */
    inline juce::Colour colour (Role role) noexcept { return palette().get (role); }
} // namespace Celine::Theme
