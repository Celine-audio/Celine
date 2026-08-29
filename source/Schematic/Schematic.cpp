#include "Schematic.h"

#include <limits>
#include <map>
#include <numeric>
#include <unordered_set>

namespace SchematicModel
{
    namespace
    {
        /** Gives every item a unique, strictly positive id, and returns the next
            free one. Ids already valid and unique are left alone, so an ordinary
            sheet round-trips unchanged.

            Every lookup here -- findElement, findWire, and through them
            selection, the inspector, ganged write-back, probe mapping -- returns
            the *first* match, so two parts sharing an id leaves the second drawn
            and audible but impossible to select, edit or delete. A hand-edited
            or truncated file is exactly where that comes from. Id 0 is worse
            still: it is also the "nothing selected" sentinel. */
        template <typename Item>
        int healIds(std::vector<Item>& items)
        {
            int next = 1;

            for (const auto& item : items)
                if (item.id > 0 && item.id < std::numeric_limits<int>::max())
                    next = std::max(next, item.id + 1);

            std::unordered_set<int> seen;

            for (auto& item : items)
                if (item.id <= 0 || ! seen.insert(item.id).second)
                    item.id = next++;

            return next;
        }

        /** Union-find over connection points. Small, and rebuilt from scratch
            every time nets are extracted -- there is no incremental
            connectivity to get out of step with the drawing. */
        struct DisjointSet
        {
            std::vector<int> parent;

            int add()
            {
                parent.push_back(static_cast<int>(parent.size()));
                return static_cast<int>(parent.size()) - 1;
            }

            int find(int i)
            {
                while (parent[static_cast<size_t>(i)] != i)
                {
                    // Path halving: keeps the trees flat without recursion.
                    parent[static_cast<size_t>(i)] = parent[static_cast<size_t>(parent[static_cast<size_t>(i)])];
                    i = parent[static_cast<size_t>(i)];
                }

                return i;
            }

            void unite(int a, int b)
            {
                a = find(a);
                b = find(b);

                if (a != b)
                    parent[static_cast<size_t>(b)] = a;
            }
        };

        /** Orders grid points so they can key a std::map. */
        struct PointLess
        {
            bool operator()(juce::Point<int> a, juce::Point<int> b) const noexcept
            {
                return a.y != b.y ? a.y < b.y : a.x < b.x;
            }
        };
        /** Joins every registered point that lies along a wire to that wire --
            pins dropped onto one, and wires ending partway along another.

            Indexed by row and column first, for the reason findJunctions is:
            unindexed it made extraction quadratic, 1.85 ms on a 400-part sheet
            against 38 us on a 50-part one. */
        void joinPointsLyingOnWires(DisjointSet& sets,
                                    const std::map<juce::Point<int>, int, PointLess>& pointIds,
                                    const std::vector<Wire>& wires)
        {
            std::map<int, std::vector<std::pair<juce::Point<int>, int>>> byColumn, byRow;

            for (const auto& [point, id] : pointIds)
            {
                byColumn[point.x].emplace_back(point, id);
                byRow[point.y].emplace_back(point, id);
            }

            for (const auto& wire : wires)
            {
                const int wireSet = pointIds.at(wire.a);

                const auto joinAll = [&](const std::vector<std::pair<juce::Point<int>, int>>& candidates)
                {
                    for (const auto& [point, id] : candidates)
                        if (wire.contains(point))
                            sets.unite(wireSet, id);
                };

                // A zero-length wire counts as both, so this is two ifs and not
                // an else -- dropping it from one index would lose a connection.
                if (wire.isVertical())
                    if (const auto column = byColumn.find(wire.a.x); column != byColumn.end())
                        joinAll(column->second);

                if (wire.isHorizontal())
                    if (const auto row = byRow.find(wire.a.y); row != byRow.end())
                        joinAll(row->second);
            }
        }

    } // namespace

    //==========================================================================
    // Elements
    //==========================================================================

    int Schematic::addElement(ElementType type, int x, int y)
    {
        Element element;
        element.id = nextElementId++;
        element.type = type;
        element.x = x;
        element.y = y;

        const auto& info = getElementInfo(type);
        element.value = info.defaultValue;
        element.valueB = info.defaultValueB;

        elements.push_back(element);
        return element.id;
    }

    void Schematic::removeElement(int id)
    {
        for (size_t i = 0; i < elements.size(); ++i)
        {
            if (elements[i].id == id)
            {
                elements.erase(elements.begin() + static_cast<long>(i));
                return;
            }
        }
    }

    Element* Schematic::findElement(int id) noexcept
    {
        for (auto& element : elements)
            if (element.id == id)
                return &element;

        return nullptr;
    }

    const Element* Schematic::findElement(int id) const noexcept
    {
        for (const auto& element : elements)
            if (element.id == id)
                return &element;

        return nullptr;
    }

    juce::Rectangle<int> Schematic::getElementBounds(const Element& element) const noexcept
    {
        // Min and max by hand, not Rectangle::getUnion: a rectangle from two
        // identical points is zero-sized, JUCE counts that as empty, and
        // getUnion returns the *other* operand when either side is empty -- so
        // unioning pin positions collapsed to a single pin.
        //
        // The centre is included because several symbols are drawn around it
        // rather than between their pins. A Text note has no pins at all, so it
        // spans what it reads, using the estimate the renderer draws with.
        if (element.type == ElementType::Text)
        {
            const int halfWidth = element.getTextHalfWidth();
            return juce::Rectangle<int>(element.x - halfWidth, element.y - 1,
                                        halfWidth * 2 + 1, 3);
        }

        // A Rectangle is its bounds -- exactly, and not expanded by a square
        // like the parts below, since the outline is drawn on the edge and a
        // frame that didn't sit on the line it draws would be visibly wrong.
        if (element.type == ElementType::Rectangle)
            return element.getRectangleBounds();

        // A Node is its tag, as wide as the name inside it -- the pin loop below
        // would box it to three squares and leave most of a long name
        // unclickable. Grown by one, like the Text note: getNodeBounds() is the
        // *drawn* box, whose far edges are grid lines the tag is painted on and
        // Rectangle::contains excludes.
        if (element.type == ElementType::Node)
        {
            const auto tag = element.getNodeBounds();
            return {tag.getX(), tag.getY(), tag.getWidth() + 1, tag.getHeight() + 1};
        }

        int minX = element.x, maxX = element.x;
        int minY = element.y, maxY = element.y;

        for (int pin = 0; pin < element.getPinCount(); ++pin)
        {
            const auto p = element.getPinPosition(pin);
            minX = juce::jmin(minX, p.x);
            maxX = juce::jmax(maxX, p.x);
            minY = juce::jmin(minY, p.y);
            maxY = juce::jmax(maxY, p.y);
        }

        // Grown by a square so the body between the pins is grabbable, and so a
        // one-pin terminal is a target rather than a point.
        return juce::Rectangle<int>(minX, minY, maxX - minX + 1, maxY - minY + 1).expanded(1);
    }

    bool Schematic::hitTest(const Element& element, juce::Point<int> gridPoint) const noexcept
    {
        // A group box is grabbed by its edge. Its middle is full of other
        // people's parts, and a box that swallowed every click inside it would
        // make everything it rings unselectable -- the opposite of the point.
        // The band is a square either side of the outline, so it stays grabbable
        // at any zoom.
        if (element.type == ElementType::Rectangle)
        {
            const auto bounds = element.getRectangleBounds();
            return bounds.expanded(1).contains(gridPoint) && ! bounds.reduced(1).contains(gridPoint);
        }

        return getElementBounds(element).contains(gridPoint);
    }

    int Schematic::findElementAt(juce::Point<int> gridPoint) const noexcept
    {
        // Parts before boxes: a Rectangle is drawn behind everything, so it has
        // to lose under the cursor too. This searches newest first, and a box is
        // placed last.
        for (const bool boxes : {false, true})
            for (auto it = elements.rbegin(); it != elements.rend(); ++it)
                if ((it->type == ElementType::Rectangle) == boxes && hitTest(*it, gridPoint))
                    return it->id;

        return -1;
    }

    int Schematic::countElementsOfType(ElementType type) const noexcept
    {
        int count = 0;

        for (const auto& element : elements)
            if (element.type == type)
                ++count;

        return count;
    }

    juce::Rectangle<int> Schematic::getContentBounds() const noexcept
    {
        juce::Rectangle<int> bounds;
        bool first = true;

        auto include = [&](juce::Rectangle<int> box)
        {
            bounds = first ? box : bounds.getUnion(box);
            first = false;
        };

        for (const auto& element : elements)
            include(getElementBounds(element));

        // A wire is a line, so its rectangle can be zero in one dimension.
        // Expanded before it goes in, or a sheet of one straight wire unions an
        // "empty" box and comes back with whatever it was unioned onto.
        for (const auto& wire : wires)
            include(juce::Rectangle<int>(wire.a, wire.b).expanded(1));

        return first ? juce::Rectangle<int>{} : bounds;
    }

    Schematic::Merged Schematic::merge(const Schematic& other, juce::Point<int> delta)
    {
        Merged arrived;
        arrived.elementIds.reserve(other.elements.size());
        arrived.wireIds.reserve(other.wires.size());

        // Copied whole rather than through copyPropertiesTo, which leaves
        // placement behind: an import keeps its layout and shifts as one.
        for (const auto& source : other.elements)
        {
            Element copy = source;
            copy.id = nextElementId++;
            copy.x += delta.x;
            copy.y += delta.y;

            arrived.elementIds.push_back(copy.id);
            elements.push_back(std::move(copy));
        }

        for (const auto& source : other.wires)
        {
            Wire copy = source;
            copy.id = nextWireId++;
            copy.a += delta;
            copy.b += delta;

            arrived.wireIds.push_back(copy.id);
            wires.push_back(copy);
        }

        return arrived;
    }

    //==========================================================================
    // Wires
    //==========================================================================

    void Schematic::addWire(juce::Point<int> from, juce::Point<int> to)
    {
        if (from == to)
            return;

        if (from.x == to.x || from.y == to.y)
        {
            wires.push_back({nextWireId++, from, to});
            return;
        }

        // A diagonal drag becomes an L, across then down: two segments meeting
        // at a corner, joined by the net extractor because they share an
        // endpoint. Two ids as well, so either can be deleted on its own when
        // the corner went the wrong way round.
        const juce::Point<int> corner{to.x, from.y};
        wires.push_back({nextWireId++, from, corner});
        wires.push_back({nextWireId++, corner, to});
    }

    const Wire* Schematic::findWire(int id) const noexcept
    {
        const auto it = std::find_if(wires.begin(), wires.end(),
                                     [id](const Wire& w) { return w.id == id; });

        return it == wires.end() ? nullptr : &(*it);
    }

    bool Schematic::removeWire(int id)
    {
        const auto it = std::find_if(wires.begin(), wires.end(),
                                     [id](const Wire& w) { return w.id == id; });

        if (it == wires.end())
            return false;

        wires.erase(it);
        return true;
    }

    void Schematic::moveWire(int id, juce::Point<int> delta)
    {
        for (auto& wire : wires)
        {
            if (wire.id != id)
                continue;

            wire.a += delta;
            wire.b += delta;
            return;
        }
    }

    bool Schematic::resizeWireEnd(int id, int end, juce::Point<int> gridPoint)
    {
        for (auto& wire : wires)
        {
            if (wire.id != id)
                continue;

            auto& moving = end == 0 ? wire.a : wire.b;
            const auto anchor = end == 0 ? wire.b : wire.a;

            // Only the coordinate running along the wire comes from the
            // pointer; the other stays on the anchor, which keeps the wire
            // axis-aligned however far off the line the mouse strays.
            //
            // isVertical rather than isHorizontal because a zero-length wire is
            // both, and a file can carry one in: treating it as vertical picks
            // an axis rather than leaving the drag doing nothing.
            auto wanted = wire.isVertical() ? juce::Point<int>(anchor.x, gridPoint.y)
                                            : juce::Point<int>(gridPoint.x, anchor.y);

            // Anywhere along the line except onto the anchor. Dragged far
            // enough the end passes through and the wire points the other way,
            // which is the honest result. Landing *on* the anchor is refused: a
            // zero-length wire is invisible and unclickable.
            if (wanted == anchor || wanted == moving)
                return false;

            moving = wanted;
            return true;
        }

        return false;
    }

    int Schematic::mergeCollinearWires(int preferredId)
    {
        const auto before = wires.size();

        // Two wires join when they run along the same line and their spans touch
        // or overlap. Overlapping counts because a wire drawn over another is a
        // duplicate, and the sheet plainly shows one wire.
        const auto joinable = [](const Wire& x, const Wire& y)
        {
            if (x.isVertical() && y.isVertical() && x.a.x == y.a.x)
            {
                const int lo = juce::jmax(juce::jmin(x.a.y, x.b.y), juce::jmin(y.a.y, y.b.y));
                const int hi = juce::jmin(juce::jmax(x.a.y, x.b.y), juce::jmax(y.a.y, y.b.y));
                return lo <= hi;
            }

            if (x.isHorizontal() && y.isHorizontal() && x.a.y == y.a.y)
            {
                const int lo = juce::jmax(juce::jmin(x.a.x, x.b.x), juce::jmin(y.a.x, y.b.x));
                const int hi = juce::jmin(juce::jmax(x.a.x, x.b.x), juce::jmax(y.a.x, y.b.x));
                return lo <= hi;
            }

            return false;
        };

        // Whether a point carries a connection that only exists because a wire
        // ends there.
        //
        // Connectivity is mediated by *points*: extractNets registers pins and
        // wire ends, and joins a wire to every registered point lying on it. So
        // merging two collinear wires deletes the ends where they met, and if
        // that was the only thing registering the point, every other wire
        // through it comes loose.
        //
        // Which is how a four-way node fell apart. Merge the two horizontals and
        // the verticals still end there, so the point survives. Merge the
        // verticals too and nothing registers it: what is left is a long
        // horizontal and a long vertical that merely cross, and a crossing is
        // not a connection. The sheet looked identical and the circuit had come
        // apart in the middle.
        //
        // A T still merges, because its stem *ends* at the seam and keeps the
        // point registered whatever the bar does. So the question is not "is
        // anything at the seam" but "does the seam survive as a registered
        // point" -- and a wire merely passing through does not register one.
        const auto seamSurvives = [this](juce::Point<int> point, const Wire& x, const Wire& y)
        {
            for (const auto& element : elements)
                for (int pin = 0; pin < element.getPinCount(); ++pin)
                    if (element.getPinPosition(pin) == point)
                        return true;

            for (const auto& other : wires)
                if (&other != &x && &other != &y && (other.a == point || other.b == point))
                    return true;

            return false;
        };

        // And nothing is stranded unless something was relying on the seam to
        // reach these two in the first place.
        const auto somethingRunsThrough = [this](juce::Point<int> point, const Wire& x, const Wire& y)
        {
            for (const auto& other : wires)
                if (&other != &x && &other != &y && other.contains(point))
                    return true;

            return false;
        };

        // The ends that stop being ends. For two collinear wires the merged span
        // runs from the outermost pair, so anything strictly inside it is an end
        // about to be dissolved -- one point for a touching pair, two for an
        // overlapping one.
        const auto endsLostByMerging = [](const Wire& x, const Wire& y,
                                          juce::Point<int> low, juce::Point<int> high)
        {
            std::vector<juce::Point<int>> lost;

            for (const auto point : { x.a, x.b, y.a, y.b })
                if (point != low && point != high)
                    lost.push_back(point);

            return lost;
        };

        // Until nothing more will join: one merge can bring the result into
        // contact with a wire neither half reached.
        for (bool merged = true; merged;)
        {
            merged = false;

            for (size_t i = 0; i < wires.size() && ! merged; ++i)
            {
                for (size_t j = i + 1; j < wires.size(); ++j)
                {
                    if (! joinable(wires[i], wires[j]))
                        continue;

                    const auto& first = wires[i];
                    const auto& second = wires[j];

                    // The span of both, which for collinear touching segments is
                    // simply the outermost pair of ends.
                    const juce::Point<int> low{
                        juce::jmin(juce::jmin(first.a.x, first.b.x), juce::jmin(second.a.x, second.b.x)),
                        juce::jmin(juce::jmin(first.a.y, first.b.y), juce::jmin(second.a.y, second.b.y))};
                    const juce::Point<int> high{
                        juce::jmax(juce::jmax(first.a.x, first.b.x), juce::jmax(second.a.x, second.b.x)),
                        juce::jmax(juce::jmax(first.a.y, first.b.y), juce::jmax(second.a.y, second.b.y))};

                    // Leave the pair alone if dissolving where they meet would
                    // strand something that reached them through it.
                    {
                        bool stranding = false;

                        for (const auto point : endsLostByMerging(first, second, low, high))
                            if (! seamSurvives(point, first, second)
                                && somethingRunsThrough(point, first, second))
                                stranding = true;

                        if (stranding)
                            continue;
                    }

                    // The survivor keeps the preferred id when it is in the
                    // pair, so a wire being dragged stays selected; otherwise
                    // the older wins, which keeps ids stable across an edit.
                    const int keep = (wires[j].id == preferredId) ? wires[j].id : wires[i].id;

                    wires[i].id = keep;
                    wires[i].a = low;
                    wires[i].b = high;
                    wires.erase(wires.begin() + static_cast<long>(j));

                    merged = true;
                    break;
                }
            }
        }

        return static_cast<int>(before - wires.size());
    }

    int Schematic::removeWiresAt(juce::Point<int> gridPoint)
    {
        const auto before = wires.size();

        for (size_t i = wires.size(); i > 0; --i)
            if (wires[i - 1].contains(gridPoint))
                wires.erase(wires.begin() + static_cast<long>(i - 1));

        return static_cast<int>(before - wires.size());
    }

    std::vector<juce::Point<int>> Schematic::findJunctions() const
    {
        // A dot is warranted where the connection is not obvious from the
        // drawing: three or more wire ends at a point, or a wire ending partway
        // along another.
        std::map<juce::Point<int>, int, PointLess> endCount;

        for (const auto& wire : wires)
        {
            ++endCount[wire.a];
            ++endCount[wire.b];
        }

        // Wires indexed by the line they run along, which turns the T-junction
        // search from quadratic into near-linear: a point can only sit inside a
        // wire whose axis passes through it. Scanning every wire for every
        // endpoint measured 634 us on a 400-part sheet, inside paint().
        std::map<int, std::vector<const Wire*>> verticalsByX, horizontalsByY;

        for (const auto& wire : wires)
        {
            if (wire.isVertical())
                verticalsByX[wire.a.x].push_back(&wire);

            // Not "else": a zero-length wire is both, and dropping it from one
            // index would make a junction on it invisible.
            if (wire.isHorizontal())
                horizontalsByY[wire.a.y].push_back(&wire);
        }

        std::vector<juce::Point<int>> junctions;

        for (const auto& [point, count] : endCount)
        {
            if (count >= 3)
            {
                junctions.push_back(point);
                continue;
            }

            // Copied out of the structured binding before the lambda captures
            // it: a binding is not a variable, and the Clang that builds this on
            // Linux rejects capturing one where Apple's accepts it.
            const auto at = point;

            // A T: this point is the end of one or two wires and also sits
            // strictly inside a third.
            const auto touches = [&at](const std::vector<const Wire*>& candidates)
            {
                for (const auto* wire : candidates)
                    if (wire->a != at && wire->b != at && wire->contains(at))
                        return true;

                return false;
            };

            bool found = false;

            if (const auto column = verticalsByX.find(point.x); column != verticalsByX.end())
                found = touches(column->second);

            if (! found)
                if (const auto row = horizontalsByY.find(point.y); row != horizontalsByY.end())
                    found = touches(row->second);

            if (found)
                junctions.push_back(point);
        }

        return junctions;
    }

    //==========================================================================
    // Connectivity
    //==========================================================================

    NetList Schematic::extractNets() const
    {
        NetList result;
        result.netOfPin.assign(elements.size(), {});

        for (auto& pins : result.netOfPin)
            pins.fill(-1);

        // Every point that can take part in a connection: element pins, and
        // wire endpoints. Coincident points share an entry, which is what makes
        // two pins touching -- with no wire at all -- a connection.
        DisjointSet sets;
        std::map<juce::Point<int>, int, PointLess> pointIds;

        auto idOf = [&](juce::Point<int> p)
        {
            auto it = pointIds.find(p);
            if (it != pointIds.end())
                return it->second;

            const int id = sets.add();
            pointIds.emplace(p, id);
            return id;
        };

        for (size_t e = 0; e < elements.size(); ++e)
            for (int pin = 0; pin < elements[e].getPinCount(); ++pin)
                result.netOfPin[e][static_cast<size_t>(pin)] = idOf(elements[e].getPinPosition(pin));

        for (const auto& wire : wires)
            sets.unite(idOf(wire.a), idOf(wire.b));

        // Nodes carrying the same label are one net -- a wire you did not have
        // to draw. United here rather than named later, which is the whole of
        // why it works: a net gets exactly one name, so a node sharing its net
        // with an Output would have had its label overwritten and joined
        // nothing. A union survives that, and the net is still called "out".
        {
            std::map<juce::String, int> firstPinOfLabel;

            for (size_t e = 0; e < elements.size(); ++e)
            {
                if (elements[e].type != ElementType::Node)
                    continue;

                const auto label = elements[e].label.trim().toLowerCase();

                // No label, no net: an unlabelled node stays on its own rather
                // than every unlabelled one welding together.
                if (label.isEmpty())
                    continue;

                const int pinId = result.netOfPin[e][0];

                if (const auto seen = firstPinOfLabel.find(label); seen != firstPinOfLabel.end())
                    sets.unite(seen->second, pinId);
                else
                    firstPinOfLabel.emplace(label, pinId);
            }
        }

        joinPointsLyingOnWires(sets, pointIds, wires);

        // Collapse the roots into dense net indices.
        std::map<int, int> netOfRoot;

        auto netIndexOf = [&](int pointId)
        {
            const int root = sets.find(pointId);
            auto it = netOfRoot.find(root);

            if (it != netOfRoot.end())
                return it->second;

            const int net = static_cast<int>(netOfRoot.size());
            netOfRoot.emplace(root, net);
            return net;
        };

        // Assign in a stable order -- by point position -- so net numbering
        // doesn't shuffle when an unrelated element is added.
        for (const auto& [point, id] : pointIds)
        {
            juce::ignoreUnused(point);
            netIndexOf(id);
        }

        for (size_t e = 0; e < elements.size(); ++e)
            for (int pin = 0; pin < elements[e].getPinCount(); ++pin)
            {
                auto& slot = result.netOfPin[e][static_cast<size_t>(pin)];
                slot = netIndexOf(slot);
            }

        result.netNames.assign(netOfRoot.size(), {});

        // Terminals name their net, before the automatic numbering, so two
        // Ground symbols both produce "gnd" and become one node with no wire
        // between them.
        //
        // In priority order, not element order: a net with a Ground on it is
        // ground whatever else shares it. Letting the last-drawn terminal win
        // meant an Output placed after its Ground named the shared net "out",
        // leaving the sheet with no ground at all, and the builder's "output
        // wired to ground" check -- which reads the name -- never fired.
        // Node is skipped: it has done its work by uniting, and naming the net
        // too would fight with a Ground or Output sharing it.
        const std::pair<ElementType, const char*> namingOrder[] = {
            {ElementType::Ground, "gnd"},
            {ElementType::Input, "in"},
            {ElementType::Output, "out"},
        };

        for (const auto& [type, name] : namingOrder)
            for (size_t e = 0; e < elements.size(); ++e)
            {
                const int net = result.netOfPin[e][0];

                if (net < 0 || elements[e].type != type)
                    continue;

                auto& slot = result.netNames[static_cast<size_t>(net)];
                if (slot.isEmpty())
                    slot = name;
            }

        int autoNumber = 1;

        for (auto& name : result.netNames)
            if (name.isEmpty())
                name = "n" + juce::String(autoNumber++);

        // Count what each net touches, so the editor can flag the ones that
        // touch only one thing.
        std::vector<int> connectionCount(result.netNames.size(), 0);

        for (size_t e = 0; e < elements.size(); ++e)
            for (int pin = 0; pin < elements[e].getPinCount(); ++pin)
                ++connectionCount[static_cast<size_t>(result.netOfPin[e][static_cast<size_t>(pin)])];

        for (size_t net = 0; net < connectionCount.size(); ++net)
            if (connectionCount[net] == 1)
                result.danglingNets.push_back(static_cast<int>(net));

        return result;
    }

    //==========================================================================
    // Whole-sheet operations
    //==========================================================================

    void Schematic::clear()
    {
        elements.clear();
        wires.clear();
        nextElementId = 1;
        nextWireId = 1;
    }

    //==========================================================================
    // Persistence
    //==========================================================================

    juce::ValueTree Schematic::toValueTree() const
    {
        juce::ValueTree tree("SCHEMATIC");

        for (const auto& element : elements)
        {
            juce::ValueTree node("ELEMENT");
            node.setProperty("id", element.id, nullptr);
            node.setProperty("type", static_cast<int>(element.type), nullptr);
            node.setProperty("x", element.x, nullptr);
            node.setProperty("y", element.y, nullptr);
            node.setProperty("orientation", element.orientation, nullptr);
            node.setProperty("mirrored", element.mirrored, nullptr);
            node.setProperty("value", element.value, nullptr);

            // Written only when the part has one, so a sheet full of resistors
            // reads the same as it always did.
            if (element.hasSecondValue())
                node.setProperty("valueB", element.valueB, nullptr);

            // Likewise: only a Rectangle has a size, so nothing else grows two
            // properties it would never read back.
            if (element.type == ElementType::Rectangle)
            {
                node.setProperty("width", element.width, nullptr);
                node.setProperty("height", element.height, nullptr);
            }

            // Only a scope carries axes, on the same principle: nothing grows a
            // property it would never read.
            if (element.type == ElementType::Scope)
            {
                node.setProperty("scopeAuto", element.scopeAutoScale, nullptr);
                node.setProperty("scopeMin", element.scopeMin, nullptr);
                node.setProperty("scopeMax", element.scopeMax, nullptr);
                node.setProperty("scopeSeconds", element.scopeSeconds, nullptr);
            }

            if (element.controlOrder != 0)
                node.setProperty("controlOrder", element.controlOrder, nullptr);
            // The id, never the index. Written even for parts with a single
            // model so that a reader never has to guess which field it is
            // looking at, and omitted only where the type has no models at all.
            // What the sheet asked for, which is not always what it got: an id
            // this build could not resolve is written back unchanged rather
            // than replaced by the fallback's, so obtaining the model later
            // still repairs the sheet.
            const auto id = element.unresolvedModelId.isNotEmpty()
                              ? element.unresolvedModelId
                              : getModelId(element.type, element.modelIndex);

            if (id.isNotEmpty())
                node.setProperty("modelId", id, nullptr);
            node.setProperty("label", element.label, nullptr);
            node.setProperty("taper", static_cast<int>(element.taper), nullptr);
            node.setProperty("polarised", element.polarised, nullptr);
            node.setProperty("closed", element.closed, nullptr);

            // Only a pot has one, on the same principle as valueB and the
            // rectangle's size: nothing grows a property it would never read.
            if (element.type == ElementType::Potentiometer)
                node.setProperty("controlPosition", element.controlPosition, nullptr);

            // And only the Output terminal carries a cabinet. Written even when
            // switched off, unlike the properties above, because "off" is a
            // setting someone chose: a sheet saved with the cab bypassed and its
            // file still named has to come back that way rather than as a sheet
            // that never had one.
            if (element.type == ElementType::Output)
            {
                node.setProperty("cab", element.cabEnabled, nullptr);
                node.setProperty("cabFile", element.cabFile, nullptr);
            }

            tree.appendChild(node, nullptr);
        }

        for (const auto& wire : wires)
        {
            juce::ValueTree node("WIRE");
            node.setProperty("id", wire.id, nullptr);
            node.setProperty("x1", wire.a.x, nullptr);
            node.setProperty("y1", wire.a.y, nullptr);
            node.setProperty("x2", wire.b.x, nullptr);
            node.setProperty("y2", wire.b.y, nullptr);
            tree.appendChild(node, nullptr);
        }

        return tree;
    }

    void Schematic::restoreFromValueTree(const juce::ValueTree& tree)
    {
        if (! tree.hasType("SCHEMATIC"))
            return;

        clear();

        loadWarnings.clear();
        int elementsWithoutModelId = 0;

        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            const auto node = tree.getChild(i);

            if (node.hasType("ELEMENT"))
            {
                Element element;
                element.id = node.getProperty("id", 0);
                const int type = node.getProperty("type", 0);

                // A file written by a later version could name a type this build
                // doesn't have. Dropping it beats constructing something that
                // would index off the end of the element table.
                if (type < 0 || type >= numElementTypes)
                    continue;

                element.type = static_cast<ElementType>(type);
                element.x = node.getProperty("x", 0);
                element.y = node.getProperty("y", 0);
                element.orientation = static_cast<int>(node.getProperty("orientation", 0)) & 3;
                element.mirrored = node.getProperty("mirrored", false);
                element.value = node.getProperty("value", 0.0);

                // Defaults to 1, so a transformer saved when `value` was the
                // whole ratio still reads as value:1.
                element.valueB = node.getProperty("valueB", 1.0);

                // Defaulted from the freshly-placed size rather than from zero,
                // so a node missing them -- anything that isn't a Rectangle --
                // still leaves a usable box behind if it ever becomes one.
                element.width = node.getProperty("width", element.width);
                element.height = node.getProperty("height", element.height);
                element.controlOrder = node.getProperty("controlOrder", 0);
                // Models are named, not numbered. Three things can happen, and
                // only the first is silent:
                //
                //   the id is known         -- use it
                //   the id is unknown       -- model 0, and say which id it was
                //   there is no id at all   -- a sheet from before model ids,
                //                              so every part on it is about to
                //                              fall back; say that once
                //
                // Falling back quietly is the failure this whole scheme exists
                // to remove: the sheet still builds and still makes sound, just
                // with the wrong parts in it.
                if (element.hasModelChoice())
                {
                    const auto id = node.getProperty("modelId", "").toString();

                    if (id.isEmpty())
                    {
                        element.modelIndex = 0;
                        ++elementsWithoutModelId;
                    }
                    else if (const auto found = findModelById(element.type, id); found >= 0)
                    {
                        element.modelIndex = found;
                    }
                    else
                    {
                        element.modelIndex = 0;
                        element.unresolvedModelId = id;
                        loadWarnings.add("\"" + id + "\" model not found in this version.");
                    }
                }
                element.label = node.getProperty("label", "").toString();
                element.polarised = node.getProperty("polarised", false);
                element.closed = node.getProperty("closed", true);

                // Centred, not zero, for a sheet saved before knob positions
                // were remembered -- those pots were drawn without a position,
                // and noon is the honest reading of "unstated".
                element.controlPosition = node.getProperty("controlPosition", 0.5);

                // Absent on every sheet written before cabinets existed, and on
                // every part that isn't an Output. Both read as "no cab", which
                // is what those sheets meant.
                element.cabEnabled = node.getProperty("cab", false);
                element.cabFile = node.getProperty("cabFile", "").toString();

                // "log" is the older boolean this replaced; read it so sheets
                // saved before the third taper existed still load.
                const int taper = node.getProperty("taper", node.getProperty("log", false) ? 1 : 0);
                element.taper = static_cast<Taper>(juce::jlimit(0, numTapers - 1, taper));

                // Absent on every sheet written before a scope had axes, which
                // read as the auto-scaling those sheets were drawn against.
                element.scopeAutoScale = node.getProperty("scopeAuto", true);
                element.scopeMin = node.getProperty("scopeMin", -5.0);
                element.scopeMax = node.getProperty("scopeMax", 5.0);

                // Clamped rather than trusted: the time base divides into the
                // column count, so a zero or a negative from a hand-edited file
                // would be a division by zero on the audio thread.
                element.scopeSeconds = juce::jlimit(0.001, 2.0,
                                                    static_cast<double>(node.getProperty("scopeSeconds", 0.04)));

                elements.push_back(element);
            }
            else if (node.hasType("WIRE"))
            {
                Wire wire;

                wire.id = node.getProperty("id", 0);
                wire.a = {static_cast<int>(node.getProperty("x1", 0)), static_cast<int>(node.getProperty("y1", 0))};
                wire.b = {static_cast<int>(node.getProperty("x2", 0)), static_cast<int>(node.getProperty("y2", 0))};

                wires.push_back(wire);
            }
        }

        // Both id spaces are repaired here rather than as each item is read,
        // because uniqueness is a property of the whole sheet: an id can only be
        // known to be a duplicate once everything sharing it has been seen.
        //
        // Wires used to be numbered inline, which covered the sheets that
        // predate wires having ids at all but not two wires that arrived with
        // the same one. Elements had neither. Both now go through the same pass,
        // so "every id is unique and positive" holds however the file got here.
        nextElementId = healIds(elements);
        nextWireId = healIds(wires);

        // One line rather than one per part. A sheet written before models were
        // named has no ids anywhere, so every part with a model choice on it
        // falls back at once -- reporting each would bury the console under a
        // message that says the same thing every time.
        //
        // Reported at all because the alternative is the failure this scheme
        // exists to remove. Such a sheet still builds and still makes sound; it
        // just has the wrong parts in it, which is far harder to notice than a
        // file that refuses to open.
        if (elementsWithoutModelId > 0)
            loadWarnings.add("This sheet predates model ids, so "
                                 + juce::String(elementsWithoutModelId)
                                 + (elementsWithoutModelId == 1 ? " part has" : " parts have")
                                 + " fallen back to the first model in the list. Check them"
                                   " before trusting how it sounds.");
    }
} // namespace SchematicModel
