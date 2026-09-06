#include "ThemePanel.h"

#include "EmbeddedAssets.h"
#include "Fonts.h"
#include "Theme.h"

#include "../ProductInfo.h"

using namespace Celine;

namespace
{
    constexpr int rowHeight = 34;
    constexpr int headingHeight = 30;
    constexpr int swatchWidth = 46;
    constexpr int hexWidth = 82;
    constexpr int gap = 10;

    /** Ink that stays readable on a swatch whatever colour the swatch is. The eye
        reads contrast against lightness rather than against hue, so this asks the
        colour how light it is rather than picking by taste. */
    juce::Colour inkOn (juce::Colour background)
    {
        return background.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff17151a)
                                                           : juce::Colour (0xfff9fbff);
    }
}

//==============================================================================
ThemePanel::Row::Row (Theme::Role roleToEdit, ThemePanel& panel)
    : role (roleToEdit), owner (panel)
{
    const auto& entry = Theme::info()[(size_t) role];

    name.setText (entry.label, juce::dontSendNotification);
    name.setFont (Fonts::light (12.5f));
    name.setColour (juce::Label::textColourId, Theme::textDim());
    name.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (name);

    // Typed as well as picked. Somebody with a hex from a palette they like should not
    // have to find it by eye in a colour wheel.
    hex.setFont (Fonts::mono (11.5f));
    hex.setJustification (juce::Justification::centred);
    hex.setBorder (juce::BorderSize<int> (0));
    hex.setIndents (0, 0);
    hex.setColour (juce::TextEditor::backgroundColourId, Theme::background());
    hex.setColour (juce::TextEditor::textColourId, Theme::textDim());
    hex.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    hex.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    hex.setTooltip ("The colour as a hex value. Paste one in, or read one off.");
    hex.onReturnKey = [this] { applyTypedText(); };
    hex.onFocusLost = [this] { applyTypedText(); };
    addAndMakeVisible (hex);

    refresh();
}

void ThemePanel::Row::refresh()
{
    const auto colour = Theme::colour (role);
    const auto text = "#" + colour.toDisplayString (false).toLowerCase();

    if (hex.getText() != text)
        hex.setText (text, juce::dontSendNotification);

    repaint();
}

void ThemePanel::Row::applyTypedText()
{
    // Whatever was typed goes through the palette's own reader, so the field and a file
    // accept exactly the same spellings. Anything it will not take puts the field back
    // rather than leaving a value on screen that is not the one in force.
    Theme::Palette candidate;
    auto* entries = new juce::DynamicObject();
    entries->setProperty (Theme::info()[(size_t) role].key, hex.getText());

    auto* root = new juce::DynamicObject();
    root->setProperty ("colours", juce::var (entries));

    if (candidate.fromVar (juce::var (root)).wasOk())
        owner.editing = true,
        Theme::palette().set (role, candidate.get (role)),
        owner.editing = false;

    refresh();
}

void ThemePanel::Row::paint (juce::Graphics& g)
{
    const auto colour = Theme::colour (role);

    g.setColour (colour);
    g.fillRoundedRectangle (swatch.toFloat(), Theme::cornerRadius - 2.0f);

    // A hairline in the ink the swatch would carry, so a colour that matches the panel
    // behind it is still a shape rather than a hole.
    g.setColour (inkOn (colour).withAlpha (0.25f));
    g.drawRoundedRectangle (swatch.toFloat().reduced (0.5f), Theme::cornerRadius - 2.0f, 1.0f);
}

void ThemePanel::Row::resized()
{
    auto area = getLocalBounds().reduced (0, 3);

    swatch = area.removeFromRight (swatchWidth);
    area.removeFromRight (gap - 4);
    hex.setBounds (area.removeFromRight (hexWidth).withSizeKeepingCentre (hexWidth, 22));
    area.removeFromRight (gap);

    name.setBounds (area);
}

void ThemePanel::Row::mouseDown (const juce::MouseEvent& event)
{
    if (swatch.contains (event.getPosition()))
        owner.openPickerFor (role, localAreaToGlobal (swatch));
}

//==============================================================================
ThemePanel::ThemePanel()
{
    setLookAndFeel (&lookAndFeel);

    title.setText ("Theme", juce::dontSendNotification);
    title.setFont (Fonts::logo (22.0f));
    title.getProperties().set (keepFontProperty, true);
    title.setColour (juce::Label::textColourId, Theme::text());
    addAndMakeVisible (title);

    subtitle.setText (juce::String::fromUTF8 (
                          "Every colour this plugin draws with. Changes show at once; "
                          "Save keeps them."),
                      juce::dontSendNotification);
    subtitle.setFont (Fonts::light (11.5f));
    subtitle.setColour (juce::Label::textColourId, Theme::comment());
    subtitle.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (subtitle);

    // A row per colour, in the order ThemeRoles.h lists them, with a heading whenever
    // the group changes. That ordering is the file's and the editor's at once, so a
    // colour added there appears here without anybody laying it out.
    juce::String currentGroup;

    for (size_t i = 0; i < Theme::numRoles; ++i)
    {
        const auto& entry = Theme::info()[i];

        if (currentGroup != entry.group)
        {
            currentGroup = entry.group;
            headings.push_back ({ currentGroup.toUpperCase(), {} });
        }

        auto row = std::make_unique<Row> ((Theme::Role) i, *this);
        rows.addAndMakeVisible (*row);
        colourRows.push_back (std::move (row));
    }

    scroller.setViewedComponent (&rows, false);
    scroller.setScrollBarsShown (true, false);
    scroller.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId,
                                               Theme::line().withAlpha (0.22f));
    addAndMakeVisible (scroller);

    importButton.setTooltip ("Open a .celthm file and use the colours in it.");
    importButton.onClick = [this] { importTheme(); };
    addAndMakeVisible (importButton);

    exportButton.setTooltip ("Write these colours to a .celthm file to keep or to share.");
    exportButton.onClick = [this] { exportTheme(); };
    addAndMakeVisible (exportButton);

    saveButton.setTooltip ("Keep these colours. Until this is pressed a change is only "
                           "on screen, which is what lets you try one and walk away from it.");
    saveButton.onClick = [this] { saveTheme(); };
    addAndMakeVisible (saveButton);

    resetButton.setTooltip ("Put every colour back to the one the plugin ships with.");
    resetButton.onClick = [this] { resetTheme(); };
    addAndMakeVisible (resetButton);

    status.setFont (Fonts::light (11.0f));
    status.setColour (juce::Label::textColourId, Theme::comment());
    status.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (status);

    close.setColour (juce::TextButton::buttonColourId, Theme::accent());
    close.setColour (juce::TextButton::textColourOffId, Theme::chrome());
    addAndMakeVisible (close);

    Theme::palette().addChangeListener (this);
    refreshRows();

    // Last, and it matters: setSize fires resized(), which lays out rows that have to
    // exist by then. See the house conventions.
    setSize (520, 640);
}

ThemePanel::~ThemePanel()
{
    // Before anything else: the picker is a desktop window of its own and would
    // otherwise report a colour to a listener that has gone.
    if (picker != nullptr)
        picker->dismiss();

    Theme::palette().removeChangeListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void ThemePanel::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    // The picker, moving the colour it was opened on. Live on purpose: the window
    // behind repaints as the pointer moves, which is the only way to judge a colour
    // against the thing it is for.
    if (auto* selector = dynamic_cast<juce::ColourSelector*> (source))
    {
        if (picking != Theme::Role::count)
        {
            editing = true;
            Theme::palette().set (picking, selector->getCurrentColour().withAlpha (1.0f));
            editing = false;

            for (auto& row : colourRows)
                row->refresh();
        }

        return;
    }

    // Its own edits already show; anything else -- a file, a reset, another window --
    // means the rows are out of date.
    if (! editing)
        refreshRows();

    // The panel wears the theme too, so it repaints itself with everything else.
    lookAndFeel.applyPalette();
    sendLookAndFeelChange();
    repaint();
}

void ThemePanel::refreshRows()
{
    for (auto& row : colourRows)
        row->refresh();

    const auto name = Theme::palette().getName();

    const auto unsaved = Theme::palette().hasUnsavedChanges();

    status.setText (unsaved                      ? juce::String ("Unsaved changes")
                    : Theme::palette().isShipped() ? juce::String ("Shipped colours")
                    : name.isNotEmpty()            ? name
                                                   : juce::String ("Saved"),
                    juce::dontSendNotification);

    // Lit only while there is something to keep, so the button says whether it has
    // anything to do rather than sitting there inviting a pointless write.
    saveButton.setEnabled (unsaved);
}

void ThemePanel::report (const juce::String& message)
{
    status.setText (message, juce::dontSendNotification);
}

//==============================================================================
void ThemePanel::openPickerFor (Theme::Role role, juce::Rectangle<int> swatchOnScreen)
{
    picking = role;

    // JUCE's own picker, in the kit's call-out box -- which the look and feel already
    // draws, so it arrives wearing the plugin rather than the system.
    // The edge gap is generous on purpose. JUCE builds the colour space and the hue
    // strip as square panels and keeps them to itself -- there is no way to round them
    // from out here -- so the next best thing is to stop anything square from being
    // flush with the bubble's rounded edge, where the two shapes fight.
    auto selector = std::make_unique<juce::ColourSelector> (
        juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders
            | juce::ColourSelector::showColourspace,
        12, 8);

    selector->setName (Theme::info()[(size_t) role].label);
    selector->setCurrentColour (Theme::colour (role), juce::dontSendNotification);

    // Room for the three slider rows, which were sitting on the bottom edge.
    selector->setSize (280, 336);
    selector->setLookAndFeel (&lookAndFeel);

    // Transparent, so the call-out bubble is the only ground. Filled, it painted a
    // square of surface() inside a rounded bubble and the corners showed.
    selector->setColour (juce::ColourSelector::backgroundColourId, juce::Colours::transparentBlack);
    selector->setColour (juce::ColourSelector::labelTextColourId, Theme::text());

    // This panel listens, rather than something owned by the box: one picker is open at
    // a time, so `picking` is enough to know which colour is moving, and there is no
    // object whose lifetime has to be threaded through the call-out box.
    selector->addChangeListener (this);

    picker = &juce::CallOutBox::launchAsynchronously (std::move (selector),
                                                      swatchOnScreen, nullptr);

    if (picker != nullptr)
        picker->setLookAndFeel (&lookAndFeel);
}

//==============================================================================
void ThemePanel::importTheme()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Open a theme", Theme::Palette::storedFile().getParentDirectory(),
        Theme::Palette::fileWildcard);

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& browser)
                          {
                              const auto file = browser.getResult();

                              if (! file.existsAsFile())
                                  return;

                              const auto result = Theme::palette().loadFrom (file);

                              if (result.wasOk())
                                  report ("Loaded " + file.getFileName());
                              else
                              {
                                  report (result.getErrorMessage());
                              }
                          });
}

void ThemePanel::exportTheme()
{
    const auto suggested = Theme::palette().getName().isNotEmpty()
                             ? Theme::palette().getName()
                             : juce::String (JucePlugin_Name).toLowerCase();

    chooser = std::make_unique<juce::FileChooser> (
        "Save this theme",
        Theme::Palette::storedFile().getParentDirectory()
            .getChildFile (juce::File::createLegalFileName (suggested)
                           + Theme::Palette::fileExtension),
        Theme::Palette::fileWildcard);

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& browser)
                          {
                              const auto file = browser.getResult();

                              if (file.getFullPathName().isEmpty())
                                  return;

                              // The file it was saved as becomes the theme's name, so
                              // it comes back carrying it rather than as "Edited".
                              Theme::palette().setName (file.getFileNameWithoutExtension());

                              const auto result = Theme::palette().saveTo (file);

                              report (result.wasOk() ? "Saved " + file.getFileName()
                                                     : result.getErrorMessage());

                          });
}

void ThemePanel::saveTheme()
{
    Theme::palette().store();

    // The swatches have not moved; what has changed is whether there is anything left
    // to save, which is what the status line and the button's own state say.
    refreshRows();
}

void ThemePanel::resetTheme()
{
    // Not stored here: the palette writes itself whenever it changes, which is what
    // stops a colour picked in the one place nobody thought to save from being lost.
    Theme::palette().reset();
    report ("Shipped colours");
}

//==============================================================================
void ThemePanel::paint (juce::Graphics& g)
{
    // The masthead keeps the toolbar's aubergine, so the window opens looking like the
    // plugin's own header rather than like a dialog about it.
    g.fillAll (Theme::chrome());

    // Everything below stands on the graph's own ground, the way the About window and
    // the popup menus do.
    const auto panel = scroller.getBounds().expanded (gap, gap);

    g.setColour (Theme::consoleBackground());
    g.fillRoundedRectangle (panel.toFloat(), Theme::cornerRadius);

    // Clipped to the list they belong to.
    //
    // These are painted here rather than by the scrolled component, in its coordinates
    // shifted by the scroll position -- so without this a heading scrolled past the top
    // carries on being drawn above the list, over the subtitle and the footer. It showed
    // up as headings appearing in the middle of the window whenever something made the
    // panel repaint under the colour picker.
    {
        const juce::Graphics::ScopedSaveState clipped (g);
        g.reduceClipRegion (scroller.getBounds());

        g.setColour (Theme::comment());
        g.setFont (Fonts::light (10.0f));

        for (const auto& heading : headings)
            if (! heading.bounds.isEmpty())
                g.drawText (heading.text,
                            heading.bounds.translated (scroller.getX() - scroller.getViewPositionX(),
                                                       scroller.getY() - scroller.getViewPositionY()),
                            juce::Justification::bottomLeft, false);
    }
}

void ThemePanel::resized()
{
    auto area = getLocalBounds().reduced (18, 16);

    title.setBounds (area.removeFromTop (28));
    area.removeFromTop (2);
    subtitle.setBounds (area.removeFromTop (30));
    area.removeFromTop (12);

    auto footer = area.removeFromBottom (36);
    area.removeFromBottom (12);

    close.setBounds (footer.removeFromRight (86).withSizeKeepingCentre (86, 30));
    footer.removeFromRight (gap);
    resetButton.setBounds (footer.removeFromRight (76).withSizeKeepingCentre (76, 30));
    footer.removeFromRight (gap - 4);
    saveButton.setBounds (footer.removeFromRight (70).withSizeKeepingCentre (70, 30));
    footer.removeFromRight (gap - 4);
    exportButton.setBounds (footer.removeFromRight (86).withSizeKeepingCentre (86, 30));
    footer.removeFromRight (gap - 4);
    importButton.setBounds (footer.removeFromRight (86).withSizeKeepingCentre (86, 30));

    status.setBounds (footer.withTrimmedRight (gap));

    scroller.setBounds (area.reduced (gap, gap));

    // The rows, and a heading wherever the group changes. Laid out into the scrolled
    // component, whose height is whatever they come to.
    const auto width = juce::jmax (1, scroller.getWidth() - scroller.getScrollBarThickness());

    auto y = 0;
    size_t heading = 0;
    juce::String currentGroup;

    for (size_t i = 0; i < colourRows.size(); ++i)
    {
        const juce::String group { Theme::info()[i].group };

        if (currentGroup != group)
        {
            currentGroup = group;

            if (heading < headings.size())
                headings[heading++].bounds = { gap, y, width - gap * 2, headingHeight - 6 };

            y += headingHeight;
        }

        colourRows[i]->setBounds (gap, y, width - gap * 2, rowHeight);
        y += rowHeight;
    }

    rows.setSize (width, y + gap);
}

//==============================================================================
void Celine::showThemeWindow (juce::Component* associatedComponent)
{
    auto panel = std::make_unique<ThemePanel>();

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Theme";
    options.dialogBackgroundColour = Theme::chrome();
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.componentToCentreAround = associatedComponent;

    auto* raw = panel.get();
    options.content.setOwned (panel.release());

    auto* window = options.launchAsync();

    if (window != nullptr)
    {
        window->setResizeLimits (ThemePanel::minimumWidth, ThemePanel::minimumHeight,
                                 ThemePanel::minimumWidth * 2, ThemePanel::minimumHeight * 2);

        raw->close.onClick = [window] { window->exitModalState (0); };
    }
}
