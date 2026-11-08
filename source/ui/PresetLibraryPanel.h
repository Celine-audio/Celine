#pragma once

#include <CelineUI/IconButton.h>
#include <CelineUI/Theme.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace Celine
{
    //==========================================================================
    /**
        The preset library: the house library panel -- GALLERY's tree, chevrons and rows
        -- listing circuits rather than cabinets, and dropped down from the toolbar's
        preset field where the preset menu used to open.

        It has no header of its own. GALLERY's carries a title, a search and two icons
        in a band above the list; here that band would sit directly under the preset
        field and say the same thing twice. So while the library is up, the part of it
        lying over the preset field *is* the field -- the same place, size and fill --
        holding the search; the folder icon at its right-hand end is the editor's, the
        same one the closed field shows, laid over the top of this, and
        the list hangs below it. The editor says where the field is: setFieldArea.

        It keeps everything the menu did. New (empty sheet) is the first row; the
        shipped examples are a folder of their own, "Factory presets"; and the user's
        preset folder follows as the tree it actually is, subfolders and all, which the
        flat menu could not show. Choosing or changing that folder is the header's folder
        button, revealing it is a right-click, and the preset in use carries a mark where
        the menu ticked it.

        Like GALLERY's it holds no files and copies nothing: what it lists is whatever is
        in the folder, read fresh each time it opens. And like the menu it replaced, a
        click on a preset loads it -- the panel says what was picked through the `on`
        callbacks and leaves loading, and closing itself, to the editor.
    */
    class PresetLibraryPanel : public juce::Component
    {
    public:
        PresetLibraryPanel();
        ~PresetLibraryPanel() override;

        //======================================================================
        /** The user's preset folder, or none. Listed again from disk. */
        void setFolder (const juce::File&);
        juce::File getFolder() const { return folder; }

        /** The shipped examples, by name, in the order SchematicModel::Examples
            numbers them -- a row reports its index back through onFactoryPresetChosen. */
        void setFactoryPresets (const juce::StringArray& names);

        /** The preset the sheet came from: a file, a factory example's name, or
            neither. Marked in the list, and its folder opened so it can be seen. */
        void setCurrentPreset (const juce::File& file, const juce::String& factoryName);

        /** Reads the folder again. */
        void rescan();

        /** Where the preset field is, in this panel's coordinates. That part of the
            panel stands in for the field while the library is up; the list goes under
            it. Set before asking getPreferredHeight. */
        void setFieldArea (juce::Rectangle<int>);
        juce::Rectangle<int> getFieldArea() const noexcept { return fieldArea; }

        /** Where the list is, in this panel's coordinates: everything under the field. */
        juce::Rectangle<int> getListArea() const;

        /** As tall as the panel wants to be to show every row now shown, field
            included -- the editor caps it to the room under the toolbar, and past that
            it scrolls. */
        int getPreferredHeight() const;

        /** Puts the keyboard in the search field, so typing on opening searches. */
        void focusSearch();

        //======================================================================
        std::function<void()> onNewSheet;
        std::function<void (int index)> onFactoryPresetChosen;
        std::function<void (const juce::File&)> onPresetChosen;

        /** The "Choose a folder..." row. The folder icon in the field is the editor's,
            not this panel's -- it is there whether the library is open or not. */
        std::function<void()> onChooseFolder;

        /** How much of the field's right-hand end the editor's folder icon takes, so
            the search stops short of it. */
        void setFieldTrailingSpace (int pixels);

        /** Escape: the panel asks to be put away. */
        std::function<void()> onDismiss;

        /** A folder opened or shut, so the panel's height may want to change. */
        std::function<void()> onPreferredHeightChanged;

        //======================================================================
        // For the tests, which drive the list the way a click does.
        void setSearchText (const juce::String&);
        int getNumShownRows() const;
        juce::String getRowLabel (int row) const;
        bool isRowFolder (int row) const;
        bool isRowOpen (int row) const;
        bool isRowCurrent (int row) const;
        int getRowDepth (int row) const;

        /** Whether a divider is drawn under this row: the last row of a root group --
            New, the factory folder with its examples, each top-level folder with what
            is open under it -- before the next one. Not between subfolders, and not
            between the presets loose at the top of the folder. */
        bool hasDividerBelow (int row) const;

        /** Whether the next row is a section break: the gap, with a line in it,
            between New, the factory presets and the personal library. */
        bool hasSectionBreakBelow (int row) const;

        /** True for the rows that are only that gap -- nothing to read or click. */
        bool isRowSeparator (int row) const;

        /** A row's height: a preset row's, or a section gap's, which is shorter. */
        int getRowHeight (int row) const;

        /** What clicking a row does. */
        void chooseRow (int row);

        //======================================================================
        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

        /** Only the field and the list. The list may be wider than the field, and the
            corner beside the field belongs to whatever is under it -- which, while the
            library is up, is the dismisser. */
        bool hitTest (int x, int y) override;

    private:
        //======================================================================
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected);
        void listBoxItemClicked (int row, const juce::MouseEvent&);
        void backgroundClicked (const juce::MouseEvent&);

        /** The rows, in a list of their own rather than a juce::ListBox: a ListBox
            gives every row one height, and a section gap the height of a preset row
            left twice the space either side of its line that the list has above its
            first row. Rows are a preset's height; gaps are just tall enough for the
            same space either side of the line. Hover lights a row, as a menu's item
            is lit; a click or right-click goes to the panel. */
        class RowList : public juce::Viewport
        {
        public:
            explicit RowList (PresetLibraryPanel& owner);

            /** Re-reads the rows' heights after `shown` changed. */
            void updateContent();

            int getLitRow() const noexcept { return lit; }
            void setLitRow (int row);

            /** The row at a point in the list's own coordinates, or -1. */
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

        /** What a row is, which decides how it is drawn and what a click on it does. */
        enum class Kind
        {
            newSheet,      // "New (empty sheet)"
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

            /** Only meaningful in `shown`, as in GALLERY: a search opens the folders a
                match lives in without touching which ones somebody opened. */
            bool open = false;

            /** Which root group the row belongs to, numbered in `shown` -- a divider
                goes wherever it changes. */
            int group = 0;
        };

        static bool isFolderKind (Kind kind) { return kind == Kind::factoryFolder || kind == Kind::folder; }

        /** The three sections the list is made of: 0 for New, 1 for the factory
            presets, 2 for the personal library and everything about it. */
        static int sectionOf (Kind kind)
        {
            return kind == Kind::newSheet ? 0 : kind == Kind::factoryFolder || kind == Kind::factoryPreset ? 1 : 2;
        }

        /** The key a folder's open state is kept under: its path, or a fixed one for
            the factory folder, which has none. Kept by key rather than by row so that a
            rescan does not shut what somebody had opened. */
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

        /** Defaults to the toolbar's preset field until the editor says otherwise. */
        juce::Rectangle<int> fieldArea { 0, 0, 302, Theme::buttonSize };
        int fieldTrailingSpace = 0;

        juce::TextEditor search;
        RowList list { *this };


        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetLibraryPanel)
    };
} // namespace Celine
