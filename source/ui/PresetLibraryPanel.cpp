#include "PresetLibraryPanel.h"

#include "../PluginProcessor.h"
#include "PluginLookAndFeel.h"
#include <CelineUI/Fonts.h>

namespace Celine
{
    namespace
    {
        // GALLERY's numbers, so the two panels are one design rather than two that
        // resemble each other.
        constexpr int gap = 8;
        constexpr int rowHeight = 28;

        /** A section gap: the space the list has above its first lit row (`gap`, and
            the pixel a lit row is inset), then the line, then the same space again. */
        constexpr int separatorHeight = 2 * gap + 1;
        constexpr int indentWidth = 16;
        constexpr int chevronWidth = 16;

        /** Larger than GALLERY's 11.5: these rows are read as a menu's are, at the
            size the toolbar's own lettering reads at. */
        constexpr float fontHeight = 14.0f;
        constexpr float searchFontHeight = 13.0f;

        /** Between the field and the list hanging under it. */
        constexpr int fieldGap = 4;

        /** A preset folder is somebody's circuits, not their home directory; both of
            these exist so pointing it at the wrong thing costs a moment, not a hang. */
        constexpr int maximumDepth = 6;
        constexpr int maximumEntries = 6000;

        /** The key the factory folder's open state is kept under. Not a path, so no
            folder on disk can ever collide with it. */
        const juce::String factoryKey { "factory:" };

        /** GALLERY's chevron: down when the folder is open, pointing at the row when it
            is shut. */
        void drawChevron (juce::Graphics& g, juce::Rectangle<float> box, bool open, juce::Colour colour)
        {
            const auto reach = juce::jmin (box.getWidth(), box.getHeight()) * 0.3f;

            juce::Path chevron;
            chevron.startNewSubPath (-reach, -reach * 0.55f);
            chevron.lineTo (0.0f, reach * 0.55f);
            chevron.lineTo (reach, -reach * 0.55f);

            chevron.applyTransform (juce::AffineTransform::rotation (open ? 0.0f : -juce::MathConstants<float>::halfPi)
                                        .translated (box.getCentre()));

            g.setColour (colour);
            g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
        }

        /** Natural order, as the preset menu used: "Bass 10" after "Bass 9". */
        void sortNaturally (juce::Array<juce::File>& files)
        {
            std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                       { return a.getFileName().compareNatural (b.getFileName()) < 0; });
        }

        /** What the platform calls showing a file where it lives. */
        juce::String revealCommandName()
        {
           #if JUCE_MAC
            return "Reveal in Finder";
           #elif JUCE_WINDOWS
            return "Show in Explorer";
           #else
            return "Show in file manager";
           #endif
        }
    } // namespace

    //==========================================================================
    PresetLibraryPanel::PresetLibraryPanel()
    {
        // Rounded, and narrower at the top than below: what is not field or list shows
        // what is behind it.
        setOpaque (false);

        search.setFont (Fonts::light (searchFontHeight));
        search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        search.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        search.setJustification (juce::Justification::centredLeft);
        search.setBorder (juce::BorderSize<int> (0));
        search.setIndents (28, 0); // room for the magnifier, drawn over the field
        search.onTextChange = [this] { applyFilter(); };

        // A search is asking for one preset: Return takes the first one it found.
        search.onReturnKey = [this]
        {
            for (int row = 0; row < shown.size(); ++row)
            {
                const auto kind = shown.getReference (row).kind;

                if (kind == Kind::preset || kind == Kind::factoryPreset)
                {
                    chooseRow (row);
                    return;
                }
            }
        };

        search.onEscapeKey = [this] { if (onDismiss) onDismiss(); };
        addAndMakeVisible (search);

        // No icons of its own. GALLERY's header carries a folder and a save button;
        // here the toolbar's Save sits just left of the field, and the folder icon is
        // the editor's, at the field's right-hand end whether this is open or not.

        addAndMakeVisible (list);

        // The shipped examples are what a fresh install has, so their folder starts open.
        expanded.add (factoryKey);

        applyColours();
        rebuild();
    }

    PresetLibraryPanel::~PresetLibraryPanel() = default;

    void PresetLibraryPanel::applyColours()
    {
        // No ground of its own: the field behind it is painted in the preset field's
        // colour, so the search reads as that field taking typing.
        search.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        search.setColour (juce::TextEditor::textColourId, Theme::text());
        search.setColour (juce::TextEditor::highlightColourId, Theme::accent().withAlpha (0.4f));

        // The placeholder's ink goes in with its text, so it is set again here.
        search.setTextToShowWhenEmpty (folder.isDirectory() ? "Search in " + folder.getFileName() + "..."
                                                            : "Search presets...",
                                       Theme::comment());
    }

    //==========================================================================
    void PresetLibraryPanel::setFolder (const juce::File& newFolder)
    {
        folder = newFolder;
        rescan();
    }

    void PresetLibraryPanel::setFactoryPresets (const juce::StringArray& names)
    {
        if (factoryNames == names)
            return;

        factoryNames = names;
        rebuild();
    }

    void PresetLibraryPanel::setCurrentPreset (const juce::File& file, const juce::String& factoryName)
    {
        currentFile = file;
        currentFactoryName = factoryName;

        // Opened down to it, so opening the library shows where you are rather than a
        // tree of shut folders with the preset you are on somewhere inside one.
        if (factoryName.isNotEmpty())
            expanded.addIfNotAlreadyThere (factoryKey);

        if (file != juce::File{} && folder.isDirectory() && file.isAChildOf (folder))
            for (auto parent = file.getParentDirectory(); parent != folder && parent.isAChildOf (folder);
                 parent = parent.getParentDirectory())
                expanded.addIfNotAlreadyThere (parent.getFullPathName());

        applyFilter();
    }

    void PresetLibraryPanel::rescan()
    {
        applyColours();
        search.setTooltip (folder.isDirectory() ? folder.getFullPathName() : juce::String());
        rebuild();
    }

    void PresetLibraryPanel::rebuild()
    {
        entries.clear();

        entries.add ({ Kind::newSheet, {}, "New empty sheet" });
        entries.add ({ Kind::factoryFolder, {}, "Factory presets" });

        for (int i = 0; i < factoryNames.size(); ++i)
            entries.add ({ Kind::factoryPreset, {}, factoryNames[i], 1, i });

        if (! folder.isDirectory())
        {
            entries.add ({ Kind::chooseFolder, {}, "Choose a folder for your own presets..." });
        }
        else
        {
            const auto before = entries.size();
            scan (folder, 0);

            if (entries.size() == before)
                entries.add ({ Kind::note, {}, "No presets in " + folder.getFileName() + " yet" });
        }

        applyFilter();
    }

    void PresetLibraryPanel::scan (const juce::File& directory, int depth)
    {
        if (depth > maximumDepth || entries.size() >= maximumEntries)
            return;

        // Folders before files at each level, the way every browser does it, and
        // GALLERY's does.
        juce::Array<juce::File> subfolders, found;

        for (const auto& entry : juce::RangedDirectoryIterator (directory, false, "*", juce::File::findDirectories))
            if (! entry.isHidden())
                subfolders.add (entry.getFile());

        // One wildcard, not a ";" list: findChildFiles and this iterator match the
        // semicolon literally, unlike a FileChooser.
        for (const auto& entry : juce::RangedDirectoryIterator (directory, false,
                                                                juce::String ("*") + PluginProcessor::circuitFileExtension,
                                                                juce::File::findFiles))
            if (! entry.isHidden())
                found.add (entry.getFile());

        sortNaturally (subfolders);
        sortNaturally (found);

        for (const auto& child : subfolders)
        {
            entries.add ({ Kind::folder, child, child.getFileName(), depth });
            scan (child, depth + 1);
        }

        for (const auto& file : found)
            entries.add ({ Kind::preset, file, file.getFileNameWithoutExtension(), depth });
    }

    juce::String PresetLibraryPanel::keyFor (const Entry& entry) const
    {
        return entry.kind == Kind::factoryFolder ? factoryKey : entry.file.getFullPathName();
    }

    bool PresetLibraryPanel::isCurrent (const Entry& entry) const
    {
        if (entry.kind == Kind::factoryPreset)
            return currentFactoryName.isNotEmpty() && entry.label == currentFactoryName;

        return entry.kind == Kind::preset && currentFile != juce::File{} && entry.file == currentFile;
    }

    void PresetLibraryPanel::applyFilter()
    {
        const auto wanted = search.getText().trim();
        const auto previousCount = shown.size();

        shown.clear();

        if (wanted.isEmpty())
        {
            // Everything down to the first shut folder on each branch, as GALLERY's.
            auto hidden = -1;

            for (const auto& entry : entries)
            {
                if (hidden >= 0 && entry.depth > hidden)
                    continue;

                hidden = -1;

                auto row = entry;
                row.open = isFolderKind (entry.kind) && isExpanded (entry);
                shown.add (row);

                if (isFolderKind (entry.kind) && ! row.open)
                    hidden = entry.depth;
            }
        }
        else
        {
            // A match and every folder above it, so a hit still says where it lives --
            // and only presets match: New and the folder prompt are not what a search
            // is looking for.
            juce::Array<int> open;
            juce::Array<bool> keep;
            keep.insertMultiple (0, false, entries.size());

            for (int i = 0; i < entries.size(); ++i)
            {
                const auto& entry = entries.getReference (i);

                if (entry.kind == Kind::newSheet || entry.kind == Kind::chooseFolder || entry.kind == Kind::note)
                    continue;

                open.removeRange (entry.depth, open.size());

                if (isFolderKind (entry.kind))
                {
                    open.add (i);
                    continue;
                }

                if (! entry.label.containsIgnoreCase (wanted))
                    continue;

                keep.set (i, true);

                for (const auto ancestor : open)
                    keep.set (ancestor, true);
            }

            for (int i = 0; i < entries.size(); ++i)
            {
                if (! keep[i])
                    continue;

                auto row = entries[i];
                row.open = isFolderKind (row.kind);
                shown.add (row);
            }
        }

        // Root groups, for the dividers: New is one, the factory folder with its
        // examples another, each top-level folder with what is open under it another,
        // and the presets loose at the top of the folder one between them.
        {
            int group = 0;
            bool looseGroup = false;

            for (auto& row : shown)
            {
                if (row.depth == 0)
                {
                    const bool loose = ! isFolderKind (row.kind) && row.kind != Kind::newSheet;

                    if (! (loose && looseGroup))
                        ++group;

                    looseGroup = loose;
                }

                row.group = group;
            }
        }

        // A gap between the three sections -- New, the factory presets, the library --
        // as a row of its own with a line through its middle: space either side of the
        // line is what tells three kinds of thing apart, where a heavy bar under the
        // last row only shouted. Only between two sections both showing, so a search
        // that leaves one section has no gap at either end.
        {
            juce::Array<Entry> spaced;

            for (int i = 0; i < shown.size(); ++i)
            {
                if (i > 0 && sectionOf (shown.getReference (i).kind) != sectionOf (shown.getReference (i - 1).kind))
                {
                    Entry spacer;
                    spacer.kind = Kind::separator;
                    spacer.group = shown.getReference (i - 1).group;
                    spaced.add (spacer);
                }

                spaced.add (shown.getReference (i));
            }

            shown.swapWith (spaced);
        }

        // Rows only ever come and go below the folder that changed, so the view and
        // the lit row are held where they were -- the pointer is still over that row.
        const auto position = list.getViewPosition();
        const auto lit = list.getLitRow();

        list.updateContent();
        list.setViewPosition (position);
        list.setLitRow (juce::isPositiveAndBelow (lit, shown.size()) ? lit : -1);

        repaint();

        if (shown.size() != previousCount && onPreferredHeightChanged)
            onPreferredHeightChanged();
    }

    void PresetLibraryPanel::toggle (const Entry& entry)
    {
        // While a search runs it decides what is open, so a click would change a set
        // nothing on screen is drawn from.
        if (search.getText().isNotEmpty())
            return;

        const auto key = keyFor (entry);

        if (expanded.contains (key))
            expanded.removeString (key);
        else
            expanded.add (key);

        applyFilter();
    }

    void PresetLibraryPanel::chooseRow (int row)
    {
        if (! juce::isPositiveAndBelow (row, shown.size()))
            return;

        const auto entry = shown[row];

        switch (entry.kind)
        {
            case Kind::newSheet:      if (onNewSheet) onNewSheet(); break;
            case Kind::factoryPreset: if (onFactoryPresetChosen) onFactoryPresetChosen (entry.factoryIndex); break;
            case Kind::preset:        if (onPresetChosen) onPresetChosen (entry.file); break;
            case Kind::chooseFolder:  if (onChooseFolder) onChooseFolder(); break;
            case Kind::factoryFolder:
            case Kind::folder:        toggle (entry); break;
            case Kind::note:
            case Kind::separator:     break;
        }
    }

    int PresetLibraryPanel::getPreferredHeight() const
    {
        // Exactly the rows there are -- one row's room when a search finds nothing, to
        // say so -- with the same padding above and below them.
        int rows = 0;

        for (int row = 0; row < shown.size(); ++row)
            rows += getRowHeight (row);

        return fieldArea.getBottom() + fieldGap + 2 * gap + juce::jmax (rowHeight, rows);
    }

    void PresetLibraryPanel::setFieldTrailingSpace (int pixels)
    {
        if (fieldTrailingSpace == pixels)
            return;

        fieldTrailingSpace = pixels;
        resized();
    }

    bool PresetLibraryPanel::hasDividerBelow (int row) const
    {
        if (! juce::isPositiveAndBelow (row, shown.size()) || shown.getReference (row).kind == Kind::separator)
            return false;

        // The next real row: a section gap sits between groups, not in one.
        int next = row + 1;

        while (juce::isPositiveAndBelow (next, shown.size()) && shown.getReference (next).kind == Kind::separator)
            ++next;

        return juce::isPositiveAndBelow (next, shown.size())
            && shown.getReference (row).group != shown.getReference (next).group;
    }

    bool PresetLibraryPanel::hasSectionBreakBelow (int row) const
    {
        return isRowSeparator (row + 1);
    }

    bool PresetLibraryPanel::isRowSeparator (int row) const
    {
        return juce::isPositiveAndBelow (row, shown.size()) && shown.getReference (row).kind == Kind::separator;
    }

    int PresetLibraryPanel::getRowHeight (int row) const
    {
        return isRowSeparator (row) ? separatorHeight : rowHeight;
    }

    void PresetLibraryPanel::setFieldArea (juce::Rectangle<int> area)
    {
        if (fieldArea == area)
            return;

        fieldArea = area;
        resized();
        repaint();
    }

    juce::Rectangle<int> PresetLibraryPanel::getListArea() const
    {
        return getLocalBounds().withTop (fieldArea.getBottom() + fieldGap);
    }

    bool PresetLibraryPanel::hitTest (int x, int y)
    {
        return fieldArea.contains (x, y) || getListArea().contains (x, y);
    }

    void PresetLibraryPanel::focusSearch()
    {
        // Only on screen: there is nobody to type, and JUCE asserts, otherwise.
        if (search.isShowing())
            search.grabKeyboardFocus();
    }

    //==========================================================================
    void PresetLibraryPanel::setSearchText (const juce::String& text)
    {
        // Filtered here rather than through onTextChange, which a TextEditor delivers
        // later, from the message loop.
        search.setText (text, false);
        applyFilter();
    }

    int PresetLibraryPanel::getNumShownRows() const { return shown.size(); }

    juce::String PresetLibraryPanel::getRowLabel (int row) const
    {
        return juce::isPositiveAndBelow (row, shown.size()) ? shown[row].label : juce::String();
    }

    bool PresetLibraryPanel::isRowFolder (int row) const
    {
        return juce::isPositiveAndBelow (row, shown.size()) && isFolderKind (shown[row].kind);
    }

    bool PresetLibraryPanel::isRowOpen (int row) const
    {
        return juce::isPositiveAndBelow (row, shown.size()) && shown[row].open;
    }

    bool PresetLibraryPanel::isRowCurrent (int row) const
    {
        return juce::isPositiveAndBelow (row, shown.size()) && isCurrent (shown[row]);
    }

    int PresetLibraryPanel::getRowDepth (int row) const
    {
        return juce::isPositiveAndBelow (row, shown.size()) ? shown[row].depth : -1;
    }

    //==========================================================================
    void PresetLibraryPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool lit)
    {
        if (! juce::isPositiveAndBelow (row, shown.size()))
            return;

        const auto& entry = shown.getReference (row);

        if (entry.kind == Kind::separator)
        {
            // As far from the rows either side as the list's first row is from the
            // list's own edge: a lit row is inset a pixel and the list `gap` inside
            // the body, so the line sits `gap` into a gap `2 * gap + 1` tall.
            g.setColour (Theme::line().withAlpha (0.16f));
            g.fillRect (juce::Rectangle<float> (8.0f, (float) gap, (float) width - 16.0f, 1.0f));
            return;
        }

        const bool clickable = entry.kind != Kind::note;
        const bool current = isCurrent (entry);

        auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (2.0f, 1.0f);

        if (lit && clickable)
        {
            g.setColour (Theme::accent().withAlpha (0.75f));
            g.fillRoundedRectangle (bounds, Theme::cornerRadius - 2.0f);
        }

        // The preset in use, marked where the menu ticked it: a dot at the far end, so
        // the name it belongs to is never moved about by it.
        if (current)
        {
            const auto mark = bounds.removeFromRight (16.0f);
            g.setColour (lit ? Theme::text() : Theme::accent());
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (mark.getCentre()));
        }

        bounds.removeFromLeft ((float) (entry.depth * indentWidth));
        const auto chevron = bounds.removeFromLeft ((float) chevronWidth);

        if (isFolderKind (entry.kind))
            drawChevron (g, chevron, entry.open, Theme::comment());

        // Folders and actions are somewhere to go or something to do, drawn the brighter;
        // presets the dimmer, so the eye runs down the names -- as in GALLERY. A note is
        // the quietest of all, since there is nothing to click.
        g.setColour (entry.kind == Kind::note ? Theme::comment()
                   : lit || current           ? Theme::text()
                   : entry.kind == Kind::preset || entry.kind == Kind::factoryPreset
                                              ? Theme::textDim()
                                              : Theme::text());

        // Cut at the row's end with an ellipsis when it does not fit: the list is only
        // as wide as the preset field above it. The mark, if any, was taken off the row
        // first, so a long name stops short of it rather than running under it.
        g.setFont (Fonts::light (fontHeight));
        g.drawText (entry.label, bounds.reduced (4.0f, 0.0f), juce::Justification::centredLeft, true);

        // A faint hairline under the last row of a root group, kept in from both ends,
        // between folders inside one section, which are only neighbours. Between the
        // sections themselves the separator row does the dividing.
        if (hasDividerBelow (row) && ! hasSectionBreakBelow (row))
        {
            g.setColour (Theme::line().withAlpha (0.06f));
            g.fillRect (juce::Rectangle<float> (14.0f, (float) height - 1.0f, (float) width - 28.0f, 1.0f));
        }
    }

    void PresetLibraryPanel::listBoxItemClicked (int row, const juce::MouseEvent& event)
    {
        if (! juce::isPositiveAndBelow (row, shown.size()))
            return;

        const auto entry = shown[row];

        if (event.mods.isPopupMenu())
        {
            // Reveal what was right-clicked if it is on disk, and the preset folder
            // otherwise -- which is where the menu's "Reveal preset folder" went.
            showRevealMenu (entry.kind == Kind::preset || entry.kind == Kind::folder ? entry.file : folder);
            return;
        }

        // The second press of a double-click arrives here too, and would shut the folder
        // the first had just opened.
        if (event.getNumberOfClicks() > 1 && isFolderKind (entry.kind))
            return;

        chooseRow (row);
    }

    void PresetLibraryPanel::backgroundClicked (const juce::MouseEvent& event)
    {
        if (event.mods.isPopupMenu())
            showRevealMenu (folder);
    }

    void PresetLibraryPanel::showRevealMenu (const juce::File& target)
    {
        if (! target.exists())
            return;

        juce::PopupMenu menu;
        menu.addItem (revealCommandName(), [target] { target.revealToUser(); });

        // A menu has no parent to inherit a look from, so it is told this one's.
        menu.setLookAndFeel (&getLookAndFeel());
        menu.showMenuAsync (juce::PopupMenu::Options());
    }

    bool PresetLibraryPanel::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            if (onDismiss)
                onDismiss();

            return true;
        }

        return false;
    }

    //==========================================================================
    void PresetLibraryPanel::paint (juce::Graphics& g)
    {
        // The field, filled exactly as the preset field is, so opening the library does
        // not change what is in the toolbar -- only that it now takes typing.
        g.setColour (Theme::button());
        g.fillRoundedRectangle (fieldArea.toFloat().reduced (Theme::borderWidth * 0.5f), Theme::cornerRadius);

        // The list: GALLERY's body, and a faint rule, since it drops down over a sheet
        // of nearly its colour and without an edge the two run together.
        const auto body = getListArea().toFloat();

        g.setColour (Theme::background());
        g.fillRoundedRectangle (body, Theme::cornerRadius);

        g.setColour (Theme::line().withAlpha (0.15f));
        g.drawRoundedRectangle (body.reduced (0.5f), Theme::cornerRadius, 1.0f);

        if (! shown.isEmpty())
            return;

        g.setColour (Theme::comment());
        g.setFont (Fonts::light (fontHeight));
        g.drawFittedText ("Nothing matches that", body.reduced (16.0f, 0.0f).toNearestInt(),
                          juce::Justification::centred, 1);
    }

    void PresetLibraryPanel::paintOverChildren (juce::Graphics& g)
    {
        // GALLERY's magnifying glass, drawn over the field: a TextEditor paints its
        // whole ground.
        const auto field = search.getBounds().toFloat();
        const auto lens = juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ field.getX() + 13.0f, field.getCentreY() - 1.0f });

        g.setColour (Theme::comment());
        g.drawEllipse (lens, 1.5f);
        g.drawLine (lens.getRight() - 1.5f, lens.getBottom() - 1.5f, lens.getRight() + 2.5f, lens.getBottom() + 2.5f, 1.6f);
    }

    void PresetLibraryPanel::resized()
    {
        // The field, less the right-hand end the editor's folder icon stands in.
        auto field = fieldArea;
        field.removeFromRight (fieldTrailingSpace);
        search.setBounds (field);

        list.setBounds (getListArea().reduced (gap));
    }

    //==========================================================================
    PresetLibraryPanel::RowList::RowList (PresetLibraryPanel& owner) : panel (owner)
    {
        setViewedComponent (&content, false);
        setScrollBarsShown (true, false);
        setScrollBarThickness (8);
        setWantsKeyboardFocus (false);
    }

    void PresetLibraryPanel::RowList::updateContent()
    {
        tops.assign (1, 0);

        for (int row = 0; row < panel.shown.size(); ++row)
            tops.push_back (tops.back() + panel.getRowHeight (row));

        resized();
        content.repaint();
    }

    void PresetLibraryPanel::RowList::resized()
    {
        juce::Viewport::resized();

        const int total = tops.empty() ? 0 : tops.back();
        const bool scrolls = total > getHeight();
        content.setSize (getWidth() - (scrolls ? getScrollBarThickness() : 0), total);
    }

    void PresetLibraryPanel::RowList::setLitRow (int row)
    {
        // Only what can be clicked lights: not a gap, not a note.
        if (juce::isPositiveAndBelow (row, panel.shown.size()))
        {
            const auto kind = panel.shown.getReference (row).kind;

            if (kind == Kind::separator || kind == Kind::note)
                row = -1;
        }
        else
        {
            row = -1;
        }

        if (row != lit)
        {
            lit = row;
            content.repaint();
        }
    }

    int PresetLibraryPanel::RowList::getRowAt (int y) const
    {
        for (size_t row = 0; row + 1 < tops.size(); ++row)
            if (y >= tops[row] && y < tops[row + 1])
                return (int) row;

        return -1;
    }

    void PresetLibraryPanel::RowList::Content::paint (juce::Graphics& g)
    {
        const auto clip = g.getClipBounds();
        const auto& rowTops = list.tops;

        for (size_t row = 0; row + 1 < rowTops.size(); ++row)
        {
            if (rowTops[row + 1] <= clip.getY() || rowTops[row] >= clip.getBottom())
                continue;

            juce::Graphics::ScopedSaveState state (g);
            g.setOrigin (0, rowTops[row]);
            g.reduceClipRegion (0, 0, getWidth(), rowTops[row + 1] - rowTops[row]);
            list.panel.paintListBoxItem ((int) row, g, getWidth(), rowTops[row + 1] - rowTops[row], (int) row == list.lit);
        }
    }

    void PresetLibraryPanel::RowList::Content::mouseMove (const juce::MouseEvent& event)
    {
        // The row under the pointer is lit, as a menu's item is: this opens where the
        // preset menu did, and is read the way it was.
        list.setLitRow (list.getRowAt (event.y));
    }

    void PresetLibraryPanel::RowList::Content::mouseUp (const juce::MouseEvent& event)
    {
        if (! contains (event.getPosition()))
            return;

        const int row = list.getRowAt (event.y);

        if (row >= 0)
            list.panel.listBoxItemClicked (row, event);
        else
            list.panel.backgroundClicked (event);
    }
} // namespace Celine
