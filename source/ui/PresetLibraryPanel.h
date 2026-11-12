#pragma once

#include <CelineUI/IconButton.h>
#include <CelineUI/Theme.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace Celine
{
    //==========================================================================
    /** The preset library, dropped down from the toolbar's preset field.

        While it is open, the area over the preset field is the search box (the editor
        says where with setFieldArea); the list hangs below. Rows: "New empty sheet", the
        "Factory presets" folder, then the user's preset folder as a tree. Right-click
        reveals a file; the loaded preset is marked with a dot.

        Lists the folder fresh each time and copies nothing. Choices are reported through
        the `on` callbacks; the editor loads and closes. */
    class PresetLibraryPanel : public juce::Component
    {
    public:
        PresetLibraryPanel();
        ~PresetLibraryPanel() override;

        // ======================================================================
        /** The user's preset folder, or none. Re-listed from disk. */
        void setFolder (const juce::File&);
        juce::File getFolder() const { return folder; }

        /** The shipped examples by name, in SchematicModel::Examples order; a row reports its
            index through onFactoryPresetChosen. */
        void setFactoryPresets (const juce::StringArray& names);

        /** The preset the sheet came from: a file, a factory example's name, or neither.
            Marked in the list, with its folder opened. */
        void setCurrentPreset (const juce::File& file, const juce::String& factoryName);

        /** Reads the folder again. */
        void rescan();

        /** Where the preset field is, in this panel's coordinates; the list goes under it.
            Set before calling getPreferredHeight. */
        void setFieldArea (juce::Rectangle<int>);
        juce::Rectangle<int> getFieldArea() const noexcept { return fieldArea; }

        /** The list's area: everything under the field. */
        juce::Rectangle<int> getListArea() const;

        /** Height needed to show every row, field included. The editor caps it; past that the
            list scrolls. */
        int getPreferredHeight() const;

        /** Focuses the search field. */
        void focusSearch();

        /** What Return in the search does: chooses the first preset shown. Nothing when
            the search is empty. */
        void chooseFirstMatch();

        //======================================================================
        std::function<void()> onNewSheet;
        std::function<void (int index)> onFactoryPresetChosen;
        std::function<void (const juce::File&)> onPresetChosen;

        /** The "Choose a folder..." row was clicked. */
        std::function<void()> onChooseFolder;

        /** Space at the field's right end taken by the editor's folder icon. */
        void setFieldTrailingSpace (int pixels);

        /** Escape was pressed. */
        std::function<void()> onDismiss;

        /** A folder opened or closed; the preferred height may have changed. */
        std::function<void()> onPreferredHeightChanged;

        // ======================================================================
        // For tests.
        void setSearchText (const juce::String&);
        int getNumShownRows() const;
        juce::String getRowLabel (int row) const;
        bool isRowFolder (int row) const;
        bool isRowOpen (int row) const;
        bool isRowCurrent (int row) const;
        int getRowDepth (int row) const;

        /** Whether a divider is drawn under this row: the last row of a root group (New, the
            factory folder, each top-level folder with its open contents) before the next. */
        bool hasDividerBelow (int row) const;

        /** Whether the next row is a section separator. */
        bool hasSectionBreakBelow (int row) const;

        /** True for section separator rows. */
        bool isRowSeparator (int row) const;

        /** A row's height (separators are shorter). */
        int getRowHeight (int row) const;

        /** Does what clicking the row does. */
        void chooseRow (int row);

        //======================================================================
        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

        /** Only the field and the list take clicks. */
        bool hitTest (int x, int y) override;

    private:
        //======================================================================
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected);
        void listBoxItemClicked (int row, const juce::MouseEvent&);
        void backgroundClicked (const juce::MouseEvent&);

        /** The rows, with per-row heights (a juce::ListBox can't vary them). Hover lights a
            row; clicks go to the panel. */
        class RowList : public juce::Viewport
        {
        public:
            explicit RowList (PresetLibraryPanel& owner);

            /** Re-reads row heights after `shown` changed. */
            void updateContent();

            int getLitRow() const noexcept { return lit; }
            void setLitRow (int row);

            /** The row at `y` in the list's coordinates, or -1. */
            int getRowAt (int y) const;

            void resized() override;

        private:
            struct Content : juce::Component
            {
                explicit Content (RowList& o) : list (o) {}
                void paint (juce::Graphics&) override;
                void mouseMove (const juce::MouseEvent&) override;
                void mouseUp (const juce::MouseEvent&) override;
                RowList& list;
            };

            PresetLibraryPanel& panel;
            Content content { *this };
            std::vector<int> tops;   // each row's top, then the total height
            int lit = -1;
        };

        void applyColours();
        void lookAndFeelChanged() override { applyColours(); }

        /** What a row is: decides how it is drawn and what a click does. */
        enum class Kind
        {
            newSheet,      // "New empty sheet"
            factoryFolder, // "Factory presets", a folder of the shipped examples
            factoryPreset,
            folder,        // a subfolder of the preset folder
            preset,        // a .celsch in it
            chooseFolder,  // "Choose a folder for your own presets...", when there is none
            note,          // something to read rather than click: an empty folder
            separator      // the gap between New, the factory presets and the library
        };

        struct Entry
        {
            Kind kind = Kind::note;
            juce::File file;
            juce::String label;
            int depth = 0;
            int factoryIndex = -1;

            /** Only used in `shown`: a search opens the folders containing matches without
                changing which ones the user opened. */
            bool open = false;

            /** Root group number in `shown`; a divider goes where it changes. */
            int group = 0;
        };

        static bool isFolderKind (Kind kind) { return kind == Kind::factoryFolder || kind == Kind::folder; }

        /** Section: 0 for New, 1 for the factory presets, 2 for the user's library. */
        static int sectionOf (Kind kind)
        {
            return kind == Kind::newSheet ? 0 : kind == Kind::factoryFolder || kind == Kind::factoryPreset ? 1 : 2;
        }

        /** Key a folder's open state is stored under: its path, or a fixed key for the factory
            folder. Survives a rescan. */
        juce::String keyFor (const Entry&) const;
        bool isExpanded (const Entry& entry) const { return expanded.contains (keyFor (entry)); }

        void rebuild();
        void scan (const juce::File& directory, int depth);
        void applyFilter();
        void toggle (const Entry&);
        void showRevealMenu (const juce::File& target);
        bool isCurrent (const Entry&) const;

        juce::File folder;
        juce::StringArray factoryNames;
        juce::File currentFile;
        juce::String currentFactoryName;

        juce::Array<Entry> entries, shown;
        juce::StringArray expanded;

        /** The toolbar's preset field until the editor sets it. */
        juce::Rectangle<int> fieldArea { 0, 0, 302, Theme::buttonSize };
        int fieldTrailingSpace = 0;

        juce::TextEditor search;
        RowList list { *this };


        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetLibraryPanel)
    };
} // namespace Celine
