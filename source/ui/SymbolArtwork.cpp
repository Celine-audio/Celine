#include "SymbolArtwork.h"

#include <BinaryData.h>

#include <array>
#include <map>
#include <memory>
#include <vector>

namespace Celine
{
    using namespace SchematicModel;

    namespace
    {
        /** SVG units per grid square. */
        constexpr float unitsPerGrid = 10.0f;

        /** The artwork's normal stroke width. */
        constexpr float baselineStroke = 1.2f;

        //======================================================================
        /** One slot per artwork file, so the paint path looks artwork up by index without
            building strings. Order matches assetNames (checked by a static_assert and a test). */
        enum class Asset
        {
            Ground, Input, Output, Resistor,
            Capacitor, CapacitorPolarised, Inductor, Potentiometer,
            SwitchClosed, SwitchOpen, SpdtA, SpdtB, SpdtOff, Sp3tA, Sp3tB, Sp3tC,
            VoltageSource, Diode, DiodeZener, DiodeSchottky, DiodeLed,
            TransistorNpn, TransistorPnp, Triode, OpAmp,
            JfetN, JfetP, VacuumDiode, Pentode,
            Transformer, TransformerCt,
            Scope,
            MosfetN, MosfetP, PowerAmp,
            count,
        };

        constexpr int assetCount = static_cast<int>(Asset::count);

        const char* const assetNames[] = {
            "ground", "input", "output", "resistor",
            "capacitor", "capacitor-polarised", "inductor", "potentiometer",
            "switch-closed", "switch-open", "spdt-a", "spdt-b", "spdt-off", "sp3t-a", "sp3t-b", "sp3t-c",
            "voltage-source", "diode", "diode-zener", "diode-schottky", "diode-led",
            "transistor-npn", "transistor-pnp", "triode", "opamp",
            "jfet-n", "jfet-p", "vacuum-diode", "pentode",
            "transformer", "transformer-ct",
            "scope",
            "mosfet-n", "mosfet-p", "power-amp",
        };

        static_assert(std::size(assetNames) == (size_t) assetCount,
                      "every Asset needs a filename, in the same order");

        /** No artwork (parts drawn in code). */
        constexpr int noAsset = -1;

        //======================================================================
        /** The union of every shape's path, grown by half a stroke so the ink is inside.
            Computed by hand: Rectangle::getUnion ignores zero-height rectangles, which straight
            lines are. */
        juce::Rectangle<float> measure(const SymbolArtwork& artwork)
        {
            bool first = true;
            float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;

            for (const auto& shape : artwork.shapes)
            {
                if (shape.path.isEmpty())
                    continue;

                // Half the authored stroke width, in grid units.
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
            // getDrawableTransform: the public accessor in JUCE 9.
            const auto here = drawable.getDrawableTransform().followedBy(accumulated);

            if (const auto* shape = dynamic_cast<const juce::DrawablePath*>(&drawable))
            {
                SymbolArtwork::Shape s;
                s.path = shape->getPath();
                s.path.applyTransform(here.scaled(1.0f / unitsPerGrid, 1.0f / unitsPerGrid));

                const auto stroke = shape->getStrokeType().getStrokeThickness();

                // A filled and stroked path may arrive as one DrawablePath or two; split it so the
                // fill and the outline are always separate shapes.
                if (! shape->getFill().isInvisible() && stroke > 0.0f)
                {
                    auto fill = s;
                    fill.filled = true;
                    out.shapes.push_back(std::move(fill));
                }

                // No stroke: a filled shape.
                s.filled = stroke <= 0.0f;
                s.strokeScale = s.filled ? 1.0f : stroke / baselineStroke;

                out.shapes.push_back(std::move(s));
            }

            // JUCE 9 composites expose children through getChild().
            if (const auto* composite = dynamic_cast<const juce::DrawableComposite*>(&drawable))
                for (int i = 0; i < composite->getNumChildren(); ++i)
                    collect(composite->getChild(i), out, here);
        }

        /** A group's opacity, which DrawableComposite has no getter for: measured once by
            drawing the group in black and reading the deepest coverage. */
        float measureOpacity(const juce::DrawableComposite& group)
        {
            const auto bounds = group.getDrawableBounds();

            if (bounds.isEmpty())
                return 1.0f;

            // Large enough that a line has fully covered pixels.
            const float scale = juce::jmin(32.0f, 512.0f / juce::jmax(bounds.getWidth(), bounds.getHeight()));
            juce::Image probe(juce::Image::ARGB,
                              juce::jmax(1, juce::roundToInt(bounds.getWidth() * scale) + 2),
                              juce::jmax(1, juce::roundToInt(bounds.getHeight() * scale) + 2), true,
                              juce::SoftwareImageType());
            {
                juce::Graphics g(probe);
                group.draw(g, 1.0f, juce::AffineTransform::translation(-bounds.getX(), -bounds.getY())
                                        .scaled(scale).translated(1.0f, 1.0f));
            }

            int deepest = 0;
            const juce::Image::BitmapData pixels(probe, juce::Image::BitmapData::readOnly);

            for (int y = 0; y < probe.getHeight(); ++y)
                for (int x = 0; x < probe.getWidth(); ++x)
                    deepest = juce::jmax(deepest, (int) pixels.getPixelColour(x, y).getAlpha());

            return deepest > 0 ? (float) deepest / 255.0f : 1.0f;
        }

        /** How strongly a fill or stroke inks: the colour's alpha for a colour fill. */
        float strength(const juce::FillType& fill) noexcept
        {
            return fill.isInvisible() ? 0.0f : fill.getOpacity();
        }

        /** `source` in `transform`, flattened, with every outline turned to wind positively
            and every hole (inside an odd number of outlines) negatively, so overlapping ink
            can share one non-zero filled path. */
        juce::Path oriented(const juce::Path& source, const juce::AffineTransform& transform)
        {
            // A quarter of a device pixel at maximum zoom.
            constexpr float tolerance = 0.25f / 80.0f;

            std::vector<std::vector<juce::Point<float>>> contours;

            for (juce::PathFlatteningIterator it(source, transform, tolerance); it.next();)
            {
                if (it.subPathIndex == 0)
                    contours.push_back({{it.x1, it.y1}});

                contours.back().push_back({it.x2, it.y2});
            }

            std::vector<juce::Path> shapes;

            for (const auto& c : contours)
            {
                juce::Path p;
                p.startNewSubPath(c.front());
                for (size_t i = 1; i < c.size(); ++i)
                    p.lineTo(c[i]);
                p.closeSubPath();
                shapes.push_back(std::move(p));
            }

            juce::Path result;

            for (size_t i = 0; i < contours.size(); ++i)
            {
                auto points = contours[i];
                float area = 0.0f;

                for (size_t k = 0; k < points.size(); ++k)
                {
                    const auto& a = points[k];
                    const auto& b = points[(k + 1) % points.size()];
                    area += a.x * b.y - b.x * a.y;
                }

                int depth = 0;

                for (size_t j = 0; j < contours.size(); ++j)
                    if (j != i && shapes[j].contains(points.front(), 0.0f))
                        ++depth;

                // Positive area is clockwise on screen (y down).
                if ((area > 0.0f) != (depth % 2 == 0))
                    std::reverse(points.begin(), points.end());

                result.startNewSubPath(points.front());
                for (size_t k = 1; k < points.size(); ++k)
                    result.lineTo(points[k]);
                result.closeSubPath();
            }

            return result;
        }

        void addInk(SymbolArtwork& out, const juce::Path& path, float alpha)
        {
            // Merge into the previous run if it has the same opacity.
            if (out.ink.empty() || ! juce::approximatelyEqual(out.ink.back().alpha, alpha))
                out.ink.push_back({ {}, alpha });

            out.ink.back().path.addPath(path);
        }

        /** Every fill and stroke outline, in grid units, grouped by opacity. Strokes are
            outlined in their own space and then transformed, as SVG specifies. */
        void collectInk(const juce::Drawable& drawable, SymbolArtwork& out,
                        juce::AffineTransform accumulated, float alpha, bool isDocument)
        {
            const auto here = drawable.getDrawableTransform().followedBy(accumulated);

            if (const auto* shape = dynamic_cast<const juce::DrawablePath*>(&drawable))
            {
                // Fill first, then the stroke over it, each at its own opacity.
                if (const auto fill = strength(shape->getFill()); fill > 0.0f)
                {
                    addInk(out, oriented(shape->getPath(), here), alpha * fill);
                }

                if (const auto stroke = strength(shape->getStrokeFill());
                    stroke > 0.0f && shape->getStrokeType().getStrokeThickness() > 0.0f)
                {
                    // Twenty times JUCE's default precision, so curves stay round at maximum zoom.
                    juce::Path outline;
                    shape->getStrokeType().createStrokedPath(outline, shape->getPath(), {}, 20.0f);
                    addInk(out, oriented(outline, here), alpha * stroke);
                }
            }

            if (const auto* composite = dynamic_cast<const juce::DrawableComposite*>(&drawable))
            {
                // Groups carry an element's opacity; the document itself is full strength.
                const float groupAlpha = isDocument ? alpha : alpha * measureOpacity(*composite);

                for (int i = 0; i < composite->getNumChildren(); ++i)
                    collectInk(composite->getChild(i), out, here, groupAlpha, false);
            }
        }

        /** The file's viewBox in SVG units, or empty if it has none. */
        juce::Rectangle<float> viewBoxOf(const void* data, int size)
        {
            const auto text = juce::String::createStringFromData(data, size);

            if (const auto xml = juce::XmlDocument::parse(text))
            {
                auto numbers = juce::StringArray::fromTokens(
                    xml->getStringAttribute("viewBox").trim(), " ,", "");
                numbers.removeEmptyStrings();

                if (numbers.size() >= 4)
                    return {numbers[0].getFloatValue(), numbers[1].getFloatValue(),
                            numbers[2].getFloatValue(), numbers[3].getFloatValue()};

                if (numbers.size() >= 2)
                    return {numbers[0].getFloatValue(), numbers[1].getFloatValue(), 0.0f, 0.0f};
            }

            return {};
        }

        /** The viewBox origin, read from the file. JUCE 9's parser (LunaSVG) moves the viewBox
            to (0,0), so this is needed to put (0,0) back at the part's centre. */
        juce::Point<float> viewBoxOrigin(const void* data, int size)
        {
            return viewBoxOf(data, size).getPosition();
        }

        /** Loads an asset by original filename (not JUCE's derived identifier, which drops
            hyphens). Empty if missing. */
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
                        // Offset so the viewBox's centre is the part's centre, whatever the file's origin.
                        // A file with no viewBox uses its origin.
                        const auto box = viewBoxOf(data, size);
                        const auto shift = box.isEmpty() ? viewBoxOrigin(data, size)
                                                         : juce::Point<float>(-box.getWidth() * 0.5f,
                                                                              -box.getHeight() * 0.5f);
                        collect(*drawable, artwork, juce::AffineTransform::translation(shift.x, shift.y));
                        collectInk(*drawable, artwork,
                                   juce::AffineTransform::translation(shift.x, shift.y)
                                       .scaled(1.0f / unitsPerGrid, 1.0f / unitsPerGrid),
                                   1.0f, true);

                        // Centred like the paths.
                        if (! box.isEmpty())
                            artwork.viewBox = box.withPosition(-box.getWidth() * 0.5f, -box.getHeight() * 0.5f)
                                              / unitsPerGrid;
                    }

                break;
            }

            // A missing asset draws as nothing.
            jassert(! artwork.isEmpty());

            // Measured with the paths, so bounds always exist.
            artwork.bounds = measure(artwork);

            return artwork;
        }

        /** Parsed artwork, one slot per asset. DeletedAtShutdown rather than a static, so the
            cached Paths are freed before JUCE's leak detector runs. */
        struct ArtworkCache final : public juce::DeletedAtShutdown
        {
            ~ArtworkCache() override { instance = nullptr; }

            std::array<std::unique_ptr<SymbolArtwork>, (size_t) assetCount> store;
            juce::CriticalSection lock;

            static ArtworkCache* instance;
        };

        ArtworkCache* ArtworkCache::instance = nullptr;

        /** Parsed on first use, then found by index. */
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
        /** Which artwork each model of a part uses, worked out once from the model's group or
            description (not its index) and cached. */
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
            // From the model's group (Silicon, Zener, ...), not its part number.
            const auto& table = modelTable(ElementType::Diode, [](int model)
            {
                const auto group = getModelGroup(ElementType::Diode, model);

                if (group.equalsIgnoreCase("Zener"))    return (int) Asset::DiodeZener;
                if (group.equalsIgnoreCase("Schottky")) return (int) Asset::DiodeSchottky;
                if (group.equalsIgnoreCase("LED"))      return (int) Asset::DiodeLed;

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
        /** Maps a part's state to its artwork. No strings: this is on the paint path. */
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
                case ElementType::PowerAmp:      return (int) Asset::PowerAmp;
                case ElementType::VacuumDiode:   return (int) Asset::VacuumDiode;
                case ElementType::Pentode:       return (int) Asset::Pentode;

                case ElementType::Transformer:          return (int) Asset::Transformer;
                case ElementType::CenterTapTransformer: return (int) Asset::TransformerCt;

                // Variants: one file per state.
                case ElementType::Capacitor:
                    return (int) (element.polarised ? Asset::CapacitorPolarised : Asset::Capacitor);

                case ElementType::Switch:
                    return (int) (element.closed ? Asset::SwitchClosed : Asset::SwitchOpen);

                case ElementType::Diode:
                    return diodeAsset(element.modelIndex);

                case ElementType::Transistor:
                    return polarityAsset(ElementType::Transistor, element.modelIndex,
                                         Asset::TransistorNpn, Asset::TransistorPnp);

                case ElementType::Jfet:
                    return polarityAsset(ElementType::Jfet, element.modelIndex,
                                         Asset::JfetN, Asset::JfetP);

                case ElementType::Mosfet:
                    return polarityAsset(ElementType::Mosfet, element.modelIndex,
                                         Asset::MosfetN, Asset::MosfetP);

                // A changeover: one file per throw the lever can be on, plus the middle of an
                // ON-OFF-ON. ON-ON and ON-OFF-ON share the two throw drawings.
                case ElementType::Spdt:
                {
                    const int on = element.getThrowAt(element.switchPosition);

                    if (element.getSwitchThrows() == 3)
                        return (int) (on == 0 ? Asset::Sp3tA : on == 1 ? Asset::Sp3tB : Asset::Sp3tC);

                    return (int) (on == 0 ? Asset::SpdtA : on == 1 ? Asset::SpdtB : Asset::SpdtOff);
                }

                // Text, group boxes and nodes are drawn in code.
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
} // namespace Celine
