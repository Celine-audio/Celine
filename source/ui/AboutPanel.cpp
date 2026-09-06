#include "AboutPanel.h"

#include "EmbeddedAssets.h"
#include "Fonts.h"
#include "Theme.h"

#include "../ProductInfo.h"

#include <cmath>

using namespace Celine;

namespace
{
    /** The About window's prose. One string rather than a stack of labels: a licence
        summary trimmed to fit a layout is a licence summary that has been changed.

        Built from ProductInfo and JucePlugin_Name rather than written out, so a new
        plugin inherits a correct notice without editing a word of it. */
    juce::String aboutBodyText()
    {
        const juce::String product { JucePlugin_Name };
        const juce::String company = juce::String::fromUTF8 (ProductInfo::companyName);
        const juce::String source { ProductInfo::repositoryUrl };
        const juce::String juceVersion { ProductInfo::juceVersion };

        juce::String text;

        text
            << juce::String::fromUTF8 ("Copyright \xc2\xa9 ") << ProductInfo::copyrightYear
            << " " << company << ".\n"
            << "\n"
            << "Built " __DATE__ " -- JUCE " << juceVersion << ", C++23.\n"
            << "\n"
            << "\n"
            << "LICENCE\n"
            << "\n"
            << product << " is free software: you may redistribute it and modify it under the terms of the GNU Affero General Public Licence, version 3.\n"
            << "\n"
            << "It comes with ABSOLUTELY NO WARRANTY, to the extent permitted by law.\n"
            << "\n"
            << "Source, including the exact commit this build came from:\n"
            << "    " << source << "\n"
            << "\n"
            << "Full licence text:\n"
            << "    https://www.gnu.org/licenses/agpl-3.0.html\n"
            << "\n"
            << "\n"
            << "WHY AGPL\n"
            << "\n"
            << product << " being free open-source software using the JUCE framework, using its free licence, it inherits its AGPLv3 terms. "
            << product << " is then under the GNU AGPL v3 licence.\n"
            << "\n"
            << "In practice:\n"
            << "\n"
            << "  * Using it costs nothing and obliges nothing. The licence governs distributing the software, not what you make with it. Audio you process through "
            << product << ", and anything you make with it, are your own work.\n"
            << "\n"
            << "  * You may fork, modify and redistribute it, provided you do so under the AGPLv3 licence and pass the source on. You may not relicense it or ship a closed-source build of it.\n"
            << "\n"
            << "  * Anyone you give a binary to is entitled to the corresponding source for that exact build. Development happens in public and each release is built from a tagged commit, which is how that right is served.\n"
            << "\n"
            << "\n"
            << "THIRD-PARTY COMPONENTS\n"
            << "\n"
            << "Bundled inside every build, keeping their own licences rather than " << product << "'s:\n"
            << "\n"
            << juce::String::fromUTF8 (
                   "  JUCE ......................... AGPLv3, \xc2\xa9 Raw Material Software Limited\n"
                   "  clap-juce-extensions ......... MIT, \xc2\xa9 2019-2020 Paul Walker\n"
                   "  Font Awesome Free icons ...... CC BY 4.0, \xc2\xa9 Fonticons, Inc.\n"
                   "  Jura typeface ................ SIL Open Font Licence 1.1, \xc2\xa9 2019 The Jura Project Authors\n"
                   "  JetBrains Mono typeface ...... SIL Open Font Licence 1.1, \xc2\xa9 2020 The JetBrains Mono Project Authors\n"
                   "  Nico Moji typeface ........... SIL Open Font Licence 1.1, \xc2\xa9 2016 The Nico Moji Project Authors\n"
                   "\n"
                   "Libraries JUCE vendors inside its own modules, compiled in as part of JUCE and all permissively licensed:\n"
                   "\n"
                   "  VST\xc2\xae" "3 SDK .................... MIT, \xc2\xa9 2025 Steinberg Media Technologies GmbH\n"
                   "  LunaSVG and PlutoVG .......... MIT. JUCE 9's SVG parser\n"
                   "  LV2 SDK ...................... ISC\n"
                   "  HarfBuzz ..................... MIT\n"
                   "  SheenBidi .................... Apache 2.0\n"
                   "  zlib, pnglib ................. zlib\n"
                   "  jpeglib ...................... Independent JPEG Group\n"
                   "  FLAC, Ogg Vorbis ............. BSD\n"
                   "  AudioUnitSDK ................. Apache 2.0 (macOS builds only)\n"
                   "\n"
                   "VST is a registered trademark of Steinberg Media Technologies GmbH.\n"
                   "\n")
           #if JUCE_ASIO
            // Only when the build actually has it. The SDK is dual-licensed --
            // Steinberg's own terms, or the GPLv3 -- and taking the GPL option is
            // what keeps an ASIO-enabled build distributable under the AGPL at all.
            // Claiming it in a build without ASIO would be as wrong as omitting it
            // from one with it, so the notice follows the compile flag.
            << juce::String::fromUTF8 (
                   "  ASIO\xc2\xae SDK .................... GPLv3 option, \xc2\xa9 2025 Steinberg Media Technologies GmbH\n"
                   "                                 (Windows standalone only)\n"
                   "\n"
                   "ASIO is a registered trademark of Steinberg Media Technologies GmbH. The SDK is\n"
                   "dual-licensed: Steinberg's own licence, or the GPLv3. This build takes the GPL\n"
                   "option, which is what keeps an ASIO-enabled build AGPLv3.\n"
                   "\n")
           #endif
            << juce::String::fromUTF8 (
                   "Font Awesome Free is CC BY 4.0, which makes attribution a condition of use rather than a courtesy.\n"
                   "\n")
            << "Used only to build and test " << product << ":\n"
            << "\n"
            << juce::String::fromUTF8 (
                   "  Pamplejuce ................... MIT, \xc2\xa9 2022 Sudara Williams. The CMake setup\n"
                   "                                 and CI started as this template, and the file\n"
                   "                                 you are reading replaced its licence here\n"
                   "  cmake-includes ............... MIT, \xc2\xa9 Sudara Williams. The shared CMake\n"
                   "                                 modules in cmake/, carried as a submodule\n"
                   "  Catch2 3.8.1 ................. Boost Software Licence 1.0\n"
                   "  CPM.cmake .................... MIT\n"
                   "\n")
            << "The repository's LICENSE and THIRD-PARTY-NOTICES files carry the full account, including the verbatim licence of every bundled work.\n"

            // Whatever this plugin owes on its own account. Empty for most of them.
            << juce::String::fromUTF8 (ProductInfo::extraNotices);

        return text;
    }
}

//==============================================================================
AboutPanel::AboutPanel()
{
    setLookAndFeel (&lookAndFeel);

    // Shown as supplied — these are other people's marks, not our artwork.
    vstMark  = Celine::Assets::drawable ("vst-compatible.png");
    auMark   = Celine::Assets::drawable ("format-au.svg");
    clapMark = Celine::Assets::drawable ("format-clap.png");
    lv2Mark  = Celine::Assets::drawable ("format-lv2.svg");

    // The identity, as artwork, and it leads the window: Apple requires the Audio
    // Units mark to be "clearly subordinate in both size and placement to the primary
    // company or product identity". Placement is the clearer half -- this pair is at
    // the top and read first, the format marks are in the footer. On size, the badge
    // is one 50px square where the identity is a lockup running the better part of
    // 300px across, so the comparison to make is the lockup's, not the house mark's
    // alone. If that ever needs more headroom, grow this pair rather than shrinking
    // the marks, which is why the two sizes were raised together.
    // Until a plugin has a wordmark drawn for it, its name is set in the display
    // face instead, so the window looks finished from the first build.
    wordmarkText.setText (juce::String (JucePlugin_Name).toLowerCase(), juce::dontSendNotification);
    wordmarkText.setFont (Fonts::logo (26.0f));
    wordmarkText.setJustificationType (juce::Justification::centredLeft);
    addChildComponent (wordmarkText);
    wordmarkText.setVisible (wordmark == nullptr);

    // One line under the mark saying what it is, since the mark itself does not.
    // Not the product name again -- that is what the artwork above it already says.
    subtitle.setText (juce::String::fromUTF8 (ProductInfo::tagline)
                          + juce::String::fromUTF8 (" \xc2\xb7 ")
                          + juce::String::fromUTF8 (ProductInfo::companyName),
                      juce::dontSendNotification);
    subtitle.setFont (Fonts::light (12.0f));
    subtitle.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (subtitle);

    // Beside the mark rather than buried in the notices. Which build you are running
    // is the first thing anyone opens this window to find out, and under the AGPL it
    // is also what identifies the source this binary corresponds to. Monospaced,
    // because it is a number to be read off and quoted rather than prose.
    version.setText (JucePlugin_VersionString, juce::dontSendNotification);
    version.setFont (Fonts::mono (13.0f));
    version.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (version);

    body.setMultiLine (true, true);
    body.setReadOnly (true);
    body.setScrollbarsShown (true);
    body.setCaretVisible (false);

    // Read-only still allows Select All and Copy, which is how the source URL
    // gets out of here.
    body.setPopupMenuEnabled (true);

    // JetBrains Mono, which is embedded for exactly this: the notices are a
    // dot-leader table and a proportional face turns them into ragged prose.
    body.setFont (Fonts::mono (13.0f));
    // Transparent, and no outline. The dark panel behind it is the ground -- an editor
    // painting one of its own would be a second rectangle inside the first, and a rule
    // around it the only hard line in the window.
    body.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    body.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    body.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);

    // The kit's scrollbar is near-white, which against this ground is the brightest
    // thing in the window and reads as a control rather than as a position.
    body.setColour (juce::ScrollBar::thumbColourId, Theme::line().withAlpha (0.22f));

    body.setText (aboutBodyText(), false);
    addAndMakeVisible (body);

    // The one action here, in the correction's violet like Export: this is the
    // window's own button, not a piece of chrome.
    addAndMakeVisible (close);

    Theme::palette().addChangeListener (this);
    applyColours();

    // Last, and it matters. setSize fires resized(), which measures each mark to
    // place it -- so called before the artwork is loaded it sizes every one of them
    // to nothing, and they stay that way until something resizes the window again.
    // A dialog that opens at a different size than this hides the bug completely.
    setSize (700, 620);
}

void AboutPanel::applyColours()
{
    // Re-read from the binary each time. Assets::tint writes the colour into the
    // drawable, so a second pass would be colouring the result of the first rather than
    // the artwork.
    logo = Celine::Assets::drawable ("logo.svg");
    wordmark = Celine::Assets::drawable (ProductInfo::wordmarkAsset, Assets::IfMissing::returnNull);

    for (auto* art : { &logo, &wordmark })
        if (*art != nullptr)
            Celine::Assets::tint (**art, Theme::text());

    wordmarkText.setVisible (wordmark == nullptr);

    wordmarkText.setColour (juce::Label::textColourId, Theme::text());
    subtitle.setColour (juce::Label::textColourId, Theme::comment());
    version.setColour (juce::Label::textColourId, Theme::comment());

    body.setColour (juce::TextEditor::textColourId, Theme::textDim());

    // The kit's scrollbar is near-white, which against this ground is the brightest
    // thing in the window and reads as a control rather than as a position.
    body.setColour (juce::ScrollBar::thumbColourId, Theme::line().withAlpha (0.22f));

    // The one action here, in the accent like Export: this is the window's own button,
    // not a piece of chrome.
    close.setColour (juce::TextButton::buttonColourId, Theme::accent());
    close.setColour (juce::TextButton::textColourOffId, Theme::chrome());
}

AboutPanel::~AboutPanel()
{
    Theme::palette().removeChangeListener (this);

    setLookAndFeel (nullptr);
}

void AboutPanel::paint (juce::Graphics& g)
{
    // The masthead keeps the toolbar's aubergine, so the window opens looking like the
    // plugin's own header rather than like a dialog about it.
    g.fillAll (Theme::chrome());

    if (logo != nullptr && ! logoBounds.isEmpty())
        logo->drawWithin (g, logoBounds.toFloat(), juce::RectanglePlacement::centred, 1.0f);

    // Baseline-centred, exactly as the toolbar draws it. A lowercase wordmark almost
    // always has a descender, and a mark centred on its bounding box centres that
    // descender too -- hanging the word visibly above where the eye puts it.
    if (wordmark != nullptr && ! wordmarkBounds.isEmpty())
        Celine::Assets::drawWordmark (g, *wordmark, wordmarkBounds.toFloat());

    // Everything below it stands on the graph's own ground, drawn the way the popup
    // menus and tooltips are: a fill and a rounding, no border. That is what makes it
    // read as a hole cut through to the same surface the spectrum sits on, rather than
    // as a panel laid on top of the chrome -- and it divides the identity from the
    // notices better than the faint rule that used to do it, which was the only hard
    // line in the window.
    g.setColour (Theme::consoleBackground());
    g.fillRoundedRectangle (panelBounds.toFloat(), Theme::cornerRadius);

    for (const auto& mark : { std::pair { vstMark.get(), vstBounds },
                              std::pair { auMark.get(), auBounds },
                              std::pair { clapMark.get(), clapBounds },
                              std::pair { lv2Mark.get(), lv2Bounds } })
        if (mark.first != nullptr && ! mark.second.isEmpty())
            mark.first->drawWithin (g, mark.second.toFloat(), juce::RectanglePlacement::centred, 1.0f);
}

void AboutPanel::resized()
{
    auto area = getLocalBounds().reduced (18);

    // The masthead: both marks fitted to their own aspects and sat on a common
    // centre line, exactly as the toolbar does it, so the two windows agree.
    {
        auto masthead = area.removeFromTop (38);

        const auto place = [&masthead] (const std::unique_ptr<juce::Drawable>& art,
                                        int height, juce::Rectangle<int>& out)
        {
            if (art == nullptr)
                return;

            const auto ink = art->getDrawableBounds();
            const auto aspect = ink.getHeight() > 0.0f ? ink.getWidth() / ink.getHeight() : 1.0f;
            const auto width = juce::roundToInt ((float) height * aspect);

            out = masthead.removeFromLeft (width).withSizeKeepingCentre (width, height);
        };

        place (logo, 38, logoBounds);
        masthead.removeFromLeft (22);

        if (wordmark != nullptr)
            place (wordmark, 27, wordmarkBounds);
        else
            wordmarkText.setBounds (masthead.removeFromLeft (220));

        // Whatever is left of the row, which puts it just past the wordmark.
        masthead.removeFromLeft (14);
        version.setBounds (masthead);
    }

    area.removeFromTop (6);
    subtitle.setBounds (area.removeFromTop (16));

    // Air between the masthead and the panel, which is what separates them now that
    // there is no rule.
    area.removeFromTop (16);

    panelBounds = area;

    // Everything from here sits inside the panel, inset far enough that no corner of
    // it is cut by the rounding -- the same reason the popup menus carry a border of
    // their own.
    area = area.reduced (16, 14);

    auto row = area.removeFromBottom (76);
    close.setBounds (row.removeFromRight (96).withSizeKeepingCentre (96, 32));

    constexpr int gap = 20;

    // What a square mark would stand; the others are scaled from it below.
    constexpr float markSize = 50.0f;

    // Every mark is given the height that puts the same *area* on the page, rather
    // than the same height. Three of these are within a few percent of square (1.07,
    // 1.00, 0.95) and LV2 is 1.59 wide, so matching heights would have let LV2 sprawl
    // half as wide again as its neighbours and read as the loudest of the four. Equal
    // area is what makes a row of differently-shaped marks look evenly weighted. They
    // share one centre line, so they are aligned as well as balanced.
    const auto place = [&row] (const std::unique_ptr<juce::Drawable>& mark,
                               juce::Rectangle<int>& out)
    {
        if (mark == nullptr)
            return;

        const auto ink = mark->getDrawableBounds();
        const auto aspect = ink.getHeight() > 0.0f ? ink.getWidth() / ink.getHeight() : 1.0f;

        const auto height = juce::roundToInt (markSize / std::sqrt (aspect));
        const auto width = juce::roundToInt ((float) height * aspect);

        out = row.removeFromLeft (width).withSizeKeepingCentre (width, height);
    };

    place (vstMark, vstBounds);   row.removeFromLeft (gap);
    place (auMark, auBounds);     row.removeFromLeft (gap);
    place (clapMark, clapBounds); row.removeFromLeft (gap);
    place (lv2Mark, lv2Bounds);

    // Air enough that the notices stop well clear of the marks. They scroll, so the
    // last line visible is usually a part line -- with the footer close underneath,
    // that read as text running into it rather than as a column that continues.
    area.removeFromBottom (22);
    body.setBounds (area);
}

//==============================================================================
void showAboutWindow (juce::Component* associatedComponent)
{
    const juce::String product { JucePlugin_Name };

    auto panel = std::make_unique<AboutPanel>();

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "About " + product;
    options.dialogBackgroundColour = Theme::chrome();
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.componentToCentreAround = associatedComponent;

    auto* raw = panel.get();
    options.content.setOwned (panel.release());

    // Async and self-deleting: a modal loop inside a host is how you hang a DAW.
    auto* window = options.launchAsync();

    // The window, not the content: a DialogWindow sizes itself around whatever it is
    // given, so a constraint set on the panel alone is one the drag never consults.
    if (window != nullptr)
        window->setResizeLimits (AboutPanel::minimumWidth, AboutPanel::minimumHeight, 1100, 1300);

    const juce::Component::SafePointer<juce::DialogWindow> dialog (window);

    raw->close.onClick = [dialog]
    {
        if (dialog != nullptr)
            dialog->exitModalState (0);
    };
}

void AboutPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    lookAndFeel.applyPalette();
    applyColours();
    sendLookAndFeelChange();
    repaint();
}
