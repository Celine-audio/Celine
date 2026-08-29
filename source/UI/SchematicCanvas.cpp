#include "SchematicCanvas.h"

#include "Fonts.h"
#include "Theme.h"

namespace SchematicUI
{
    using namespace SchematicModel;

    namespace
    {
        const juce::Colour ghostColour = Theme::text().withAlpha (0.4f);

        constexpr float minGridSize = 4.0f;
        constexpr float maxGridSize = 40.0f;
    } // namespace

    //==========================================================================

    SchematicCanvas::SchematicCanvas(Schematic& s) : schematic(s)
    {
        painter.gridSize = 16.0f;
        painter.origin = {40.0f, 40.0f};

        setWantsKeyboardFocus(true);

        addAndMakeVisible(topRuler);
        addAndMakeVisible(leftRuler);
    }

    void SchematicCanvas::setTool(Tool newTool)
    {
        tool = newTool;
        setMouseCursor(tool == Tool::Select ? juce::MouseCursor::NormalCursor : juce::MouseCursor::CrosshairCursor);

        // The next thing you do is on the sheet, so the sheet takes the
        // keyboard. Without this the shortcuts only work after a click on the
        // canvas, which for the part being placed is one action too late.
        if (isShowing())
            grabKeyboardFocus();

        repaint();

        if (onToolChanged)
            onToolChanged();
    }

    void SchematicCanvas::setPendingType(ElementType type)
    {
        // Picking from the palette is asking for a *fresh* part, so whatever was
        // being cloned stops being cloned.
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

        // The copy comes up facing the way the original does, which is almost
        // always what you want -- and Cmd-R still turns it before you place it.
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

        // Scroll it into view if it is off-screen. Only then: yanking the view
        // about when the part is already visible loses your place for nothing.
        if (const auto* element = schematic.findElement(elementId))
        {
            const auto centre = painter.toPixel({element->x, element->y});
            const auto visible = getLocalBounds().toFloat().reduced(24.0f);

            if (! visible.contains(centre))
                painter.origin += visible.getCentre() - centre;
        }

        repaint();
    }

    void SchematicCanvas::refresh()
    {
        // Drop anything that has gone. Rebuilt from the survivors rather than
        // erased in place, so a sheet replaced wholesale under us leaves no ids
        // pointing at parts that no longer exist.
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

        // Selecting a part drops any wire selection: two lists, one idea.
        // Callers that want both -- the box, which catches each -- set the wires
        // immediately after, so this cannot undo them.
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

        // Assigned directly: the single-list setter clears the wires whenever
        // given any parts, which is right on a click and wrong here, where the
        // parts and the wires are one block.
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

        // Seven pixels of the line, or its width if that is fatter. In pixels
        // rather than grid squares: a hit target is something you aim a pointer
        // at, and the pointer does not zoom.
        const float wireWidth = juce::jmax(1.0f, painter.gridSize * 0.13f);
        const float tolerance = juce::jmax(7.0f, wireWidth) / juce::jmax(1.0f, painter.gridSize);
        float best = tolerance;
        int found = -1;

        for (const auto& wire : schematic.getWires())
        {
            const juce::Line<float> line(wire.a.toFloat(), wire.b.toFloat());
            juce::Point<float> nearest;
            const auto distance = line.getDistanceFromPoint(grid, nearest);

            // <=, so of two wires crossing at the same distance the later one
            // wins -- the one drawn on top, which is the one you were aiming at.
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

        // Compared in pixels rather than in grid squares: the box is a pixel
        // gesture, and rounding it to the grid first would make a box drawn
        // just inside a part's bounds catch it anyway when zoomed out.
        const bool crossing = isCrossingBox();

        std::vector<int> caught = additive ? selectedIds : std::vector<int>{};

        for (const auto& element : schematic.getElements())
        {
            const auto bounds = painter.elementBounds(element);
            const juce::Rectangle<float> pixels(painter.toPixel(bounds.getX(), bounds.getY()),
                                                painter.toPixel(bounds.getRight(), bounds.getBottom()));

            // Left to right encloses, right to left crosses -- the convention
            // in every CAD package.
            const bool hit = crossing ? box.intersects(pixels) : box.contains(pixels);

            if (hit && std::find(caught.begin(), caught.end(), element.id) == caught.end())
                caught.push_back(element.id);
        }

        std::vector<int> caughtWires = additive ? selectedWireIds : std::vector<int>{};

        for (const auto& wire : schematic.getWires())
        {
            const juce::Line<float> line(painter.toPixel(wire.a), painter.toPixel(wire.b));

            // Same rule as the parts: enclosing needs the whole wire inside,
            // crossing takes anything the box touches.
            //
            // Crossing tests the *line*, not its bounding rectangle: every wire
            // is axis-aligned, so that rectangle has zero width or height, and
            // Rectangle::intersects ends with "&& other.w > 0 && other.h > 0"
            // -- false for every wire on the sheet.
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

        // Leave a margin, and room for captions and terminal labels, which are
        // drawn outside the pin geometry the bounds are computed from.
        bounds = bounds.expanded(5);

        const float scaleX = static_cast<float>(getWidth()) / static_cast<float>(juce::jmax(1, bounds.getWidth()));
        const float scaleY = static_cast<float>(getHeight()) / static_cast<float>(juce::jmax(1, bounds.getHeight()));

        painter.gridSize = juce::jlimit(minGridSize, maxGridSize, juce::jmin(scaleX, scaleY));
        painter.origin = {static_cast<float>(getWidth()) * 0.5f
                              - (static_cast<float>(bounds.getCentreX()) * painter.gridSize),
                          static_cast<float>(getHeight()) * 0.5f
                              - (static_cast<float>(bounds.getCentreY()) * painter.gridSize)};

        repaint();
    }

    void SchematicCanvas::resized()
    {
        // Full-length strips, each covering one edge. The corner belongs to the
        // left one, drawn after the top one so it wins there.
        topRuler.setBounds(0, 0, getWidth(), topRulerThickness);
        leftRuler.setBounds(0, 0, leftRulerThickness, getHeight());
        leftRuler.toFront(false);

    }

    //==========================================================================

    void SchematicCanvas::paintGrid(juce::Graphics& g) const
    {
        // Grid dots. Skipped when zoomed out far enough that they would read as
        // a solid fill rather than as a grid.
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
        // Group boxes first, under everything: the circuit a box rings has to
        // stay readable through it.
        for (const auto& element : schematic.getElements())
            if (element.type == ElementType::Rectangle)
                painter.draw(g, element, isSelected(element.id) ? Theme::selected() : Theme::element());

        // Wires under parts.
        const float wireWidth = wireThickness();

        for (const auto& wire : schematic.getWires())
        {
            const bool selected = std::find(selectedWireIds.begin(), selectedWireIds.end(), wire.id)
                               != selectedWireIds.end();

            g.setColour(selected ? Theme::selected() : Theme::wire());
            g.drawLine(juce::Line<float>(painter.toPixel(wire.a), painter.toPixel(wire.b)),
                       selected ? wireWidth * 1.8f : wireWidth);
        }

        // Junction dots where the connection isn't obvious from the drawing.
        //
        // The colour is set explicitly because the loop above leaves the last
        // wire's behind. And it is the wire colour rather than the selection's:
        // a dot belongs to the *connection*, and there is no honest answer for a
        // junction where one of three wires is selected.
        g.setColour(Theme::wire());

        const float junctionRadius = juce::jmax(2.0f, painter.gridSize * 0.22f);

        for (const auto& junction : schematic.findJunctions())
        {
            const auto p = painter.toPixel(junction);
            g.fillEllipse(juce::Rectangle<float>(junctionRadius * 2.0f, junctionRadius * 2.0f).withCentre(p));
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

            // Outside a group box rather than on it: its bounds *are* its
            // outline.
            const auto grid = painter.gridSize;
            const juce::Rectangle<float> pixels =
                juce::Rectangle<float>(painter.toPixel(bounds.getTopLeft()),
                                       painter.toPixel(bounds.getBottomRight()))
                    .expanded(isBox ? grid * 0.5f : 0.0f);

            g.setColour(Theme::selected().withAlpha(0.35f));
            g.drawRoundedRectangle(pixels, grid * 0.4f, 1.5f);

            // Only on a box selected by itself: with several selected the drag
            // moves them all, so the handle would offer a gesture that is not
            // available.
            if (isBox && selectedIds.size() == 1)
            {
                const auto handle = painter.toPixel(bounds.getBottomRight());
                const float size = juce::jmax(3.0f, grid * 0.45f);

                g.setColour(Theme::selected());
                g.fillRect(juce::Rectangle<float>(size * 2.0f, size * 2.0f).withCentre(handle));
            }
        }

        // The ends of a wire selected on its own. A diamond rather than a
        // square, which already means a junction and a box's corner.
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
            g.drawLine(juce::Line<float>(painter.toPixel(wireStart), painter.toPixel(corner)),
                       wireThickness());
            g.drawLine(juce::Line<float>(painter.toPixel(corner), painter.toPixel(wireEnd)),
                       wireThickness());
        }

        // Drawn last, over the drawing, and solid or dashed by which way it was
        // dragged -- otherwise which rule is in force is invisible until you let
        // go.
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
            painter.drawSymbolOnly(g, ghost, ghostColour);
        }
    }

    void SchematicCanvas::paint(juce::Graphics& g)
    {
        g.fillAll(Theme::background());

        paintGrid(g);
        paintDrawing(g);
        paintSelection(g);
        paintInFlight(g);

        // The rulers are children, not part of this paint: their cursor markers
        // repaint without the sheet repainting.
    }

    void SchematicCanvas::mouseDoubleClick(const juce::MouseEvent& event)
    {
        // Only the Select tool: with the wire tool both clicks already mean
        // something.
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
        // Only what the part is *set to*: parts chosen from a list have no such
        // number, so for those the inspector remains the way in. A Text note's
        // words are the same idea.
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
        inlineEditor->setColour(juce::TextEditor::backgroundColourId, Theme::surface());
        inlineEditor->setColour(juce::TextEditor::outlineColourId, Theme::teal());
        inlineEditor->setColour(juce::TextEditor::focusedOutlineColourId, Theme::teal());

        inlineEditor->onReturnKey = [this] { commitInlineEdit(); };
        inlineEditor->onEscapeKey = [this] { closeInlineEdit(); };

        // Focus loss commits, as the inspector's fields do.
        inlineEditor->onFocusLost = [this] { commitInlineEdit(); };

        const auto centre = painter.toPixel({element.x, element.y});
        const float width = juce::jmax(72.0f, painter.gridSize * 6.0f);
        const float height = juce::jmax(22.0f, painter.gridSize * 1.5f);

        inlineEditor->setBounds(juce::Rectangle<float>(width, height)
                                    .withCentre({centre.x, centre.y + painter.gridSize * 3.0f})
                                    .toNearestInt());

        addAndMakeVisible(*inlineEditor);

        // JUCE asserts that a component is showing before it can take focus, and
        // a canvas built where nobody is looking is not. The grab would fail
        // there anyway.
        if (isShowing())
            inlineEditor->grabKeyboardFocus();
    }

    void SchematicCanvas::commitInlineEdit()
    {
        if (inlineEditor == nullptr)
            return;

        const auto typed = inlineEditor->getText();
        const int id = inlineEditId;

        // Closed *first*. Committing fires onSchematicChanged, which the editor
        // answers by rebuilding the inspector -- and an editor still alive and
        // still focused at that point would lose focus and commit again from
        // inside its own commit.
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

        // Refused rather than zeroed. Zero is the table's "nobody has filled this
        // in" value and it draws red and stops a build, so quietly writing it
        // because somebody typed nonsense would turn a typo into a broken sheet.
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

        // The callbacks are cleared before the editor goes, or removing it fires
        // onFocusLost into a half-destroyed object.
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
        // Below about six pixels a square the trace is a smudge, and the whole
        // picture would still be walked and copied to draw it.
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

            // Upright however the part is turned: this is a thing you read.
            const auto centre = painter.toPixel({element.x, element.y});
            const juce::Rectangle<float> area(centre.x + screen.getX() * painter.gridSize,
                                              centre.y + screen.getY() * painter.gridSize,
                                              screen.getWidth() * painter.gridSize,
                                              screen.getHeight() * painter.gridSize);

            drawScopeTrace(g, area.reduced(painter.gridSize * 0.18f), reading,
                           Theme::captionValue(), Theme::comment(), Theme::background());

            // Below the part's own caption, which SymbolPainter puts one square
            // under the lowest pin -- exactly where a readout would otherwise
            // go. Only when there is room to read it.
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
        // Opaque so repainting it never drags the canvas underneath along, and
        // click-through so the sheet still receives the mouse at its edges.
        setOpaque(true);
        setInterceptsMouseClicks(false, false);
        setWantsKeyboardFocus(false);
    }

    void SchematicCanvas::Ruler::paint(juce::Graphics& g)
    {
        const auto thickness =
            static_cast<float>(isHorizontal ? topRulerThickness : leftRulerThickness);

        // Where this strip's numbering may begin: after *the other* ruler, which
        // owns the corner they share. Crossed on purpose -- the top ruler is
        // stopped by the left one's width and vice versa -- and it matters
        // because the two are no longer the same size.
        const auto crossThickness =
            static_cast<float>(isHorizontal ? leftRulerThickness : topRulerThickness);

        const auto& view = owner.painter;

        // Chrome, not sheet: the rulers belong to the window frame in the design,
        // the same aubergine as the toolbar and the inspector, so the sheet reads
        // as something set into them rather than as running under them.
        g.fillAll(Theme::chrome());

        // A definite edge, two pixels of it, which is what separates the frame
        // from the sheet where the two colours nearly meet.
        g.setColour(Theme::line());

        // The two edges meet at the sheet's top-left and run outwards from it,
        // leaving the block where the rulers overlap as plain chrome. Drawing
        // either line *through* that block instead puts a stray stub across the
        // corner, pointing at nothing -- there is no sheet under it to bound.
        if (isHorizontal)
            g.fillRect(0.0f, thickness - 2.0f, static_cast<float>(getWidth()), 2.0f);
        else
            g.fillRect(thickness - 2.0f, crossThickness - 2.0f, 2.0f,
                       static_cast<float>(getHeight()) - crossThickness + 2.0f);

        // One label every `step` grid squares, chosen so they never collide: at
        // 40 px per square every square can be numbered, at 5 px per square only
        // every tenth can.
        int step = 1;

        for (const int candidate : {1, 2, 5, 10, 20, 50, 100})
        {
            step = candidate;

            if (view.gridSize * static_cast<float>(step) >= 44.0f)
                break;
        }

        // The strip covers the whole edge, but the canvas it labels starts after
        // the other ruler -- so ask the canvas, in the canvas's own coordinates,
        // which happen to be this strip's too since both sit at the origin.
        const auto first = view.toGrid({0.0f, 0.0f});
        const auto last = view.toGrid({static_cast<float>(owner.getWidth()),
                                          static_cast<float>(owner.getHeight())});

        g.setFont(Fonts::light(11.0f));

        const auto tickColour = Theme::line();
        const auto labelColour = Theme::textDim();

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

        // The cursor. Drawn at the pointer's actual pixel rather than snapped to
        // the grid, so it tracks the mouse instead of hopping a square at a time.
        if (cursor >= crossThickness)
        {
            // Violet, the drawing's own accent -- not the selection orange it used
            // to borrow. Orange means "this part is selected" everywhere else on
            // the sheet, and a mark that follows the pointer is not a selection.
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

        // Only the two strips repaint, so this is free of whatever the sheet
        // happens to contain.
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

        // Backwards, so the part drawn last -- the one on top -- is the one you
        // get when two overlap.
        for (auto e = elements.rbegin(); e != elements.rend(); ++e)
        {
            if (e->type == ElementType::Rectangle)
                continue;   // boxes are drawn behind everything, so they lose here

            if (painter.elementBounds(*e).contains(grid))
                return e->id;
        }

        // Group boxes last, by their edge only: a box is drawn behind the
        // circuit, so it has to lose to it under the cursor too.
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
        // A real click can only land on a canvas that is showing. What this
        // declines is the synthetic mouseDown the tests inject offscreen.
        if (isShowing())
            grabKeyboardFocus();

        const auto grid = gridAt(event.position);

        // Middle-click picks a part up: the Place tool is armed with a copy of
        // it, so every click after that puts another one down.
        if (event.mods.isMiddleButtonDown())
        {
            if (const int hit = elementAt(event.position); hit >= 0)
                cloneElement(hit);

            return;
        }

        // Right-drag pans, and is the only thing that does.
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

        // A clone brings its value, model, label and settings with it; a fresh
        // part keeps the table's defaults.
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
        // Both handles win over anything under them: they sit on an outline or a
        // pin, either of which would otherwise take every click aimed at them.
        // Only while the thing they belong to is the whole selection, which is
        // also the condition they are drawn under.
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

                    // Cleared here rather than in beginDrag, which this gesture
                    // does not go through: left over from the last drag it would
                    // report a click on a handle as a move.
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
            // Shift-click adds or removes and starts no drag: building a
            // selection up part by part should not move anything.
            if (additive)
            {
                toggleInSelection(hit);
                return;
            }

            // Clicking something already selected keeps the whole selection so
            // it can be dragged as a group. Narrowing back to the one part
            // happens on mouse *up*, if the drag never moved.
            if (! isSelected(hit))
                setSelection(hit);

            beginDrag(grid);
            return;
        }

        // A wire, tried before the selection box, or a wire could only ever be
        // caught by dragging a box round it.
        if (const int wire = wireAt(event.position); wire >= 0)
        {
            if (additive)
                toggleWireInSelection(wire);
            else if (! isWireSelected(wire))
                setSelection(std::vector<int>{}, std::vector<int>{wire});   // same rule as the parts

            beginDrag(grid);
            return;
        }

        // Empty sheet: start a selection box. The existing selection is left
        // alone until the button comes up, so a shift-drag adds to it and a
        // plain drag replaces it -- decided in applyBoxSelection, in one place.
        boxSelecting = true;
        boxAnchor = boxCurrent = event.position;

        // Both lists: a plain click never opens a box, so mouse-up skips
        // applyBoxSelection and this is the only place a click clears anything.
        if (! additive)
        {
            setSelection(-1);
            setSelectedWires({});
        }
    }

    void SchematicCanvas::mouseDrag(const juce::MouseEvent& event)
    {
        // mouseMove stops firing once a button is down, so the markers would
        // freeze exactly while you are dragging a part to a coordinate -- which
        // is when you are most likely to be reading them.
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
                // Not merged yet: joining mid-drag would swap the wire under
                // the pointer for a longer one. That happens once, on mouseUp.
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
            // A delta, not an offset from one part's centre: everything
            // selected has to move by the same amount and keep its spacing.
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

            // Moving a part can change what it touches, so this is a topology
            // edit like any other.
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

                // A wire drawn on from where another ended is the continuation
                // it looks like, so the two become one.
                mergeWiresAfterEdit(-1);
                notifyChanged();
            }
        }

        // The end of a resize, and the end of a wire drag that moved whole
        // wires: either can leave a wire touching a collinear neighbour.
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

            // A click rather than a drag: the box never opened, and the
            // selection was already cleared on the way down.
            if (getSelectionBox().getWidth() >= 2.0f || getSelectionBox().getHeight() >= 2.0f)
                applyBoxSelection(event.mods.isShiftDown());

            repaint();
        }

        // Clicking a member of a group without dragging narrows to that one
        // thing -- the way out of a group. A wire counts, and the count that
        // decides is both lists together, since a wire is picked up by the
        // gesture the parts use.
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

        // Fired unconditionally: whoever is recording undo states needs the
        // boundary even when the gesture changed nothing, or the next drag gets
        // folded into the last one's undo step.
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
        // The diamond that is drawn and not a pixel more: in pixels against the
        // handle, not grid squares against the snapped point. Manhattan
        // distance, which is the diamond exactly.
        const float reach = wireHandleReach();

        const auto near = [this, pixel, reach](juce::Point<int> end)
        {
            const auto centre = painter.toPixel(end);
            return std::abs(pixel.x - centre.x) + std::abs(pixel.y - centre.y) <= reach;
        };

        // `a` first: on a wire two squares or shorter the grab bands overlap,
        // and without a fixed answer the end you caught would depend on which
        // way the wire happened to be stored.
        if (near(wire.a))
            return 0;

        return near(wire.b) ? 1 : -1;
    }

    void SchematicCanvas::mergeWiresAfterEdit(int preferredId)
    {
        if (schematic.mergeCollinearWires(preferredId) == 0)
            return;

        // An absorbed wire no longer exists, and a selection holding its id
        // would draw nothing and delete nothing.
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

        // A square either side, matching the edge's own grab band.
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

        // The element's position is its *centre*, so pinning the opposite
        // corner means moving the centre by half the size change. withCentre and
        // getCentreX are exact inverses at odd sizes too, so this cannot creep.
        element.x = before.getX() + width / 2;
        element.y = before.getY() + height / 2;

        // Announced like any other edit, which is also what keeps the
        // inspector's Width and Height counting along with the drag.
        notifyChanged();
    }

    void SchematicCanvas::mouseExit(const juce::MouseEvent&)
    {
        // A marker left behind at the last position it saw reads as the cursor
        // still being there.
        updateRulerCursor({-1.0f, -1.0f});
    }

    void SchematicCanvas::mouseMove(const juce::MouseEvent& event)
    {
        const auto grid = gridAt(event.position);

        // The markers cost two thin strips to move. The ghost still redraws only
        // when the part would land somewhere new -- that is a full repaint.
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
        // A press hard enough to start a pan can nudge the wheel, so a pan would
        // zoom as it went. While panning, panning wins.
        if (panning || event.mods.isPopupMenu())
            return;

        // Zoom about the cursor. The *unrounded* position: toGrid() snaps, and
        // anchoring on a snapped point drifts the sheet up to half a square a
        // wheel step.
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
        /** Whether a key press means `character`, on any keyboard layout.

            Matched on the character the *layout* produced, folded to lower case,
            with the key code as a fallback -- so R is whichever key types an "r"
            in front of you rather than a position on an American keyboard, and
            Caps Lock does not break it.

            With a command modifier held the text character is not the letter:
            X11 and Windows deliver the ASCII control code (Ctrl-Z arrives as
            0x1A), which is non-zero, so matching on it silently compares 0x1A
            against 'z'. The key code is the layout's letter on every platform,
            so it is preferred there. */
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
        // isCommandDown() is the whole of "respect the system's binding": JUCE
        // maps it to Command on macOS and Control elsewhere.
        if (keyIs(key, 'z'))
        {
            // Shift-Cmd-Z is redo everywhere; Ctrl-Y is the other redo Windows
            // users reach for, handled below.
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

        // Command turns a letter into an action on what is already there, which
        // leaves the bare letters free to name parts.
        if (key.getModifiers().isCommandDown())
            return commandKeyPressed(key);

        if (keyIs(key, 's')) { setTool(Tool::Select); return true; }
        if (keyIs(key, 'w')) { setTool(Tool::Wire); return true; }
        if (keyIs(key, 'x')) { setTool(Tool::Delete); return true; }

        // F frames the drawing. A verb among nouns, allowed because it moves the
        // *view* rather than the sheet; Cmd-F still flips the selection.
        if (keyIs(key, 'f')) { zoomToFit(); return true; }

        // Parts by initial, arming one for placing exactly as the palette does.
        // B is for box, R being the resistor; D is the diode, which is why the
        // Delete tool is X.
        struct PartKey { juce::juce_wchar key; ElementType type; };

        for (const auto& part : {PartKey{'r', ElementType::Resistor},
                                 PartKey{'c', ElementType::Capacitor},
                                 PartKey{'l', ElementType::Inductor},
                                 PartKey{'t', ElementType::Transistor},
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

            // Both lists: setSelection(-1) alone keeps the wires, which is what
            // the box flow wants of it.
            setSelection(-1);
            setSelectedWires({});
            return true;
        }

        return false;
    }

    void SchematicCanvas::rotate()
    {
        // While a part is armed for placing, R turns the thing about to go down
        // rather than whatever happens to be selected behind it.
        if (tool == Tool::Place)
        {
            pendingOrientation = (pendingOrientation + 1) & 3;
            repaint();
            return;
        }

        if (selectedIds.empty())
            return;

        // Each part turns about its own centre rather than the group turning
        // about a common one. Turning the group would move parts off the pins
        // they are wired to, which on a schematic is a different -- and far
        // more destructive -- operation than rotating the symbols.
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

        // Mirroring moves the pins, so it can change what the part touches --
        // a topology edit like moving it.
        for (const int id : selectedIds)
            if (auto* element = schematic.findElement(id))
                element->mirrored = ! element->mirrored;

        notifyChanged();
    }

    void SchematicCanvas::flipVertical()
    {
        // A mirror in the other axis, which is a mirror plus a half turn. Doing
        // it that way rather than adding a second mirror flag keeps the part's
        // state to the two numbers the file format already carries.
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
        if (selectedIds.empty())
            return;

        // Offset so the copies are visibly separate and immediately draggable,
        // rather than hidden exactly behind what they were copied from.
        constexpr int offset = 2;

        std::vector<int> copies;

        for (const int id : selectedIds)
        {
            const auto* source = schematic.findElement(id);

            if (source == nullptr)
                continue;

            // Read the source's fields before adding, since adding can reallocate
            // the element vector and invalidate the pointer.
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

        // The copies become the selection: the thing you just made is the thing
        // you want to move, and leaving the originals selected would make the
        // next drag pull the wrong set.
        setSelection(std::move(copies));
        notifyChanged();
    }

    void SchematicCanvas::deleteSelection()
    {
        if (selectedIds.empty() && selectedWireIds.empty())
            return;

        // Copied first: removeElement walks the same vector the ids index into,
        // and clearing the selection as we go would cut the loop short.
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

        // Nothing placed here, so try the wires. Deleting a wire by clicking
        // anywhere along it -- not just its ends -- is what makes tidying up a
        // drawing bearable.
        // Snapped, unlike the part test above: a wire runs along grid lines, so
        // the nearest intersection is exactly what "on it" means for one.
        if (schematic.removeWiresAt(painter.toGrid(pixel)) > 0)
        {
            notifyChanged();
            return true;
        }

        return false;
    }
} // namespace SchematicUI
