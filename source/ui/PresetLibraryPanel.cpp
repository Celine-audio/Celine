#include "PresetLibraryPanel.h"

#include "../PluginProcessor.h"
#include "PluginLookAndFeel.h"
#include <CelineUI/Fonts.h>

namespace Celine
{
    namespace
    {
        // Same dimensions as GALLERY's library panel.
        constexpr int gap = 8;
        constexpr int rowHeight = 28;

        /** A section separator: `gap` + 1 px (a lit row's inset) above the line, the same
            below. */
        constexpr int separatorHeight = 2 * gap + 1;
        constexpr int indentWidth = 16;
        constexpr int chevronWidth = 16;

        /** Row font size. */
        constexpr float fontHeight = 14.0f;
        constexpr float searchFontHeight = 13.0f;

        /** Between the field and the list. */
        constexpr int fieldGap = 4;

        /** Limits, so picking a huge folder costs a moment rather than a hang. */
        constexpr int maximumDepth = 6;
        constexpr int maximumEntries = 6000;

        /** Key for the factory folder's open state. Not a path, so it can't collide. */
        const juce::String factoryKey { "factory:" };

        /** Chevron: down when open, right when closed. */
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

        /** Natural order: "Bass 9" before "Bass 10". */
        void sortNaturally (juce::Array<juce::File>& files)
        {
            std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                       { return a.getFileName().compareNatural (b.getFileName()) < 0; });
        }

        /** The platform's name for revealing a file. */
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
        // Only the field and list are painted.
        setOpaque (false);

        search.setFont (Fonts::light (searchFontHeight));
        search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        search.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        search.setJustification (juce::Justification::centredLeft);
        search.setBorder (juce::BorderSize<int> (0));
        search.setIndents (28, 0); // room for the magnifier, drawn over the field
        search.onTextChange = [this] { applyFilter(); };

        // Return loads the first matching preset.
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

        addAndMakeVisible (list);

        // The factory folder starts open.
        expanded.add (factoryKey);

        applyColours();
        rebuild();
    }

    PresetLibraryPanel::~PresetLibraryPanel() = default;

    void PresetLibraryPanel::applyColours()
    {
        // Transparent: the field painted behind it is the background.
        search.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        search.setColour (juce::TextEditor::textColourId, Theme::text());
        search.setColour (juce::TextEditor::highlightColourId, Theme::accent().withAlpha (0.4f));

        // Set again with the text, since the placeholder colour goes with it.
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

        // Open the folders down to the current preset.
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

        // Folders before files at each level.
        juce::Array<juce::File> subfolders, found;

        for (const auto& entry : juce::RangedDirectoryIterator (directory, false, "*", juce::File::findDirectories))
            if (! entry.isHidden())
                subfolders.add (entry.getFile());

        // One wildcard: the iterator doesn't split on ";".
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
            // Everything down to the first closed folder on each branch.
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
            // Keep matches and every folder above them. Only presets match.
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

        // Root groups, for the dividers: New, the factory folder with its examples, each
        // top-level folder with its open contents, and loose presets together.
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

        // Insert a separator row between sections, only where both neighbours are shown.
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

        // Keep the scroll position and the lit row.
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
        // While searching, the search decides what is open.
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
        // The rows' total height (one row's room when a search finds nothing), plus padding.
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

        // The next real row, skipping separators.
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
        // Only when showing; JUCE asserts otherwise.
        if (search.isShowing())
            search.grabKeyboardFocus();
    }

    //==========================================================================
    void PresetLibraryPanel::setSearchText (const juce::String& text)
    {
        // Filters immediately, rather than waiting for onTextChange.
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
            // The line sits `gap` into the separator row, matching the list's top padding.
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

        // The loaded preset: a dot at the row's end.
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

        // Folders and actions brighter, presets dimmer, notes dimmest.
        g.setColour (entry.kind == Kind::note ? Theme::comment()
                   : lit || current           ? Theme::text()
                   : entry.kind == Kind::preset || entry.kind == Kind::factoryPreset
                                              ? Theme::textDim()
                                              : Theme::text());

        // Truncated with an ellipsis when too long.
        g.setFont (Fonts::light (fontHeight));
        g.drawText (entry.label, bounds.reduced (4.0f, 0.0f), juce::Justification::centredLeft, true);

        // A faint line between root groups inside a section.
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
            // Reveal the right-clicked file, or the preset folder.
            showRevealMenu (entry.kind == Kind::preset || entry.kind == Kind::folder ? entry.file : folder);
            return;
        }

        // Ignore the second click of a double-click on a folder, which would close it again.
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

        // Menus don't inherit a look and feel.
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
        // The search field, filled like the preset field.
        g.setColour (Theme::button());
        g.fillRoundedRectangle (fieldArea.toFloat().reduced (Theme::borderWidth * 0.5f), Theme::cornerRadius);

        // The list body and its outline.
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
        // Magnifying glass, drawn over the search field.
        const auto field = search.getBounds().toFloat();
        const auto lens = juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ field.getX() + 13.0f, field.getCentreY() - 1.0f });

        g.setColour (Theme::comment());
        g.drawEllipse (lens, 1.5f);
        g.drawLine (lens.getRight() - 1.5f, lens.getBottom() - 1.5f, lens.getRight() + 2.5f, lens.getBottom() + 2.5f, 1.6f);
    }

    void PresetLibraryPanel::resized()
    {
        // The field, minus the folder icon's space.
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
        // Only clickable rows light up.
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
        // The row under the pointer lights up.
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
