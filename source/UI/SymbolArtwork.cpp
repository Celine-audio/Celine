#include "SymbolArtwork.h"

#include <BinaryData.h>

#include <array>
#include <map>
#include <memory>
#include <vector>

namespace SchematicUI
{
    using namespace SchematicModel;

    namespace
    {
        /** SVG units per grid square. Baked into the artwork, so it lives here
            and in the header's convention note and nowhere else. */
        constexpr float unitsPerGrid = 10.0f;

        /** The stroke width the artwork calls normal. */
        constexpr float baselineStroke = 1.2f;

        //======================================================================
        /** One slot per file. The *index* is the identity of a symbol as far as
            the drawing is concerned; the filename is needed once, when the file
            is parsed, and never again.

            That split is the whole point. The canvas asks for artwork once per
            part per repaint, and the first version of this answered by building
            a juce::String, searching a table of names for it, and hashing it
            into a map -- three allocations per part per frame in the one
            function that is supposed to allocate nothing.

            Order matches assetNames below, which a static_assert and a test both
            hold to. */
        enum class Asset
        {
            Ground, Input, Output, Resistor,
            Capacitor, CapacitorPolarised, Inductor, Potentiometer,
            SwitchClosed, SwitchOpen, SpdtA, SpdtB,
            VoltageSource, Diode, DiodeZener, DiodeSchottky, DiodeLed,
            TransistorNpn, TransistorPnp, Triode, OpAmp,
            JfetN, JfetP, VacuumDiode, Pentode,
            Transformer, TransformerCt,
            Scope,
            count,
        };

        constexpr int assetCount = static_cast<int>(Asset::count);

        const char* const assetNames[] = {
            "ground", "input", "output", "resistor",
            "capacitor", "capacitor-polarised", "inductor", "potentiometer",
            "switch-closed", "switch-open", "spdt-a", "spdt-b",
            "voltage-source", "diode", "diode-zener", "diode-schottky", "diode-led",
            "transistor-npn", "transistor-pnp", "triode", "opamp",
            "jfet-n", "jfet-p", "vacuum-diode", "pentode",
            "transformer", "transformer-ct",
            "scope",
        };

        static_assert(std::size(assetNames) == (size_t) assetCount,
                      "every Asset needs a filename, in the same order");

        /** No artwork: Text is its own text, Rectangle a dragged frame. */
        constexpr int noAsset = -1;

        //======================================================================
        /** Pulls the paths out of a parsed SVG, in grid units.
            Recursive because a drawing program will nest things in groups even
            when the artwork is flat. */
        /** The union of every shape's path, grown by half a stroke so the ink
            is inside the box rather than straddling it.

            Written out by hand rather than with Rectangle::getUnion, for the
            reason Schematic::getElementBounds spells out: a horizontal line has
            zero height, JUCE counts a zero-sized rectangle as empty, and
            getUnion answers with the *other* operand whenever either side is
            empty -- so a symbol made of straight lines would measure as one of
            them. */
        juce::Rectangle<float> measure(const SymbolArtwork& artwork)
        {
            bool first = true;
            float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;

            for (const auto& shape : artwork.shapes)
            {
                if (shape.path.isEmpty())
                    continue;

                // Half the *authored* weight, in grid units. The canvas strokes
                // at a floor that varies with zoom, so this is the shape's own
                // idea of how fat it is, not the view's.
                const auto half = shape.filled ? 0.0f
                                               : 0.5f * shape.strokeScale * baselineStroke / unitsPerGrid;

                const auto box = shape.path.getBounds().expanded(half);

                if (first)
                {
                    minX = box.getX(); minY = box.getY();
                    maxX = box.getRight(); maxY = box.getBottom();
                    first = false;
                    continue;
                }

                minX = juce::jmin(minX, box.getX());
                minY = juce::jmin(minY, box.getY());
                maxX = juce::jmax(maxX, box.getRight());
                maxY = juce::jmax(maxY, box.getBottom());
            }

            return first ? juce::Rectangle<float>()
                         : juce::Rectangle<float>(minX, minY, maxX - minX, maxY - minY);
        }

        void collect(const juce::Drawable& drawable, SymbolArtwork& out,
                     juce::AffineTransform accumulated)
        {
            // getDrawableTransform rather than getTransform: JUCE 9 made the
            // latter protected and virtual, and this one public. They return the
            // same member. DrawableText overrides the virtual one, which would
            // matter if artwork carried text -- it does not, and could not: the
            // lettering on this sheet is drawn with a font so it stays upright
            // when a part is turned, which is the whole reason it is in code.
            const auto here = drawable.getDrawableTransform().followedBy(accumulated);

            if (const auto* shape = dynamic_cast<const juce::DrawablePath*>(&drawable))
            {
                SymbolArtwork::Shape s;
                s.path = shape->getPath();
                s.path.applyTransform(here.scaled(1.0f / unitsPerGrid, 1.0f / unitsPerGrid));

                const auto stroke = shape->getStrokeType().getStrokeThickness();

                // No stroke means the path is a filled one -- a diode's
                // triangle, an arrowhead, a contact dot.
                s.filled = stroke <= 0.0f;
                s.strokeScale = s.filled ? 1.0f : stroke / baselineStroke;

                out.shapes.push_back(std::move(s));
            }

            // Children by index. JUCE 9 stopped deriving Drawable from
            // Component, so a composite no longer has Component::getChildren()
            // and there is nothing to downcast -- getChild() hands back a
            // Drawable already, which is what this always wanted.
            if (const auto* composite = dynamic_cast<const juce::DrawableComposite*>(&drawable))
                for (int i = 0; i < composite->getNumChildren(); ++i)
                    collect(composite->getChild(i), out, here);
        }

        /** The viewBox origin, which the SVG parser normalises away.

            JUCE 9 replaced its own SVG parser with LunaSVG, and LunaSVG maps the
            viewBox onto a viewport starting at (0,0) -- so for this project's
            `viewBox="-60 -60 120 120"` every path comes back carrying a
            translation of (+60,+60), and the artwork lands in the first quadrant
            instead of straddling the origin. Every symbol was six grid squares
            down and to the right of where its pins are drawn.

            Undone here rather than compensated for further down, because the
            convention in SymbolArtwork.h is that (0,0) is the part's centre, and
            that has to be true of the paths themselves: the canvas draws pins at
            the positions in Element.h and strokes the cached paths beside them,
            with nothing in between to reconcile the two.

            Read from the file rather than assumed, even though the convention
            fixes it at -60. A hardcoded 60 would silently mis-place any artwork
            that used a different box, which is exactly the failure the
            convention exists to prevent -- and it would do it without a
            compiler or a test saying anything. */
        juce::Point<float> viewBoxOrigin(const void* data, int size)
        {
            const auto text = juce::String::createStringFromData(data, size);

            if (const auto xml = juce::XmlDocument::parse(text))
            {
                auto numbers = juce::StringArray::fromTokens(
                    xml->getStringAttribute("viewBox").trim(), " ,", "");
                numbers.removeEmptyStrings();

                if (numbers.size() >= 2)
                    return {numbers[0].getFloatValue(), numbers[1].getFloatValue()};
            }

            return {};
        }

        /** Loads one asset by *filename*, or leaves it empty if there isn't one.

            Matched on the original filename rather than on the C++ identifier
            JUCE derives from it. That derivation is not the obvious one --
            "capacitor-polarised.svg" becomes `capacitorpolarised_svg`, with the
            hyphen dropped rather than turned into an underscore -- and guessing
            it wrong is silent: the part simply draws as nothing but its pins.
            Asking for the filename means the rule never has to be guessed, and
            "drop a file in with this name" is the whole contract. */
        SymbolArtwork load(const juce::String& name)
        {
            SymbolArtwork artwork;

            const auto wanted = name + ".svg";

            for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
            {
                const auto* resource = BinaryData::namedResourceList[i];

                if (wanted != BinaryData::getNamedResourceOriginalFilename(resource))
                    continue;

                int size = 0;

                if (const auto* data = BinaryData::getNamedResource(resource, size);
                    data != nullptr && size > 0)
                    if (const auto drawable = juce::Drawable::createFromImageData(data, (size_t) size))
                    {
                        // Seeded with the viewBox origin, so the parser's
                        // normalisation is cancelled before anything is cached.
                        const auto origin = viewBoxOrigin(data, size);
                        collect(*drawable, artwork,
                                juce::AffineTransform::translation(origin.x, origin.y));
                    }

                break;
            }

            // A part with no artwork draws as nothing at all, which looks like a
            // bug in the schematic rather than a missing file.
            jassert(! artwork.isEmpty());

            // Measured here, with the paths, so nothing can hold artwork whose
            // bounds were never taken.
            artwork.bounds = measure(artwork);

            return artwork;
        }

        /** The parsed artwork, one slot per asset.

            A `DeletedAtShutdown` rather than a plain function-local static, for
            the same reason Fonts.cpp's cache is one: the paths inside are
            leak-counted JUCE objects, and a static holding them outlives the
            detector that counts them. What that looks like is
            "*** Leaked objects detected: 165 instance(s) of class Path" and an
            assertion at the end of every single test run -- a permanent false
            positive, which is worse than no leak checking at all because it
            trains you to ignore the one that matters. */
        struct ArtworkCache final : public juce::DeletedAtShutdown
        {
            ~ArtworkCache() override { instance = nullptr; }

            std::array<std::unique_ptr<SymbolArtwork>, (size_t) assetCount> store;
            juce::CriticalSection lock;

            static ArtworkCache* instance;
        };

        ArtworkCache* ArtworkCache::instance = nullptr;

        /** Parsed once each, on first use, and found again by index. */
        const SymbolArtwork& cached(int asset)
        {
            if (ArtworkCache::instance == nullptr)
                ArtworkCache::instance = new ArtworkCache();

            auto& cache = *ArtworkCache::instance;
            const juce::ScopedLock guard(cache.lock);

            jassert(juce::isPositiveAndBelow(asset, assetCount));
            auto& slot = cache.store[(size_t) asset];

            if (slot == nullptr)
                slot = std::make_unique<SymbolArtwork>(load(assetNames[asset]));

            return *slot;
        }

        //======================================================================
        /** Which artwork each model of a part wants, worked out once.

            The decision is made from the model's *name* or *description*, so
            that reordering the lists in Element.h cannot silently swap a
            symbol -- but those reads build StringArrays, and doing that per part
            per frame is exactly the kind of thing that made painting allocate.
            Resolved into a small table the first time it is asked for instead. */
        template <typename Decide>
        const std::vector<int>& modelTable(ElementType type, Decide&& decide)
        {
            static std::map<ElementType, std::vector<int>> tables;
            static juce::CriticalSection lock;

            const juce::ScopedLock guard(lock);

            if (const auto it = tables.find(type); it != tables.end())
                return it->second;

            std::vector<int> table;

            for (int model = 0; model < getModelChoices(type).size(); ++model)
                table.push_back(decide(model));

            return tables.emplace(type, std::move(table)).first->second;
        }

        int variantFromTable(ElementType type, int modelIndex, int fallback,
                             const std::vector<int>& table)
        {
            juce::ignoreUnused(type);
            return juce::isPositiveAndBelow(modelIndex, (int) table.size())
                     ? table[(size_t) modelIndex] : fallback;
        }

        int diodeAsset(int modelIndex)
        {
            const auto& table = modelTable(ElementType::Diode, [](int model)
            {
                const auto name = getModelChoices(ElementType::Diode)[model];

                if (name.containsIgnoreCase("zener"))    return (int) Asset::DiodeZener;
                if (name.containsIgnoreCase("schottky")) return (int) Asset::DiodeSchottky;
                if (name.containsIgnoreCase("led"))      return (int) Asset::DiodeLed;

                return (int) Asset::Diode;
            });

            return variantFromTable(ElementType::Diode, modelIndex, (int) Asset::Diode, table);
        }

        int polarityAsset(ElementType type, int modelIndex, Asset forward, Asset reverse)
        {
            const auto& table = modelTable(type, [type, forward, reverse](int model)
            {
                return (int) (isReversePolarity(type, model) ? reverse : forward);
            });

            return variantFromTable(type, modelIndex, (int) forward, table);
        }

        //======================================================================
        /** The one place a part's state becomes a choice of artwork. No strings:
            this is on the paint path. */
        int assetFor(const Element& element)
        {
            switch (element.type)
            {
                case ElementType::Ground:        return (int) Asset::Ground;
                case ElementType::Input:         return (int) Asset::Input;
                case ElementType::Output:        return (int) Asset::Output;
                case ElementType::Scope:         return (int) Asset::Scope;
                case ElementType::Resistor:      return (int) Asset::Resistor;
                case ElementType::Inductor:      return (int) Asset::Inductor;
                case ElementType::Potentiometer: return (int) Asset::Potentiometer;
                case ElementType::VoltageSource: return (int) Asset::VoltageSource;
                case ElementType::Triode:        return (int) Asset::Triode;
                case ElementType::OpAmp:         return (int) Asset::OpAmp;
                case ElementType::VacuumDiode:   return (int) Asset::VacuumDiode;
                case ElementType::Pentode:       return (int) Asset::Pentode;

                case ElementType::Transformer:          return (int) Asset::Transformer;
                case ElementType::CenterTapTransformer: return (int) Asset::TransformerCt;

                // The variants. Each is a file of its own rather than a flag on
                // one, so replacing "the open switch" is replacing one drawing.
                case ElementType::Capacitor:
                    return (int) (element.polarised ? Asset::CapacitorPolarised : Asset::Capacitor);

                case ElementType::Switch:
                    return (int) (element.closed ? Asset::SwitchClosed : Asset::SwitchOpen);

                case ElementType::Spdt:
                    return (int) (element.closed ? Asset::SpdtA : Asset::SpdtB);

                case ElementType::Diode:
                    return diodeAsset(element.modelIndex);

                case ElementType::Transistor:
                    return polarityAsset(ElementType::Transistor, element.modelIndex,
                                         Asset::TransistorNpn, Asset::TransistorPnp);

                case ElementType::Jfet:
                    return polarityAsset(ElementType::Jfet, element.modelIndex,
                                         Asset::JfetN, Asset::JfetP);

                // A note is its own text, a group box a frame whose size is
                // dragged, and a node a tag as wide as the name written in it.
                // None of the three is a picture of a part, so none has a file.
                case ElementType::Text:
                case ElementType::Rectangle:
                case ElementType::Node:
                    return noAsset;
            }

            return noAsset;
        }
    } // namespace

    //==========================================================================

    const SymbolArtwork* getSymbolArtwork(const Element& element)
    {
        const int asset = assetFor(element);

        return asset == noAsset ? nullptr : &cached(asset);
    }

    juce::String getSymbolAssetName(const Element& element)
    {
        const int asset = assetFor(element);

        return asset == noAsset ? juce::String() : juce::String(assetNames[asset]);
    }

    juce::StringArray getAllSymbolAssetNames()
    {
        return juce::StringArray(assetNames, assetCount);
    }
} // namespace SchematicUI
