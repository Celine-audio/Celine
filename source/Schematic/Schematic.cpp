#include "Schematic.h"

#include <limits>
#include <map>
#include <numeric>
#include <unordered_set>

namespace SchematicModel
{
    namespace
    {
        /** Gives every item a unique, positive id and returns the next free one. Valid,
            unique ids are kept. Lookups return the first match and 0 means "none", so
            duplicate or zero ids from a damaged file must be fixed. */
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

        /** Union-find over connection points, rebuilt on every extraction. */
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
                    // Path halving.
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

        /** Orders grid points for use as a std::map key. */
        struct PointLess
        {
            bool operator()(juce::Point<int> a, juce::Point<int> b) const noexcept
            {
                return a.y != b.y ? a.y < b.y : a.x < b.x;
            }
        };
        /** Joins each registered point lying along a wire to that wire: pins on a wire, and
            wire ends partway along another. Wires are indexed by row and column so this stays
            near-linear. */
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

                // A zero-length wire is both vertical and horizontal, hence two ifs.
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
        // Min/max by hand (Rectangle::getUnion ignores zero-sized rectangles). The centre is
        // included because some symbols are drawn around it. A Text note spans its text.
        if (element.type == ElementType::Text)
        {
            const int halfWidth = element.getTextHalfWidth();
            return juce::Rectangle<int>(element.x - halfWidth, element.y - 1,
                                        halfWidth * 2 + 1, 3);
        }

        // A Rectangle is exactly its bounds.
        if (element.type == ElementType::Rectangle)
            return element.getRectangleBounds();

        // A Node is its tag, grown by one (Rectangle::contains excludes the far edges).
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

        // Grown by a square so the body and one-pin terminals are easy to click.
        return juce::Rectangle<int>(minX, minY, maxX - minX + 1, maxY - minY + 1).expanded(1);
    }

    bool Schematic::hitTest(const Element& element, juce::Point<int> gridPoint) const noexcept
    {
        // A group box is grabbed by its outline (a square either side), so the parts inside
        // stay clickable.
        if (element.type == ElementType::Rectangle)
        {
            const auto bounds = element.getRectangleBounds();
            return bounds.expanded(1).contains(gridPoint) && ! bounds.reduced(1).contains(gridPoint);
        }

        return getElementBounds(element).contains(gridPoint);
    }

    int Schematic::findElementAt(juce::Point<int> gridPoint) const noexcept
    {
        // Parts first, then boxes, since boxes are drawn behind.
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

        // Expanded first: a straight wire's rectangle has zero size in one dimension.
        for (const auto& wire : wires)
            include(juce::Rectangle<int>(wire.a, wire.b).expanded(1));

        return first ? juce::Rectangle<int>{} : bounds;
    }

    Schematic::Merged Schematic::merge(const Schematic& other, juce::Point<int> delta)
    {
        Merged arrived;
        arrived.elementIds.reserve(other.elements.size());
        arrived.wireIds.reserve(other.wires.size());

        // Copied whole, positions included, so an import keeps its layout.
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

        // A diagonal drag becomes an L (across then down): two wires sharing the corner.
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

            // Only the coordinate along the wire follows the pointer, keeping it straight. A
            // zero-length wire is treated as vertical.
            auto wanted = wire.isVertical() ? juce::Point<int>(anchor.x, gridPoint.y)
                                            : juce::Point<int>(gridPoint.x, anchor.y);

            // The moving end may pass the anchor but not land on it.
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

        // Collinear wires whose spans touch or overlap.
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

        // Whether a point keeps a connection that exists only because a wire ends there.
        // Nets are formed through registered points (pins and wire ends), so merging two
        // wires removes the ends where they met; if nothing else registers that point, wires
        // crossing it would disconnect. A wire merely passing through doesn't register it.
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

        // Whether some other wire relies on the seam to reach these two.
        const auto somethingRunsThrough = [this](juce::Point<int> point, const Wire& x, const Wire& y)
        {
            for (const auto& other : wires)
                if (&other != &x && &other != &y && other.contains(point))
                    return true;

            return false;
        };

        // The ends that disappear in a merge: those strictly inside the merged span.
        const auto endsLostByMerging = [](const Wire& x, const Wire& y,
                                          juce::Point<int> low, juce::Point<int> high)
        {
            std::vector<juce::Point<int>> lost;

            for (const auto point : { x.a, x.b, y.a, y.b })
                if (point != low && point != high)
                    lost.push_back(point);

            return lost;
        };

        // Repeat until nothing joins: one merge can create another.
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

                    // The merged span: the outermost pair of ends.
                    const juce::Point<int> low{
                        juce::jmin(juce::jmin(first.a.x, first.b.x), juce::jmin(second.a.x, second.b.x)),
                        juce::jmin(juce::jmin(first.a.y, first.b.y), juce::jmin(second.a.y, second.b.y))};
                    const juce::Point<int> high{
                        juce::jmax(juce::jmax(first.a.x, first.b.x), juce::jmax(second.a.x, second.b.x)),
                        juce::jmax(juce::jmax(first.a.y, first.b.y), juce::jmax(second.a.y, second.b.y))};

                    // Skip if merging would disconnect something at the seam.
                    {
                        bool stranding = false;

                        for (const auto point : endsLostByMerging(first, second, low, high))
                            if (! seamSurvives(point, first, second)
                                && somethingRunsThrough(point, first, second))
                                stranding = true;

                        if (stranding)
                            continue;
                    }

                    // Keep the preferred id if it is in the pair, else the older id.
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
        // Junction dots: three or more wire ends at a point, or a wire ending partway along
        // another.
        std::map<juce::Point<int>, int, PointLess> endCount;

        for (const auto& wire : wires)
        {
            ++endCount[wire.a];
            ++endCount[wire.b];
        }

        // Wires indexed by the line they run along, so the T-junction search is near-linear.
        std::map<int, std::vector<const Wire*>> verticalsByX, horizontalsByY;

        for (const auto& wire : wires)
        {
            if (wire.isVertical())
                verticalsByX[wire.a.x].push_back(&wire);

            // Not "else": a zero-length wire is both.
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

            // Copied out of the structured binding: some Clang versions can't capture a binding.
            const auto at = point;

            // A T: this point is an end of one or two wires and lies strictly inside a third.
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

        // Every connection point: pins and wire ends. Coincident points share an entry, so
        // touching pins connect without a wire.
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

        // Nodes with the same label are one net. Joined here (not by naming), so it still
        // works when the net is later named by a Ground or Output.
        {
            std::map<juce::String, int> firstPinOfLabel;

            for (size_t e = 0; e < elements.size(); ++e)
            {
                if (elements[e].type != ElementType::Node)
                    continue;

                const auto label = elements[e].label.trim().toLowerCase();

                // Unlabelled nodes stay separate.
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

        // Map roots to dense net indices.
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

        // Numbered in point order, so numbering is stable when unrelated parts are added.
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

        // Terminals name their net, in priority order: Ground wins over anything sharing its
        // net, so two Grounds become one node. Nodes are skipped (already joined above).
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

        // Count connections per net, to flag nets with only one.
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

            // Only written for parts with a second value.
            if (element.hasSecondValue())
                node.setProperty("valueB", element.valueB, nullptr);

            // Only Rectangles have a size.
            if (element.type == ElementType::Rectangle)
            {
                node.setProperty("width", element.width, nullptr);
                node.setProperty("height", element.height, nullptr);
            }

            // Only scopes have axes.
            if (element.type == ElementType::Scope)
            {
                node.setProperty("scopeAuto", element.scopeAutoScale, nullptr);
                node.setProperty("scopeMin", element.scopeMin, nullptr);
                node.setProperty("scopeMax", element.scopeMax, nullptr);
                node.setProperty("scopeSeconds", element.scopeSeconds, nullptr);
            }

            if (element.controlOrder != 0)
                node.setProperty("controlOrder", element.controlOrder, nullptr);
            // The model id, never the index. An unknown id is written back unchanged so the
            // reference isn't lost.
            const auto id = element.unresolvedModelId.isNotEmpty()
                              ? element.unresolvedModelId
                              : getModelId(element.type, element.modelIndex);

            if (id.isNotEmpty())
                node.setProperty("modelId", id, nullptr);
            node.setProperty("label", element.label, nullptr);
            node.setProperty("taper", static_cast<int>(element.taper), nullptr);
            node.setProperty("polarised", element.polarised, nullptr);
            node.setProperty("closed", element.closed, nullptr);

            // A changeover's action and lever position. `closed` (above) is true on throw A, for
            // older builds.
            if (element.type == ElementType::Spdt)
            {
                node.setProperty("action", static_cast<int>(element.switchAction), nullptr);
                node.setProperty("switchPosition", element.switchPosition, nullptr);
            }

            // Only pots have a position.
            if (element.type == ElementType::Potentiometer)
                node.setProperty("controlPosition", element.controlPosition, nullptr);

            // Only Output carries a cabinet; written even when off, to keep the chosen file.
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

                // A type from a later version: skip it.
                if (type < 0 || type >= numElementTypes)
                    continue;

                element.type = static_cast<ElementType>(type);
                element.x = node.getProperty("x", 0);
                element.y = node.getProperty("y", 0);
                element.orientation = static_cast<int>(node.getProperty("orientation", 0)) & 3;
                element.mirrored = node.getProperty("mirrored", false);
                element.value = node.getProperty("value", 0.0);

                // Defaults: 1 for older transformer sheets (ratio in `value`), 0 for a rail.
                element.valueB = node.getProperty("valueB", element.isSecondValueRail() ? 0.0 : 1.0);

                // Default to a new box's size.
                element.width = node.getProperty("width", element.width);
                element.height = node.getProperty("height", element.height);
                element.controlOrder = node.getProperty("controlOrder", 0);
                // Models are stored by id:
                //   known id -> use it;
                //   unknown id -> model 0, and report the id;
                //   no id (a sheet from before model ids) -> model 0, reported once for the sheet.
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

                // Missing on older sheets: ON-ON, with `closed` giving the throw.
                element.switchAction = static_cast<SwitchAction>(
                    juce::jlimit(0, numSwitchActions - 1, static_cast<int>(node.getProperty("action", 0))));
                element.switchPosition = juce::jlimit(
                    0, element.getSwitchPositions() - 1,
                    static_cast<int>(node.getProperty("switchPosition", element.closed ? element.getSwitchPositions() - 1 : 0)));

                // Centred when missing.
                element.controlPosition = node.getProperty("controlPosition", 0.5);

                // Missing means no cabinet.
                element.cabEnabled = node.getProperty("cab", false);
                element.cabFile = node.getProperty("cabFile", "").toString();

                // "log" is the older boolean property.
                const int taper = node.getProperty("taper", node.getProperty("log", false) ? 1 : 0);
                element.taper = static_cast<Taper>(juce::jlimit(0, numTapers - 1, taper));

                // Missing means auto-scale.
                element.scopeAutoScale = node.getProperty("scopeAuto", true);
                element.scopeMin = node.getProperty("scopeMin", -5.0);
                element.scopeMax = node.getProperty("scopeMax", 5.0);

                // Clamped: the time base divides into the column count.
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

        // Ids are repaired after reading everything, since a duplicate can only be found once
        // all items are known.
        nextElementId = healIds(elements);
        nextWireId = healIds(wires);

        // One message for a sheet without model ids, not one per part.
        if (elementsWithoutModelId > 0)
            loadWarnings.add("This sheet predates model ids, so "
                                 + juce::String(elementsWithoutModelId)
                                 + (elementsWithoutModelId == 1 ? " part has" : " parts have")
                                 + " fallen back to the first model in the list. Check them"
                                   " before trusting how it sounds.");
    }
} // namespace SchematicModel
