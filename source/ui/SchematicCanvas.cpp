#include "SchematicCanvas.h"

#include <CelineUI/Fonts.h>
#include <CelineUI/Theme.h>

namespace Celine
{
    using namespace SchematicModel;

    namespace
    {
        /** The part being placed, drawn faintly under the pointer. A function so it follows
            theme changes and isn't read during static initialisation. */
        juce::Colour ghostColour() { return Theme::text().withAlpha (0.4f); }

        constexpr float minGridSize = 4.0f;
        constexpr float maxGridSize = 40.0f;
    } // namespace

    //==========================================================================

    SchematicCanvas::SchematicCanvas(Schematic& s) : schematic(s)
    {
        painter.gridSize = 16.0f;
        painter.origin = {40.0f, 40.0f};

        // What a caption would cover where it is being tried: other captions and parts (bad)
        // and wires (untidy). Captions are placed in drawing order, so later ones move.
        painter.captionClutter = [this](const Element& element, juce::Rectangle<float> box)
        { return captionObstacles.clutterAt(element.id, box); };

        painter.onCaptionPlaced = [this](juce::Rectangle<float> box) { captionObstacles.addCaption(box); };

        setWantsKeyboardFocus(true);

        addAndMakeVisible(topRuler);
        addAndMakeVisible(leftRuler);
    }

    void SchematicCanvas::setTool(Tool newTool)
    {
        tool = newTool;
        setMouseCursor(tool == Tool::Select ? juce::MouseCursor::NormalCursor : juce::MouseCursor::CrosshairCursor);

        // The sheet takes the keyboard, so shortcuts work without a click on the canvas.
        if (isShowing())
            grabKeyboardFocus();

        repaint();

        if (onToolChanged)
            onToolChanged();
    }

    void SchematicCanvas::setPendingType(ElementType type)
    {
        // Picking from the palette asks for a fresh part: stop cloning.
        pendingClone.reset();
        pendingType = type;
        setTool(Tool::Place);
    }

    void SchematicCanvas::cloneElement(int elementId)
    {
        const auto* source = schematic.findElement(elementId);

        if (source == nullptr)
            return;

        pendingClone = *source;
        pendingType = source->type;

        // The copy faces the way the original does.
        pendingOrientation = source->orientation;
        pendingMirrored = source->mirrored;

        setTool(Tool::Place);
    }

    Element* SchematicCanvas::getSelectedElement() noexcept
    {
        return selectedIds.empty() ? nullptr : schematic.findElement(selectedIds.front());
    }

    bool SchematicCanvas::isSelected(int elementId) const noexcept
    {
        return std::find(selectedIds.begin(), selectedIds.end(), elementId) != selectedIds.end();
    }

    bool SchematicCanvas::isWireSelected(int wireId) const noexcept
    {
        return std::find(selectedWireIds.begin(), selectedWireIds.end(), wireId) != selectedWireIds.end();
    }

    void SchematicCanvas::selectElement(int elementId)
    {
        if (schematic.findElement(elementId) == nullptr)
            return;

        setSelection(elementId);

        // Scroll it into view only if it is off-screen.
        if (const auto* element = schematic.findElement(elementId))
        {
            const auto centre = painter.toPixel({element->x, element->y});
            const auto visible = getFramingArea().toFloat().reduced(24.0f);

            if (! visible.contains(centre))
                painter.origin += visible.getCentre() - centre;
        }

        repaint();
    }

    void SchematicCanvas::refresh()
    {
        // Drop ids of parts that no longer exist.
        std::vector<int> survivors;

        for (const int id : selectedIds)
            if (schematic.findElement(id) != nullptr)
                survivors.push_back(id);

        if (survivors.size() != selectedIds.size())
            setSelection(std::move(survivors));

        std::vector<int> liveWires;

        for (const int id : selectedWireIds)
            if (schematic.findWire(id) != nullptr)
                liveWires.push_back(id);

        if (liveWires.size() != selectedWireIds.size())
            setSelectedWires(std::move(liveWires));

        repaint();
    }

    void SchematicCanvas::notifyChanged()
    {
        repaint();

        if (onSchematicChanged)
            onSchematicChanged();
    }

    void SchematicCanvas::setSelection(int elementId)
    {
        if (elementId < 0)
            setSelection(std::vector<int>{});
        else
            setSelection(std::vector<int>{elementId});
    }

    void SchematicCanvas::setSelection(std::vector<int> ids)
    {
        if (ids == selectedIds)
            return;

        selectedIds = std::move(ids);

        // Selecting parts drops the wire selection. Callers wanting both set the wires
        // afterwards.
        if (! selectedIds.empty())
            selectedWireIds.clear();

        repaint();

        if (onSelectionChanged)
            onSelectionChanged();
    }

    void SchematicCanvas::setSelectedWires(std::vector<int> ids)
    {
        if (ids == selectedWireIds)
            return;

        selectedWireIds = std::move(ids);
        repaint();

        if (onSelectionChanged)
            onSelectionChanged();
    }

    void SchematicCanvas::setSelection(std::vector<int> elementIds, std::vector<int> wireIds)
    {
        if (elementIds == selectedIds && wireIds == selectedWireIds)
            return;

        // Assigned directly: the single-list setter would clear the wires.
        selectedIds = std::move(elementIds);
        selectedWireIds = std::move(wireIds);
        repaint();

        if (onSelectionChanged)
            onSelectionChanged();
    }

    juce::Point<float> SchematicCanvas::pixelAt(juce::Point<float> gridPoint) const noexcept
    {
        return painter.toPixel(gridPoint.x, gridPoint.y);
    }

    void SchematicCanvas::beginDrag(juce::Point<int> grid)
    {
        draggingElement = true;
        dragMovedSomething = false;
        dragLastGrid = grid;
    }

    void SchematicCanvas::toggleWireInSelection(int wireId)
    {
        if (wireId < 0)
            return;

        auto next = selectedWireIds;
        const auto it = std::find(next.begin(), next.end(), wireId);

        if (it != next.end())
            next.erase(it);
        else
            next.push_back(wireId);

        setSelectedWires(std::move(next));
    }

    int SchematicCanvas::wireAt(juce::Point<float> pixel) const noexcept
    {
        const auto grid = painter.toGridExact(pixel);

        // Hit distance in pixels: seven, or the line width if fatter.
        const float wireWidth = juce::jmax(1.0f, painter.gridSize * 0.13f);
        const float tolerance = juce::jmax(7.0f, wireWidth) / juce::jmax(1.0f, painter.gridSize);
        float best = tolerance;
        int found = -1;

        for (const auto& wire : schematic.getWires())
        {
            const juce::Line<float> line(wire.a.toFloat(), wire.b.toFloat());
            juce::Point<float> nearest;
            const auto distance = line.getDistanceFromPoint(grid, nearest);

            // <=, so the later wire (drawn on top) wins a tie.
            if (distance <= best)
            {
                best = distance;
                found = wire.id;
            }
        }

        return found;
    }

    void SchematicCanvas::toggleInSelection(int elementId)
    {
        if (elementId < 0)
            return;

        auto next = selectedIds;
        const auto it = std::find(next.begin(), next.end(), elementId);

        if (it != next.end())
            next.erase(it);
        else
            next.push_back(elementId);

        setSelection(std::move(next));
    }

    juce::Rectangle<float> SchematicCanvas::getSelectionBox() const noexcept
    {
        return juce::Rectangle<float>(boxAnchor, boxCurrent);
    }

    void SchematicCanvas::applyBoxSelection(bool additive)
    {
        const auto box = getSelectionBox();

        // In pixels, not grid squares, so rounding doesn't catch parts just outside the box.
        const bool crossing = isCrossingBox();

        std::vector<int> caught = additive ? selectedIds : std::vector<int>{};

        for (const auto& element : schematic.getElements())
        {
            const auto bounds = painter.elementBounds(element);
            const juce::Rectangle<float> pixels(painter.toPixel(bounds.getX(), bounds.getY()),
                                                painter.toPixel(bounds.getRight(), bounds.getBottom()));

            // Left to right encloses, right to left crosses.
            const bool hit = crossing ? box.intersects(pixels) : box.contains(pixels);

            if (hit && std::find(caught.begin(), caught.end(), element.id) == caught.end())
                caught.push_back(element.id);
        }

        std::vector<int> caughtWires = additive ? selectedWireIds : std::vector<int>{};

        for (const auto& wire : schematic.getWires())
        {
            const juce::Line<float> line(painter.toPixel(wire.a), painter.toPixel(wire.b));

            // Same rule as the parts. Crossing tests the line itself: a wire's bounding rectangle
            // has zero width or height, which Rectangle::intersects always rejects.
            const bool hit = crossing
                                 ? box.intersects(line)
                                 : box.contains(juce::Rectangle<float>(line.getStart(), line.getEnd()));

            if (hit && std::find(caughtWires.begin(), caughtWires.end(), wire.id) == caughtWires.end())
                caughtWires.push_back(wire.id);
        }

        setSelection(std::move(caught));
        setSelectedWires(std::move(caughtWires));
    }

    //==========================================================================

    void SchematicCanvas::zoomToFit()
    {
        if (schematic.isEmpty() || getWidth() < 20 || getHeight() < 20)
            return;

        juce::Rectangle<int> bounds;
        bool first = true;

        auto include = [&](juce::Rectangle<int> r)
        {
            bounds = first ? r : bounds.getUnion(r);
            first = false;
        };

        for (const auto& element : schematic.getElements())
            include(painter.elementBounds(element).getSmallestIntegerContainer());

        for (const auto& wire : schematic.getWires())
            include(juce::Rectangle<int>(wire.a, wire.b).expanded(1));

        if (first)
            return;

        // Margin for captions and terminal labels drawn outside the pin bounds.
        bounds = bounds.expanded(5);

        const auto area = getFramingArea().toFloat();
        const float scaleX = area.getWidth() / static_cast<float>(juce::jmax(1, bounds.getWidth()));
        const float scaleY = area.getHeight() / static_cast<float>(juce::jmax(1, bounds.getHeight()));

        painter.gridSize = juce::jlimit(minGridSize, maxGridSize, juce::jmin(scaleX, scaleY));
        painter.origin = {area.getCentreX() - (static_cast<float>(bounds.getCentreX()) * painter.gridSize),
                          area.getCentreY() - (static_cast<float>(bounds.getCentreY()) * painter.gridSize)};

        repaint();
    }

    juce::Rectangle<int> SchematicCanvas::getFramingArea() const
    {
        const auto area = framingInsets.subtractedFrom(getLocalBounds());

        // If the panels leave less than this, frame the whole canvas instead.
        constexpr int narrowest = 120;

        if (area.getWidth() < narrowest || area.getHeight() < narrowest)
            return getLocalBounds();

        return area;
    }

    void SchematicCanvas::resized()
    {
        // Full-length strips; the corner belongs to the left one, drawn last.
        topRuler.setBounds(0, 0, getWidth(), topRulerThickness);
        leftRuler.setBounds(0, 0, leftRulerThickness, getHeight());
        leftRuler.toFront(false);

    }

    //==========================================================================

    void SchematicCanvas::paintGrid(juce::Graphics& g) const
    {
        // Grid dots, skipped when zoomed out too far.
        if (painter.gridSize >= 8.0f)
        {
            g.setColour(Theme::grid());

            const auto topLeft = painter.toGrid({0.0f, 0.0f});
            const auto bottomRight = painter.toGrid({static_cast<float>(getWidth()), static_cast<float>(getHeight())});

            for (int y = topLeft.y; y <= bottomRight.y; ++y)
                for (int x = topLeft.x; x <= bottomRight.x; ++x)
                {
                    const auto p = painter.toPixel(juce::Point<int>(x, y));
                    g.fillRect(p.x, p.y, 1.0f, 1.0f);
                }
        }
    }

    void SchematicCanvas::paintDrawing(juce::Graphics& g) const
    {
        // Group boxes first, under everything.
        for (const auto& element : schematic.getElements())
            if (element.type == ElementType::Rectangle)
                painter.draw(g, element, isSelected(element.id) ? Theme::selected() : Theme::element());

        // Wires under parts.
        const float wireWidth = wireThickness();

        for (const auto& wire : schematic.getWires())
        {
            const bool selected = std::find(selectedWireIds.begin(), selectedWireIds.end(), wire.id)
                               != selectedWireIds.end();

            // Round caps, so corners join in a rounded elbow.
            juce::Path segment;
            segment.startNewSubPath(painter.toPixel(wire.a));
            segment.lineTo(painter.toPixel(wire.b));

            g.setColour(selected ? Theme::selected() : Theme::wire());
            g.strokePath(segment, juce::PathStrokeType(selected ? wireWidth * 1.8f : wireWidth,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        // Junction dots where the connection isn't obvious. Always in the wire colour: a dot
        // belongs to the connection, not to one selected wire.
        g.setColour(Theme::wire());

        const float junctionRadius = juce::jmax(2.0f, painter.gridSize * 0.22f);

        for (const auto& junction : schematic.findJunctions())
        {
            const auto p = painter.toPixel(junction);
            g.fillEllipse(juce::Rectangle<float>(junctionRadius * 2.0f, junctionRadius * 2.0f).withCentre(p));
        }

        // Everything a caption could land on, sorted by area once for this paint.
        {
            juce::Rectangle<float> extent;
            bool first = true;
            const auto grow = [&](juce::Rectangle<float> r)
            {
                // Never zero-sized: getUnion() drops an empty rectangle.
                r = r.withSize(juce::jmax(r.getWidth(), 0.001f), juce::jmax(r.getHeight(), 0.001f));
                extent = first ? r : extent.getUnion(r);
                first = false;
            };

            for (const auto& element : schematic.getElements())
                if (element.type != ElementType::Rectangle)
                    grow(painter.elementBounds(element));

            for (const auto& wire : schematic.getWires())
                grow(juce::Rectangle<float>(wire.a.toFloat(), wire.b.toFloat()));

            captionObstacles.reset(extent);

            for (const auto& element : schematic.getElements())
                if (element.type != ElementType::Rectangle)
                    captionObstacles.addPart(element.id, painter.elementBounds(element));

            for (const auto& wire : schematic.getWires())
                captionObstacles.addWire(juce::Line<float>(wire.a.toFloat(), wire.b.toFloat()));
        }

        for (const auto& element : schematic.getElements())
            if (element.type != ElementType::Rectangle)
                painter.draw(g, element, isSelected(element.id) ? Theme::selected() : Theme::element());

        drawScopeTraces(g);
    }

    void SchematicCanvas::paintSelection(juce::Graphics& g) const
    {
        // Selection halo, one per selected part.
        for (const int id : selectedIds)
        {
            const auto* selected = schematic.findElement(id);

            if (selected == nullptr)
                continue;

            const auto bounds = painter.elementBounds(*selected).getSmallestIntegerContainer();
            const bool isBox = selected->type == ElementType::Rectangle;

            // Outside a group box rather than on its outline.
            const auto grid = painter.gridSize;
            const juce::Rectangle<float> pixels =
                juce::Rectangle<float>(painter.toPixel(bounds.getTopLeft()),
                                       painter.toPixel(bounds.getBottomRight()))
                    .expanded(isBox ? grid * 0.5f : 0.0f);

            g.setColour(Theme::selected().withAlpha(0.35f));
            g.drawRoundedRectangle(pixels, grid * 0.4f, 1.5f);

            // Only on a box selected alone: with several selected, a drag moves them all.
            if (isBox && selectedIds.size() == 1)
            {
                // A bracket following the box's rounded corner (same radius as SchematicSymbols).
                const auto frame = juce::Rectangle<float>(painter.toPixel(bounds.getTopLeft()),
                                                          painter.toPixel(bounds.getBottomRight()));
                const float radius = juce::jmin(grid * 0.7f, frame.getWidth() * 0.2f, frame.getHeight() * 0.2f);
                const float arm = juce::jmin(radius + grid * 0.9f, frame.getWidth() * 0.5f, frame.getHeight() * 0.5f);
                const auto corner = frame.getBottomRight();

                juce::Path bracket;
                bracket.startNewSubPath(corner.x, corner.y - arm);
                bracket.lineTo(corner.x, corner.y - radius);
                bracket.quadraticTo(corner.x, corner.y, corner.x - radius, corner.y);
                bracket.lineTo(corner.x - arm, corner.y);

                g.setColour(Theme::selected());
                g.strokePath(bracket, juce::PathStrokeType(juce::jmax(2.5f, grid * 0.3f),
                                                           juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));
            }
        }

        // The ends of a wire selected on its own, as diamonds.
        if (const int handleWire = wireWithHandles(); handleWire >= 0)
        {
            if (const auto* wire = schematic.findWire(handleWire))
            {
                const float reach = wireHandleReach();

                g.setColour(Theme::selected());

                for (const auto end : {wire->a, wire->b})
                {
                    const auto centre = painter.toPixel(end);

                    juce::Path diamond;
                    diamond.startNewSubPath(centre.x, centre.y - reach);
                    diamond.lineTo(centre.x + reach, centre.y);
                    diamond.lineTo(centre.x, centre.y + reach);
                    diamond.lineTo(centre.x - reach, centre.y);
                    diamond.closeSubPath();

                    g.fillPath(diamond);
                }
            }
        }
    }

    void SchematicCanvas::paintInFlight(juce::Graphics& g) const
    {
        // The wire being dragged out.
        if (draggingWire)
        {
            g.setColour(Theme::wire().withAlpha(0.7f));

            const juce::Point<int> corner{wireEnd.x, wireStart.y};
            // One path, so the elbow joins like a drawn wire's.
            juce::Path path;
            path.startNewSubPath(painter.toPixel(wireStart));
            path.lineTo(painter.toPixel(corner));
            path.lineTo(painter.toPixel(wireEnd));
            g.strokePath(path, juce::PathStrokeType(wireThickness(), juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
        }

        // Drawn last; solid when enclosing, dashed when crossing.
        if (boxSelecting)
        {
            const auto box = getSelectionBox();
            const bool crossing = isCrossingBox();
            const auto colour = crossing ? Theme::boxCrossing() : Theme::boxEnclose();

            g.setColour(colour.withAlpha(0.12f));
            g.fillRect(box);

            g.setColour(colour.withAlpha(0.9f));

            if (crossing)
            {
                const float dashes[] = {5.0f, 4.0f};

                for (const auto& edge : {juce::Line<float>(box.getTopLeft(), box.getTopRight()),
                                         juce::Line<float>(box.getTopRight(), box.getBottomRight()),
                                         juce::Line<float>(box.getBottomRight(), box.getBottomLeft()),
                                         juce::Line<float>(box.getBottomLeft(), box.getTopLeft())})
                    g.drawDashedLine(edge, dashes, 2, 1.0f);
            }
            else
            {
                g.drawRect(box, 1.0f);
            }
        }

        // A ghost of what the Place tool would drop here.
        if (tool == Tool::Place && isMouseOverOrDragging())
        {
            Element ghost;
            if (pendingClone.has_value())
                pendingClone->copyPropertiesTo(ghost);

            ghost.type = pendingType;
            ghost.x = hoverGrid.x;
            ghost.y = hoverGrid.y;
            ghost.orientation = pendingOrientation;
            ghost.mirrored = pendingMirrored;
            painter.drawSymbolOnly(g, ghost, ghostColour());
        }
    }

    void SchematicCanvas::paint(juce::Graphics& g)
    {
        g.fillAll(Theme::background());

        paintGrid(g);
        paintDrawing(g);
        paintSelection(g);
        paintInFlight(g);

        // The rulers are child components, so their cursor markers repaint without the sheet.
    }

    void SchematicCanvas::mouseDoubleClick(const juce::MouseEvent& event)
    {
        // Only the Select tool: with the wire tool both clicks already mean something.
        if (tool != Tool::Select)
            return;

        const int hit = elementAt(event.position);

        if (hit < 0)
            return;

        if (auto* element = schematic.findElement(hit))
            beginInlineEdit(*element);
    }

    void SchematicCanvas::beginInlineEdit(Element& element)
    {
        // Only parts with a typed value, and Text notes; list-chosen parts use the inspector.
        const bool isText = element.type == ElementType::Text;

        if (! isText && ! element.hasNumericValue())
            return;

        closeInlineEdit();

        inlineEditId = element.id;
        inlineEditor = std::make_unique<juce::TextEditor>();

        const auto& info = getElementInfo(element.type);

        inlineEditor->setText(isText ? element.label
                                     : SymbolPainter::formatValue(element.value, info.unit),
                              juce::dontSendNotification);
        inlineEditor->selectAll();
        inlineEditor->setJustification(juce::Justification::centred);
        inlineEditor->setFont(Fonts::light(juce::jlimit(12.0f, 20.0f, painter.gridSize * 0.9f)));
        // No outline; the bright fill separates it from the drawing.
        inlineEditor->setColour(juce::TextEditor::backgroundColourId, Theme::surfaceBright());

        inlineEditor->onReturnKey = [this] { commitInlineEdit(); };
        inlineEditor->onEscapeKey = [this] { closeInlineEdit(); };

        // Focus loss commits, as in the inspector.
        inlineEditor->onFocusLost = [this] { commitInlineEdit(); };

        const auto centre = painter.toPixel({element.x, element.y});
        const float width = juce::jmax(72.0f, painter.gridSize * 6.0f);
        const float height = juce::jmax(22.0f, painter.gridSize * 1.5f);

        inlineEditor->setBounds(juce::Rectangle<float>(width, height)
                                    .withCentre({centre.x, centre.y + painter.gridSize * 3.0f})
                                    .toNearestInt());

        addAndMakeVisible(*inlineEditor);

        // Only a showing component can take focus.
        if (isShowing())
            inlineEditor->grabKeyboardFocus();
    }

    void SchematicCanvas::commitInlineEdit()
    {
        if (inlineEditor == nullptr)
            return;

        const auto typed = inlineEditor->getText();
        const int id = inlineEditId;

        // Close first: committing rebuilds the inspector, and a still-focused editor would
        // commit again from inside its own commit.
        closeInlineEdit();

        auto* element = schematic.findElement(id);

        if (element == nullptr)
            return;

        if (element->type == ElementType::Text)
        {
            if (element->label == typed)
                return;

            element->label = typed;
            notifyChanged();
            return;
        }

        double parsed = 0.0;

        // Refuse invalid input rather than writing zero, which would break the build.
        if (! SymbolPainter::parseValue(typed, parsed) || parsed <= 0.0)
            return;

        if (juce::approximatelyEqual(element->value, parsed))
            return;

        element->value = parsed;
        notifyChanged();
    }

    void SchematicCanvas::closeInlineEdit()
    {
        if (inlineEditor == nullptr)
            return;

        // Clear the callbacks first, or removal fires onFocusLost into a half-destroyed
        // object.
        inlineEditor->onFocusLost = nullptr;
        inlineEditor->onReturnKey = nullptr;
        inlineEditor->onEscapeKey = nullptr;

        removeChildComponent(inlineEditor.get());
        inlineEditor.reset();
        inlineEditId = -1;

        if (isShowing())
            grabKeyboardFocus();
    }

    void SchematicCanvas::drawScopeTraces(juce::Graphics& g) const
    {
        // Below about six pixels a square the trace is unreadable.
        if (! scopeReader || painter.gridSize < 6.0f)
            return;

        const auto screen = SymbolPainter::getScopeScreen();

        for (const auto& element : schematic.getElements())
        {
            if (element.type != ElementType::Scope)
                continue;

            ScopeReading reading;

            if (! scopeReader(element.id, reading))
                continue;

            // Upright however the part is turned.
            const auto centre = painter.toPixel({element.x, element.y});
            const juce::Rectangle<float> area(centre.x + screen.getX() * painter.gridSize,
                                              centre.y + screen.getY() * painter.gridSize,
                                              screen.getWidth() * painter.gridSize,
                                              screen.getHeight() * painter.gridSize);

            drawScopeTrace(g, area.reduced(painter.gridSize * 0.18f), reading,
                           Theme::captionValue(), Theme::comment(), Theme::background());

            // Readout under the screen, only when there is room.
            if (painter.gridSize < 11.0f || ! reading.live)
                continue;

            g.setFont(Fonts::light(painter.gridSize * 0.82f));
            g.setColour(Theme::captionValue());
            g.drawText(SymbolPainter::formatValue(reading.dcAverage, "V") + "  pp "
                           + SymbolPainter::formatValue(reading.peakToPeak, "V"),
                       juce::Rectangle<float>(area.getWidth() * 1.6f, painter.gridSize * 1.2f)
                           .withCentre({area.getCentreX(), area.getBottom() + painter.gridSize * 3.0f}),
                       juce::Justification::centred, false);
        }
    }

    SchematicCanvas::Ruler::Ruler(const SchematicCanvas& o, bool horizontal)
        : owner(o), isHorizontal(horizontal)
    {
        // Opaque so repainting never repaints the canvas; ignores the mouse.
        setOpaque(true);
        setInterceptsMouseClicks(false, false);
        setWantsKeyboardFocus(false);
    }

    void SchematicCanvas::Ruler::paint(juce::Graphics& g)
    {
        const auto thickness =
            static_cast<float>(isHorizontal ? topRulerThickness : leftRulerThickness);

        // Numbering starts after the other ruler, which owns the shared corner.
        const auto crossThickness =
            static_cast<float>(isHorizontal ? leftRulerThickness : topRulerThickness);

        const auto& view = owner.painter;

        // Rulers are frame, in the toolbar colour.
        g.fillAll(Theme::headerBackground());

        // Two-pixel edge between frame and sheet.
        g.setColour(Theme::line());

        // The edges run outwards from the sheet's top-left corner, not through the
        // overlapping block.
        if (isHorizontal)
            g.fillRect(0.0f, thickness - 2.0f, static_cast<float>(getWidth()), 2.0f);
        else
            g.fillRect(thickness - 2.0f, crossThickness - 2.0f, 2.0f,
                       static_cast<float>(getHeight()) - crossThickness + 2.0f);

        // One label every `step` squares, chosen so labels never collide.
        int step = 1;

        for (const int candidate : {1, 2, 5, 10, 20, 50, 100})
        {
            step = candidate;

            if (view.gridSize * static_cast<float>(step) >= 44.0f)
                break;
        }

        // The visible area starts after the other ruler; canvas and strip share coordinates.
        const auto first = view.toGrid({0.0f, 0.0f});
        const auto last = view.toGrid({static_cast<float>(owner.getWidth()),
                                          static_cast<float>(owner.getHeight())});

        g.setFont(Fonts::light(11.0f));

        const auto tickColour = Theme::line();
        const auto labelColour = Theme::graphText();

        const int from = isHorizontal ? first.x : first.y;
        const int to = isHorizontal ? last.x : last.y;

        for (int n = (from / step) * step - step; n <= to + step; n += step)
        {
            const auto at = isHorizontal ? view.toPixel(juce::Point<int>(n, 0)).x
                                         : view.toPixel(juce::Point<int>(0, n)).y;

            const auto limit = isHorizontal ? static_cast<float>(getWidth())
                                            : static_cast<float>(getHeight());

            if (at < crossThickness || at > limit)
                continue;

            g.setColour(tickColour);

            if (isHorizontal)
                g.drawLine(at, thickness - 8.0f, at, thickness - 2.0f);
            else
                g.drawLine(thickness - 8.0f, at, thickness - 2.0f, at);

            g.setColour(labelColour);

            if (isHorizontal)
                g.drawText(juce::String(n), juce::Rectangle<float>(at - 20.0f, 3.0f, 40.0f, 13.0f),
                           juce::Justification::centred, false);
            else
                g.drawText(juce::String(n),
                           juce::Rectangle<float>(1.0f, at - 7.0f, thickness - 10.0f, 14.0f),
                           juce::Justification::centredRight, false);
        }

        // Cursor marker at the pointer's actual pixel, not snapped.
        if (cursor >= crossThickness)
        {
            g.setColour(Theme::cursorMark());

            if (isHorizontal)
                g.fillRect(cursor - 1.0f, 0.0f, 2.0f, thickness);
            else
                g.fillRect(0.0f, cursor - 1.0f, thickness, 2.0f);
        }
    }

    void SchematicCanvas::updateRulerCursor(juce::Point<float> position)
    {
        const bool inside = getLocalBounds().toFloat().contains(position);

        // Only the two strips repaint.
        auto move = [](Ruler& ruler, float to)
        {
            if (std::abs(ruler.cursor - to) < 0.5f)
                return;

            ruler.cursor = to;
            ruler.repaint();
        };

        move(topRuler, inside ? position.x : -1.0f);
        move(leftRuler, inside ? position.y : -1.0f);
    }

    //==========================================================================

    int SchematicCanvas::elementAt(juce::Point<float> pixel) const noexcept
    {
        const auto grid = painter.toGridExact(pixel);
        const auto& elements = schematic.getElements();

        // Backwards, so the part on top wins.
        for (auto e = elements.rbegin(); e != elements.rend(); ++e)
        {
            if (e->type == ElementType::Rectangle)
                continue;   // boxes lose to everything

            if (painter.elementBounds(*e).contains(grid))
                return e->id;
        }

        // Group boxes last, by their edge only.
        for (auto e = elements.rbegin(); e != elements.rend(); ++e)
        {
            if (e->type != ElementType::Rectangle)
                continue;

            const auto bounds = e->getRectangleBounds().toFloat();

            if (bounds.expanded(0.6f).contains(grid) && ! bounds.reduced(0.6f).contains(grid))
                return e->id;
        }

        return -1;
    }

    void SchematicCanvas::mouseDown(const juce::MouseEvent& event)
    {
        // Ignores the tests' offscreen synthetic clicks.
        if (isShowing())
            grabKeyboardFocus();

        const auto grid = gridAt(event.position);

        // Middle-click arms the Place tool with a copy of the part.
        if (event.mods.isMiddleButtonDown())
        {
            if (const int hit = elementAt(event.position); hit >= 0)
                cloneElement(hit);

            return;
        }

        // Right-drag pans.
        if (event.mods.isPopupMenu())
        {
            panning = true;
            panOrigin = painter.origin - event.position;
            return;
        }

        switch (tool)
        {
            case Tool::Place:  placePendingElement(grid); return;
            case Tool::Delete: deleteAt(event.position); return;

            case Tool::Wire:
                draggingWire = true;
                wireStart = wireEnd = grid;
                return;

            case Tool::Select: beginSelectGesture(event, grid); return;
        }
    }

    void SchematicCanvas::placePendingElement(juce::Point<int> grid)
    {
        const int id = schematic.addElement(pendingType, grid.x, grid.y);
        auto* placed = schematic.findElement(id);

        // A clone brings its value, model, label and settings; a fresh part has the defaults.
        if (pendingClone.has_value())
            pendingClone->copyPropertiesTo(*placed);

        placed->orientation = pendingOrientation;
        placed->mirrored = pendingMirrored;
        setSelection(id);

        // One part per click, then back to selecting.
        setTool(Tool::Select);
        notifyChanged();
    }

    bool SchematicCanvas::grabResizeHandle(const juce::MouseEvent& event, juce::Point<int> grid)
    {
        // Handles win over anything under them, while their owner is the whole selection.
        if (selectedIds.size() == 1)
        {
            if (auto* selected = getSelectedElement();
                selected != nullptr && selected->type == ElementType::Rectangle
                && isNearResizeHandle(*selected, grid))
            {
                resizingElement = true;
                return true;
            }
        }

        if (const int wireId = wireWithHandles(); wireId >= 0)
        {
            if (const auto* wire = schematic.findWire(wireId))
            {
                if (const int end = wireHandleAt(*wire, event.position); end >= 0)
                {
                    resizingWireEnd = end;

                    // Reset here: this gesture skips beginDrag.
                    dragMovedSomething = false;
                    return true;
                }
            }
        }

        return false;
    }

    void SchematicCanvas::beginSelectGesture(const juce::MouseEvent& event, juce::Point<int> grid)
    {
        if (grabResizeHandle(event, grid))
            return;

        const int hit = elementAt(event.position);
        const bool additive = event.mods.isShiftDown();

        if (hit >= 0)
        {
            // Shift-click toggles and starts no drag.
            if (additive)
            {
                toggleInSelection(hit);
                return;
            }

            // Clicking a selected part keeps the selection for a group drag; it narrows on mouse
            // up if nothing moved.
            if (! isSelected(hit))
                setSelection(hit);

            beginDrag(grid);
            return;
        }

        // Wires are tried before the selection box.
        if (const int wire = wireAt(event.position); wire >= 0)
        {
            if (additive)
                toggleWireInSelection(wire);
            else if (! isWireSelected(wire))
                setSelection(std::vector<int>{}, std::vector<int>{wire});   // same rule as the parts

            beginDrag(grid);
            return;
        }

        // Empty sheet: start a selection box. The selection changes on mouse up (see
        // applyBoxSelection).
        boxSelecting = true;
        boxAnchor = boxCurrent = event.position;

        // A plain click: mouse-up skips applyBoxSelection, so clear here.
        if (! additive)
        {
            setSelection(-1);
            setSelectedWires({});
        }
    }

    void SchematicCanvas::mouseDrag(const juce::MouseEvent& event)
    {
        // mouseMove doesn't fire while dragging, so update the markers here too.
        updateRulerCursor(event.position);

        const auto grid = gridAt(event.position);

        if (panning)
        {
            painter.origin = panOrigin + event.position;
            repaint();
            return;
        }

        if (draggingWire)
        {
            wireEnd = grid;
            repaint();
            return;
        }

        if (resizingElement)
        {
            if (auto* element = getSelectedElement())
                resizeTo(*element, grid);

            return;
        }

        if (resizingWireEnd >= 0)
        {
            if (selectedWireIds.size() == 1
                && schematic.resizeWireEnd(selectedWireIds.front(), resizingWireEnd, grid))
            {
                // Merged on mouseUp, not mid-drag.
                dragMovedSomething = true;
                notifyChanged();
                repaint();
            }

            return;
        }

        if (boxSelecting)
        {
            boxCurrent = event.position;
            repaint();
            return;
        }

        if (draggingElement)
        {
            // A delta, so every selected part moves by the same amount.
            const auto delta = grid - dragLastGrid;

            if (delta.isOrigin())
                return;

            dragLastGrid = grid;
            dragMovedSomething = true;

            for (const int id : selectedIds)
            {
                if (auto* element = schematic.findElement(id))
                {
                    element->x += delta.x;
                    element->y += delta.y;
                }
            }

            for (const int id : selectedWireIds)
                schematic.moveWire(id, delta);

            // Moving a part can change its connections.
            notifyChanged();
        }
    }

    void SchematicCanvas::mouseUp(const juce::MouseEvent& event)
    {
        if (draggingWire)
        {
            draggingWire = false;

            if (wireStart != wireEnd)
            {
                schematic.addWire(wireStart, wireEnd);

                // A wire drawn on from another's end joins it.
                mergeWiresAfterEdit(-1);
                notifyChanged();
            }
        }

        // After a resize or a whole-wire drag, merge collinear neighbours.
        if (dragMovedSomething
            && (resizingWireEnd >= 0 || (draggingElement && ! selectedWireIds.empty())))
        {
            const int preferred = selectedWireIds.size() == 1 ? selectedWireIds.front() : -1;

            mergeWiresAfterEdit(preferred);
            notifyChanged();
        }

        resizingWireEnd = -1;

        if (boxSelecting)
        {
            boxSelecting = false;

            // A click, not a drag: the selection was already cleared on mouse down.
            if (getSelectionBox().getWidth() >= 2.0f || getSelectionBox().getHeight() >= 2.0f)
                applyBoxSelection(event.mods.isShiftDown());

            repaint();
        }

        // Clicking a group member without dragging narrows to it. Parts and wires count
        // together.
        if (draggingElement && ! dragMovedSomething && ! event.mods.isShiftDown()
            && selectedIds.size() + selectedWireIds.size() > 1)
        {
            if (const int hit = elementAt(event.position); hit >= 0)
                setSelection(hit);   // clears the wires itself
            else if (const int wire = wireAt(event.position); wire >= 0)
                setSelection(std::vector<int>{}, std::vector<int>{wire});
        }

        panning = false;
        draggingElement = false;
        resizingElement = false;

        // Always fired, so undo has a gesture boundary even when nothing changed.
        if (onGestureEnd)
            onGestureEnd();
    }

    bool SchematicCanvas::isMidGesture() const noexcept
    {
        return draggingElement || resizingElement || draggingWire || resizingWireEnd >= 0;
    }

    int SchematicCanvas::wireWithHandles() const noexcept
    {
        if (selectedWireIds.size() != 1 || ! selectedIds.empty())
            return -1;

        return schematic.findWire(selectedWireIds.front()) != nullptr ? selectedWireIds.front() : -1;
    }

    float SchematicCanvas::wireHandleReach() const noexcept
    {
        return juce::jmax(3.0f, painter.gridSize * 0.34f);
    }

    int SchematicCanvas::wireHandleAt(const Wire& wire, juce::Point<float> pixel) const noexcept
    {
        // Manhattan distance in pixels, matching the drawn diamond.
        const float reach = wireHandleReach();

        const auto near = [this, pixel, reach](juce::Point<int> end)
        {
            const auto centre = painter.toPixel(end);
            return std::abs(pixel.x - centre.x) + std::abs(pixel.y - centre.y) <= reach;
        };

        // `a` first, so overlapping grab bands on short wires resolve consistently.
        if (near(wire.a))
            return 0;

        return near(wire.b) ? 1 : -1;
    }

    void SchematicCanvas::mergeWiresAfterEdit(int preferredId)
    {
        if (schematic.mergeCollinearWires(preferredId) == 0)
            return;

        // Drop wires absorbed by a merge.
        std::vector<int> surviving;

        for (const int id : selectedWireIds)
            if (schematic.findWire(id) != nullptr)
                surviving.push_back(id);

        if (surviving.size() != selectedWireIds.size())
            setSelectedWires(std::move(surviving));
    }

    bool SchematicCanvas::isNearResizeHandle(const Element& element, juce::Point<int> grid) const noexcept
    {
        const auto corner = element.getRectangleBounds().getBottomRight();

        // One square either side, like the edge's grab band.
        return std::abs(grid.x - corner.x) <= 1 && std::abs(grid.y - corner.y) <= 1;
    }

    void SchematicCanvas::resizeTo(Element& element, juce::Point<int> grid)
    {
        const auto before = element.getRectangleBounds();
        const int width = juce::jmax(minRectangleSize, grid.x - before.getX());
        const int height = juce::jmax(minRectangleSize, grid.y - before.getY());

        if (width == element.width && height == element.height)
            return;

        element.width = width;
        element.height = height;

        // The position is the centre: move it by half the size change to pin the opposite
        // corner.
        element.x = before.getX() + width / 2;
        element.y = before.getY() + height / 2;

        // Also keeps the inspector's Width and Height updated during the drag.
        notifyChanged();
    }

    void SchematicCanvas::mouseExit(const juce::MouseEvent&)
    {
        // Hide the markers.
        updateRulerCursor({-1.0f, -1.0f});
    }

    void SchematicCanvas::mouseMove(const juce::MouseEvent& event)
    {
        const auto grid = gridAt(event.position);

        // The markers are cheap; the ghost repaints only when its snapped position changes.
        updateRulerCursor(event.position);

        if (grid != hoverGrid)
        {
            hoverGrid = grid;

            if (tool == Tool::Place)
                repaint();
        }
    }

    void SchematicCanvas::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
    {
        // While panning, ignore the wheel.
        if (panning || event.mods.isPopupMenu())
            return;

        // Ignore sideways scrolls and momentum tails with no vertical travel.
        if (wheel.deltaY == 0.0f)
            return;

        // Zoom about the unrounded cursor position, so the sheet doesn't drift.
        const auto gridUnderCursor = painter.toGridExact(event.position);
        const float factor = wheel.deltaY > 0 ? 1.12f : (1.0f / 1.12f);

        painter.gridSize = juce::jlimit(minGridSize, maxGridSize, painter.gridSize * factor);
        painter.origin = event.position
                       - juce::Point<float>(gridUnderCursor.x * painter.gridSize,
                                            gridUnderCursor.y * painter.gridSize);

        repaint();
    }

    namespace
    {
        /** Whether a key press means `character`, on any keyboard layout. Matches the
            lower-cased typed character, falling back to the key code. With a command modifier
            the key code is used, since X11 and Windows deliver a control code as the
            character. */
        bool keyIs(const juce::KeyPress& key, juce::juce_wchar character)
        {
            const auto byCode = [&]
            {
                const int code = key.getKeyCode();
                return code == static_cast<int>(juce::CharacterFunctions::toUpperCase(character))
                    || code == static_cast<int>(character);
            };

            if (key.getModifiers().isCommandDown())
                return byCode();

            const auto typed = key.getTextCharacter();

            return typed != 0 ? juce::CharacterFunctions::toLowerCase(typed) == character
                              : byCode();
        }
    } // namespace

    bool SchematicCanvas::commandKeyPressed(const juce::KeyPress& key)
    {
        // isCommandDown() is Command on macOS, Control elsewhere.
        if (keyIs(key, 'z'))
        {
            // Shift-Cmd-Z redoes; Ctrl-Y is handled below.
            if (auto& callback = key.getModifiers().isShiftDown() ? onRedoRequested : onUndoRequested)
                callback();

            return true;
        }

        if (keyIs(key, 'y'))
        {
            if (onRedoRequested)
                onRedoRequested();

            return true;
        }

        if (keyIs(key, 'd')) { duplicateSelection(); return true; }
        if (keyIs(key, 'r')) { rotate(); return true; }
        if (keyIs(key, 'f')) { flip(); return true; }

        return false;
    }

    bool SchematicCanvas::keyPressed(const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            deleteSelection();
            return true;
        }

        // Command + letter acts on the sheet; bare letters name parts.
        if (key.getModifiers().isCommandDown())
            return commandKeyPressed(key);

        if (keyIs(key, 's')) { setTool(Tool::Select); return true; }
        if (keyIs(key, 'w')) { setTool(Tool::Wire); return true; }
        if (keyIs(key, 'x')) { setTool(Tool::Delete); return true; }

        // F frames the drawing; Cmd-F flips.
        if (keyIs(key, 'f')) { zoomToFit(); return true; }

        // Bare letters arm a part, as the palette does. B is the box, D the diode, I and O the
        // terminals.
        struct PartKey { juce::juce_wchar key; ElementType type; };

        for (const auto& part : {PartKey{'r', ElementType::Resistor},
                                 PartKey{'c', ElementType::Capacitor},
                                 PartKey{'l', ElementType::Inductor},
                                 PartKey{'t', ElementType::Transistor},
                                 PartKey{'i', ElementType::Input},
                                 PartKey{'o', ElementType::Output},
                                 PartKey{'g', ElementType::Ground},
                                 PartKey{'d', ElementType::Diode},
                                 PartKey{'b', ElementType::Rectangle}})
        {
            if (keyIs(key, part.key))
            {
                setPendingType(part.type);
                return true;
            }
        }

        if (key == juce::KeyPress::escapeKey)
        {
            setTool(Tool::Select);

            // Clear both lists: setSelection(-1) alone keeps the wires.
            setSelection(-1);
            setSelectedWires({});
            return true;
        }

        return false;
    }

    void SchematicCanvas::rotate()
    {
        // While placing, act on the part being placed.
        if (tool == Tool::Place)
        {
            pendingOrientation = (pendingOrientation + 1) & 3;
            repaint();
            return;
        }

        if (selectedIds.empty())
            return;

        // Each part turns about its own centre, so it stays on its wires' grid positions.
        for (const int id : selectedIds)
            if (auto* element = schematic.findElement(id))
                element->orientation = (element->orientation + 1) & 3;

        notifyChanged();
    }

    void SchematicCanvas::flip()
    {
        if (tool == Tool::Place)
        {
            pendingMirrored = ! pendingMirrored;
            repaint();
            return;
        }

        if (selectedIds.empty())
            return;

        // Mirroring moves the pins, so it can change connections.
        for (const int id : selectedIds)
            if (auto* element = schematic.findElement(id))
                element->mirrored = ! element->mirrored;

        notifyChanged();
    }

    void SchematicCanvas::flipVertical()
    {
        // A vertical mirror is a mirror plus a half turn, keeping the state to the two saved
        // fields.
        if (tool == Tool::Place)
        {
            pendingMirrored = ! pendingMirrored;
            pendingOrientation = (pendingOrientation + 2) & 3;
            repaint();
            return;
        }

        if (selectedIds.empty())
            return;

        for (const int id : selectedIds)
        {
            if (auto* element = schematic.findElement(id))
            {
                element->mirrored = ! element->mirrored;
                element->orientation = (element->orientation + 2) & 3;
            }
        }

        notifyChanged();
    }

    void SchematicCanvas::duplicateSelection()
    {
        if (selectedIds.empty() && selectedWireIds.empty())
            return;

        // Offset so the copies are visible and draggable.
        constexpr int offset = 2;

        std::vector<int> copies;

        for (const int id : selectedIds)
        {
            const auto* source = schematic.findElement(id);

            if (source == nullptr)
                continue;

            // Copy before adding: adding can reallocate and invalidate the pointer.
            const auto original = *source;

            const int copy = schematic.addElement(original.type,
                                                  original.x + offset, original.y + offset);

            if (auto* placed = schematic.findElement(copy))
            {
                original.copyPropertiesTo(*placed);
                placed->x = original.x + offset;
                placed->y = original.y + offset;
                copies.push_back(copy);
            }
        }

        // Selected wires are copied at the same offset. addWire only splits diagonals, so each
        // copy is the last wire added.
        std::vector<int> wireCopies;

        for (const int id : selectedWireIds)
        {
            const auto* source = schematic.findWire(id);

            if (source == nullptr)
                continue;

            const juce::Point<int> delta(offset, offset);
            const auto count = schematic.getWires().size();
            schematic.addWire(source->a + delta, source->b + delta);

            if (schematic.getWires().size() > count)
                wireCopies.push_back(schematic.getWires().back().id);
        }

        // The copies become the selection.
        setSelection(std::move(copies), std::move(wireCopies));
        notifyChanged();
    }

    void SchematicCanvas::deleteSelection()
    {
        if (selectedIds.empty() && selectedWireIds.empty())
            return;

        // Copied first: removing parts would change the selection mid-loop.
        const auto doomed = selectedIds;
        const auto doomedWires = selectedWireIds;
        setSelection(-1);
        setSelectedWires({});

        for (const int id : doomed)
            schematic.removeElement(id);

        for (const int id : doomedWires)
            schematic.removeWire(id);

        notifyChanged();
    }

    bool SchematicCanvas::deleteAt(juce::Point<float> pixel)
    {
        const int hit = elementAt(pixel);

        if (hit >= 0)
        {
            schematic.removeElement(hit);

            if (isSelected(hit))
            {
                auto remaining = selectedIds;
                remaining.erase(std::remove(remaining.begin(), remaining.end(), hit), remaining.end());
                setSelection(std::move(remaining));
            }

            notifyChanged();
            return true;
        }

        // No part here: delete wires passing through the snapped point.
        if (schematic.removeWiresAt(painter.toGrid(pixel)) > 0)
        {
            notifyChanged();
            return true;
        }

        return false;
    }
} // namespace Celine
