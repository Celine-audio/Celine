#include "KiCadSchematic.h"

#include "SExpression.h"
#include "KiCadFootprints.h"
#include "../ui/SchematicSymbols.h"
#include "../ui/SymbolArtwork.h"

#include <CelineUI/Theme.h>

#include <BinaryData.h>

#include <functional>
#include <map>
#include <numeric>
#include <set>

namespace Interchange
{
    using namespace SchematicModel;

    namespace
    {
        //======================================================================
        // Coordinates. Everything is placed in units of 1.27 mm (half a grid square), the
        // finest step KiCad's own symbols put a pin on, so every point is an integer and two
        // points meet exactly or not at all. A Céline grid square is two units.
        using P = juce::Point<int>;

        constexpr double unitMm = 1.27;
        constexpr double textSize = 1.27;

        P fromGrid(juce::Point<int> g) { return {2 * g.x, 2 * g.y}; }

        int toUnits(double mm) { return juce::roundToInt(mm / unitMm); }

        int manhattan(P a, P b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }

        struct PointLess
        {
            bool operator()(P a, P b) const noexcept { return a.x != b.x ? a.x < b.x : a.y < b.y; }
        };

        using PointSet = std::set<P, PointLess>;

        /** A placed symbol's turn: KiCad turns a symbol counter-clockwise on the screen and then
            mirrors it left to right (`mirror y`), its library drawn with y up. Found by asking
            kicad-cli which pins a label at each predicted point reaches. */
        struct Orientation
        {
            int angle = 0; // 0, 90, 180, 270
            bool mirror = false;

            /** A library point (y up) as an offset on the sheet (y down). */
            P apply(P lib) const noexcept
            {
                int x = lib.x, y = -lib.y;
                const int c = angle == 0 ? 1 : angle == 180 ? -1 : 0;
                const int s = angle == 90 ? 1 : angle == 270 ? -1 : 0;
                const int rx = x * c + y * s, ry = -x * s + y * c;
                return {mirror ? -rx : rx, ry};
            }
        };

        const Orientation allOrientations[] = {{0, false}, {90, false}, {180, false}, {270, false},
                                               {0, true}, {90, true}, {180, true}, {270, true}};

        juce::String newUuid() { return juce::Uuid().toDashedString(); }

        double mm(int units) { return units * unitMm; }

        SExpr uuid() { return SExpr::node("uuid", {SExpr::string(newUuid())}); }

        SExpr at(P p, int angle = 0)
        {
            return SExpr::node("at", {SExpr::number(mm(p.x)), SExpr::number(mm(p.y)), SExpr::number(angle)});
        }

        SExpr effects(bool hidden, const juce::String& justify = {})
        {
            auto e = SExpr::node("effects", {SExpr::node("font", {SExpr::node("size", {SExpr::number(textSize), SExpr::number(textSize)})})});

            if (justify.isNotEmpty())
            {
                auto j = SExpr::node("justify");
                for (const auto& word : juce::StringArray::fromTokens(justify, " ", ""))
                    j.add(SExpr::symbol(word));
                e.add(j);
            }

            if (hidden)
                e.add(SExpr::node("hide", {SExpr::symbol("yes")}));

            return e;
        }

        SExpr stroke(double width, const juce::String& type = "default")
        {
            return SExpr::node("stroke", {SExpr::node("width", {SExpr::number(width)}), SExpr::node("type", {SExpr::symbol(type)})});
        }

        SExpr fill(bool filled)
        {
            return SExpr::node("fill", {SExpr::node("type", {SExpr::symbol(filled ? "outline" : "none")})});
        }

        SExpr wire(P a, P b)
        {
            return SExpr::node("wire", {SExpr::node("pts", {SExpr::node("xy", {SExpr::number(mm(a.x)), SExpr::number(mm(a.y))}),
                                                            SExpr::node("xy", {SExpr::number(mm(b.x)), SExpr::number(mm(b.y))})}),
                                        stroke(0), uuid()});
        }

        //======================================================================
        // Symbols: KiCad's own, read from the copy Céline ships (assets/kicad), and the few
        // Céline draws itself.
        struct LibPin
        {
            juce::String number, name;
            int unit = 0; // 0: every unit
            P at;         // library frame, y up, in units
            int angle = 0;
        };

        struct LibSymbol
        {
            juce::String id; // "Device:R"
            SExpr expr;
            std::vector<LibPin> pins;
            int units = 1;
            bool power = false;
        };

        /** The pins of a symbol, from its unit sub-symbols ("R_1_1": unit 1, style 1). */
        void readPins(LibSymbol& symbol)
        {
            symbol.pins.clear();
            symbol.units = 1;
            symbol.power = symbol.expr.find("power") != nullptr;

            for (const auto* unit : symbol.expr.findAll("symbol"))
            {
                const auto tokens = juce::StringArray::fromTokens(unit->textAt(1), "_", "");
                const int number = tokens.size() >= 2 ? tokens[tokens.size() - 2].getIntValue() : 0;
                symbol.units = juce::jmax(symbol.units, number);

                for (const auto* pin : unit->findAll("pin"))
                {
                    LibPin p;
                    p.unit = number;

                    if (const auto* a = pin->find("at"))
                    {
                        p.at = {toUnits(a->numberAt(1)), toUnits(a->numberAt(2))};
                        p.angle = juce::roundToInt(a->numberAt(3));
                    }

                    if (const auto* n = pin->find("name"))
                        p.name = n->textAt(1);

                    if (const auto* n = pin->find("number"))
                        p.number = n->textAt(1);

                    symbol.pins.push_back(p);
                }
            }
        }

        /** Every symbol KiCad's library gives the export, parsed once. */
        const std::map<juce::String, LibSymbol>& standardSymbols()
        {
            static const auto symbols = []
            {
                std::map<juce::String, LibSymbol> result;

                int size = 0;
                const char* data = nullptr;

                for (int i = 0; i < BinaryData::namedResourceListSize && data == nullptr; ++i)
                    if (juce::String(BinaryData::getNamedResourceOriginalFilename(BinaryData::namedResourceList[i])) == "kicad-symbols.kicad_sym")
                        data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);

                if (data == nullptr)
                    return result;

                const auto parsed = SExpr::parse(juce::String::fromUTF8(data, size));

                if (! parsed)
                    return result;

                for (const auto* s : parsed->findAll("symbol"))
                {
                    LibSymbol symbol;
                    symbol.id = s->textAt(1);
                    symbol.expr = *s;
                    readPins(symbol);
                    result.emplace(symbol.id, std::move(symbol));
                }

                return result;
            }();

            return symbols;
        }

        /** Sets the text of the list `(head "...")` inside `e`, where it exists. */
        void setChildText(SExpr& e, const juce::String& head, size_t index, const juce::String& text)
        {
            for (auto& item : e.items)
                if (item.head() == head && index < item.items.size())
                    item.items[index].text = text;
        }

        void setProperty(SExpr& symbol, const juce::String& name, const juce::String& value)
        {
            for (auto& item : symbol.items)
                if (item.head() == "property" && item.textAt(1) == name && item.items.size() > 2)
                    item.items[2].text = value;
        }

        /** A standard symbol under another name with its pins renumbered by name, for a lead
            order KiCad's library does not have. */
        LibSymbol renumbered(const LibSymbol& base, const juce::String& id, const std::map<juce::String, juce::String>& numberOfName)
        {
            LibSymbol symbol = base;
            symbol.id = id;
            symbol.expr.items[1].text = id;

            for (auto& unit : symbol.expr.items)
                if (unit.head() == "symbol")
                    for (auto& pin : unit.items)
                        if (pin.head() == "pin")
                            if (const auto* name = pin.find("name"))
                                if (const auto it = numberOfName.find(name->textAt(1)); it != numberOfName.end())
                                    setChildText(pin, "number", 1, it->second);

            readPins(symbol);
            return symbol;
        }

        //======================================================================
        // Céline's own drawings, for the parts KiCad's library has no symbol with these pins for.
        struct DrawnPin
        {
            juce::String number;
            P at; // library frame, units
            int angle = 0;
            double length = 0.0; // mm
            juce::String type = "passive";
        };

        struct DrawnUnit
        {
            std::vector<SExpr> graphics;
            std::vector<DrawnPin> pins;
        };

        /** A part's artwork, unturned, as KiCad polylines in its library frame (y up). */
        std::vector<SExpr> artworkGraphics(const Element& element)
        {
            std::vector<SExpr> graphics;

            Element upright = element;
            upright.orientation = 0;
            upright.mirrored = false;

            const auto* art = Celine::getSymbolArtwork(upright);

            if (art == nullptr)
                return graphics;

            constexpr double square = kiCadMillimetresPerSquare;

            for (const auto& shape : art->shapes)
            {
                // KiCad fills solid only; a faint tint (a body's shading) is left out.
                if (shape.filled && shape.path.getBounds().getWidth() * shape.path.getBounds().getHeight() > 1.0f)
                    continue;

                const double width = 0.12 * square * static_cast<double>(shape.strokeScale);

                juce::PathFlatteningIterator it(shape.path, {}, 0.02f);
                std::vector<juce::Point<float>> points;

                auto flush = [&]()
                {
                    if (points.size() >= 2)
                    {
                        auto pts = SExpr::node("pts");
                        for (const auto& p : points)
                            pts.add(SExpr::node("xy", {SExpr::number(p.x * square), SExpr::number(-p.y * square)}));

                        graphics.push_back(SExpr::node("polyline", {pts, stroke(shape.filled ? 0.0 : width), fill(shape.filled)}));
                    }

                    points.clear();
                };

                while (it.next())
                {
                    if (points.empty() || points.back().getDistanceFrom({it.x1, it.y1}) > 1.0e-4f)
                    {
                        flush();
                        points.push_back({it.x1, it.y1});
                    }

                    points.push_back({it.x2, it.y2});

                    if (it.closesSubPath)
                        flush();
                }

                flush();
            }

            return graphics;
        }

        SExpr line(std::initializer_list<juce::Point<double>> points, double width = 0.254)
        {
            auto pts = SExpr::node("pts");
            for (const auto& p : points)
                pts.add(SExpr::node("xy", {SExpr::number(p.x), SExpr::number(p.y)}));
            return SExpr::node("polyline", {pts, stroke(width), fill(false)});
        }

        /** Céline's pin `index` of an unturned part, in a library frame (y up), in units. */
        P celinePinInLibrary(const Element& element, int index)
        {
            Element upright = element;
            upright.orientation = 0;
            upright.mirrored = false;
            upright.x = upright.y = 0;
            const auto g = upright.getPinPosition(index);
            return {2 * g.x, -2 * g.y};
        }

        /** A heater or filament: a loop with its pins below, `numbers` left to right. */
        DrawnUnit heaterUnit(const juce::StringArray& numbers)
        {
            DrawnUnit unit;
            const int count = numbers.size();
            const double half = (count - 1) * 2.54 * 0.5;

            unit.graphics.push_back(line({{-half, -2.54}, {-half, 0.0}, {0.0, 1.905}, {half, 0.0}, {half, -2.54}}));

            for (int i = 0; i < count; ++i)
                unit.pins.push_back({numbers[i], {toUnits(-half + i * 2.54), -4}, 90, 2.54, "passive"});

            if (count == 3)
                unit.graphics.push_back(line({{0.0, -2.54}, {0.0, 1.905}}));

            return unit;
        }

        LibSymbol drawnSymbol(const juce::String& id, const juce::String& reference, const std::vector<DrawnUnit>& units,
                              bool showPinNumbers)
        {
            const auto shortName = id.fromFirstOccurrenceOf(":", false, false);
            auto symbol = SExpr::node("symbol", {SExpr::string(id)});

            if (! showPinNumbers)
                symbol.add(SExpr::node("pin_numbers", {SExpr::node("hide", {SExpr::symbol("yes")})}));

            symbol.add(SExpr::node("pin_names", {SExpr::node("offset", {SExpr::number(0)}), SExpr::node("hide", {SExpr::symbol("yes")})}))
                .add(SExpr::node("exclude_from_sim", {SExpr::symbol("no")}))
                .add(SExpr::node("in_bom", {SExpr::symbol("yes")}))
                .add(SExpr::node("on_board", {SExpr::symbol("yes")}))
                .add(SExpr::node("property", {SExpr::string("Reference"), SExpr::string(reference), at({0, 0}), effects(false)}))
                .add(SExpr::node("property", {SExpr::string("Value"), SExpr::string(shortName), at({0, 0}), effects(false)}))
                .add(SExpr::node("property", {SExpr::string("Footprint"), SExpr::string(""), at({0, 0}), effects(true)}))
                .add(SExpr::node("property", {SExpr::string("Datasheet"), SExpr::string(""), at({0, 0}), effects(true)}));

            for (size_t u = 0; u < units.size(); ++u)
            {
                auto unit = SExpr::node("symbol", {SExpr::string(shortName + "_" + juce::String((int) u + 1) + "_1")});

                for (const auto& g : units[u].graphics)
                    unit.add(g);

                for (const auto& pin : units[u].pins)
                    unit.add(SExpr::node("pin", {SExpr::symbol(pin.type), SExpr::symbol("line"), at(pin.at, pin.angle),
                                                 SExpr::node("length", {SExpr::number(pin.length)}),
                                                 SExpr::node("name", {SExpr::string("~"), effects(false)}),
                                                 SExpr::node("number", {SExpr::string(pin.number), effects(false)})}));

                symbol.add(unit);
            }

            LibSymbol result;
            result.id = id;
            result.expr = symbol;
            readPins(result);
            return result;
        }

        //======================================================================
        // Values, as KiCad's own libraries write them: "10k", "100nF", "A100k".
        juce::String modelName(const Element& element)
        {
            if (! element.hasModelChoice())
                return {};

            if (element.unresolvedModelId.isNotEmpty())
                return element.unresolvedModelId;

            const auto choices = getModelChoices(element.type);
            return juce::isPositiveAndBelow(element.modelIndex, choices.size()) ? choices[element.modelIndex] : juce::String();
        }

        juce::String modelId(const Element& element) { return getModelId(element.type, element.modelIndex); }

        /** A valve's part number without the maker: "12AX7 JJ" is a 12AX7. */
        juce::String valveName(const Element& element)
        {
            return modelName(element).upToFirstOccurrenceOf(" ", false, false);
        }

        juce::String railName(double volts)
        {
            auto text = juce::String(std::abs(volts), 2);
            text = text.trimCharactersAtEnd("0").trimCharactersAtEnd(".");
            return (volts < 0.0 ? "-" : "+") + text + "V";
        }

        //======================================================================
        /** One unit of a placed symbol, and how Céline's pins map onto it. */
        struct Unit
        {
            int unit = 1;
            int element = -1;                    // index into the sheet's elements, or -1 for one Céline does not draw
            std::vector<juce::String> pinOfCeline; // KiCad pin number for each Céline pin ("" for none)

            // Pins Céline does not draw, and where they go: a supply's name, a label, or "NC".
            std::map<juce::String, juce::String> attach;

            // Placed:
            P origin;
            Orientation orientation;
        };

        /** One physical part: one reference, one footprint, one or more units. */
        struct Package
        {
            juce::String libId;
            juce::String prefix;
            juce::String value;
            juce::String footprint;
            std::vector<std::pair<juce::String, juce::String>> extraFields; // shown
            std::vector<Unit> units;
            bool inBom = true;
            juce::String reference;
            P sortKey;
        };

        //======================================================================
        class Exporter
        {
           public:
            Exporter(const Schematic& s, const juce::String& n)
                : sheet(s), name(n), elements(s.getElements())
            {
            }

            KiCadProject run()
            {
                nets = sheet.extractNets();
                resolveNets();
                collectCelinePoints();
                buildPackages();
                addSupplyParts();
                assignReferences();
                placeDrawnUnits();
                writeLabelsAndNotes();
                placeExtraUnits();
                writeWires();
                return finish();
            }

           private:
            const Schematic& sheet;
            juce::String name;
            const std::vector<Element>& elements;
            NetList nets;

            std::vector<int> canonicalNet;            // per extracted net
            std::map<int, juce::String> supplyNameOf; // canonical net -> "GND", "+9V"
            std::map<juce::String, double> railVolts; // "+9V" -> 9

            std::map<juce::String, LibSymbol> used;
            std::vector<Package> packages;

            // Every point Céline connects at, and the canonical net there.
            std::map<P, int, PointLess> celinePoints;
            PointSet occupied; // points taken by what has been placed so far
            std::vector<std::pair<P, P>> extraWires;
            std::vector<SExpr> items; // labels, power symbols, no-connects, texts
            std::vector<SExpr> symbolInstances;
            std::map<P, int, PointLess> pinEnds; // where KiCad pins end, for the junction dots
            std::set<juce::String> supplyNets; // what the power connector must bring
            KiCadProject result;
            juce::String rootUuid = newUuid();
            int nextPower = 1, nextFlag = 1;
            P drawingMin, drawingMax;
            bool anyDrawn = false;

            //==================================================================
            int netOfPin(size_t element, int pin) const
            {
                return canonicalNet[(size_t) nets.netOfPin[element][(size_t) pin]];
            }

            void resolveNets()
            {
                // Nets sharing a name are one (every Ground is "gnd").
                std::map<juce::String, int> byName;
                canonicalNet.resize(nets.netNames.size());

                for (size_t n = 0; n < nets.netNames.size(); ++n)
                    canonicalNet[n] = byName.emplace(nets.netNames[n], (int) n).first->second;

                // Supply potentials: ground is 0 V, and each voltage source with one side known
                // fixes the other. A source with neither side known floats, and is a connector.
                std::map<int, double> volts;

                if (const auto g = byName.find("gnd"); g != byName.end())
                    volts[g->second] = 0.0;

                for (bool changed = true; changed;)
                {
                    changed = false;

                    for (size_t e = 0; e < elements.size(); ++e)
                    {
                        if (elements[e].type != ElementType::VoltageSource)
                            continue;

                        const int plus = netOfPin(e, 0), minus = netOfPin(e, 1);
                        const auto p = volts.find(plus), m = volts.find(minus);

                        if (p == volts.end() && m != volts.end())
                            volts[plus] = m->second + elements[e].value, changed = true;
                        else if (m == volts.end() && p != volts.end())
                            volts[minus] = p->second - elements[e].value, changed = true;
                    }
                }

                for (const auto& [net, v] : volts)
                {
                    const auto supply = std::abs(v) < 1.0e-9 ? juce::String("GND") : railName(v);
                    supplyNameOf[net] = supply;

                    if (supply != "GND")
                        railVolts[supply] = v;
                }
            }

            juce::String supplyOf(int net) const
            {
                const auto it = supplyNameOf.find(net);
                return it == supplyNameOf.end() ? juce::String() : it->second;
            }

            void collectCelinePoints()
            {
                for (size_t e = 0; e < elements.size(); ++e)
                    for (int p = 0; p < elements[e].getPinCount(); ++p)
                        celinePoints[fromGrid(elements[e].getPinPosition(p))] = netOfPin(e, p);

                for (const auto& w : sheet.getWires())
                    for (const auto end : {w.a, w.b})
                        celinePoints.emplace(fromGrid(end), -1);

                for (const auto& [point, net] : celinePoints)
                    occupied.insert(point);
            }

            //==================================================================
            const LibSymbol& use(const juce::String& id)
            {
                if (const auto it = used.find(id); it != used.end())
                    return it->second;

                const auto& standard = standardSymbols();

                if (const auto it = standard.find(id); it != standard.end())
                    return used.emplace(id, it->second).first->second;

                jassertfalse; // every id named here must be in assets/kicad or drawn below
                return used.emplace(id, LibSymbol {id, SExpr::node("symbol", {SExpr::string(id)}), {}, 1, false}).first->second;
            }

            void addDrawn(LibSymbol symbol) { used.emplace(symbol.id, std::move(symbol)); }

            /** The supply symbol for a net name, made from VCC or VEE when the library has none. */
            juce::String powerSymbolFor(const juce::String& supply)
            {
                const auto id = "power:" + supply;

                if (used.count(id) > 0 || standardSymbols().count(id) > 0)
                {
                    use(id);
                    return id;
                }

                auto symbol = standardSymbols().at(supply.startsWith("-") ? "power:VEE" : "power:VCC");
                symbol.id = id;
                symbol.expr.items[1].text = id;
                setProperty(symbol.expr, "Value", supply);

                const auto oldName = supply.startsWith("-") ? juce::String("VEE") : juce::String("VCC");

                for (auto& unit : symbol.expr.items)
                    if (unit.head() == "symbol")
                    {
                        unit.items[1].text = unit.items[1].text.replace(oldName, supply);

                        for (auto& pin : unit.items)
                            if (pin.head() == "pin")
                                setChildText(pin, "name", 1, supply);
                    }

                readPins(symbol);
                addDrawn(symbol);
                return id;
            }

            //==================================================================
            // Which symbol each part is, and how its pins land.
            juce::String transistorSymbol(const Element& e)
            {
                const auto pinout = pinoutFor(e);
                const bool reverse = isReversePolarity(e.type, e.modelIndex);

                juce::String base;
                if (e.type == ElementType::Transistor)
                    base = juce::String("Transistor_BJT:Q_") + (modelId(e) == "celine:bjt-mpsa13" ? "NPN_Darlington" : reverse ? "PNP" : "NPN");
                else if (e.type == ElementType::Jfet)
                    base = juce::String("Device:Q_") + (reverse ? "PJFET" : "NJFET");
                else
                    base = juce::String("Transistor_FET:Q_") + (reverse ? "PMOS" : "NMOS");

                const auto id = base + "_" + pinout;

                if (standardSymbols().count(id) > 0)
                {
                    use(id);
                    return id;
                }

                // A lead order KiCad's library lacks: the first it has, renumbered by lead name.
                const auto& reference = standardSymbols().at(base + "_" + juce::String(e.type == ElementType::Transistor ? "EBC" : "DGS"));
                std::map<juce::String, juce::String> numbers;
                for (int i = 0; i < pinout.length(); ++i)
                    numbers[juce::String::charToString(pinout[i])] = juce::String(i + 1);

                const auto custom = "Celine:" + base.fromFirstOccurrenceOf(":", false, false) + "_" + pinout;
                addDrawn(renumbered(reference, custom, numbers));
                return custom;
            }

            /** Céline pin -> KiCad pin number, for a 3-lead semiconductor in its pinout. */
            std::vector<juce::String> leadNumbers(const Element& e)
            {
                const auto pinout = pinoutFor(e);
                std::vector<juce::String> numbers;

                for (const auto& role : pinRolesFor(e.type))
                    numbers.push_back(juce::String(pinout.indexOf(role) + 1));

                return numbers;
            }

            static bool isDualOpAmp(const juce::String& model)
            {
                return model == "celine:opamp-tl072" || model == "celine:opamp-jrc4558" || model == "celine:opamp-ne5532"
                       || model == "celine:opamp-ideal";
            }

            juce::String opAmpSymbol(const juce::String& model)
            {
                if (model == "celine:opamp-jrc4558")
                    return use("Amplifier_Operational:NJM4558").id;

                if (model == "celine:opamp-ne5532")
                    return use("Amplifier_Operational:NE5532").id;

                if (model == "celine:opamp-lm741")
                    return use("Amplifier_Operational:LM741").id;

                if (model == "celine:opamp-lm308")
                {
                    // The LM741's drawing and pins, with 1 and 8 the compensation pins.
                    if (used.count("Celine:LM308") == 0)
                    {
                        auto symbol = standardSymbols().at("Amplifier_Operational:LM741");
                        symbol.id = "Celine:LM308";
                        symbol.expr.items[1].text = symbol.id;
                        setProperty(symbol.expr, "Value", "LM308");

                        for (auto& unit : symbol.expr.items)
                            if (unit.head() == "symbol")
                            {
                                unit.items[1].text = unit.items[1].text.replace("LM741", "LM308");

                                // The LM741's pin 8 is not connected; the LM308's is the
                                // compensation's other end, drawn where the 741's offset null is.
                                unit.items.erase(std::remove_if(unit.items.begin(), unit.items.end(), [](const SExpr& pin)
                                                                {
                                                                    const auto* number = pin.find("number");
                                                                    return pin.head() == "pin" && number != nullptr && number->textAt(1) == "8";
                                                                }),
                                                 unit.items.end());

                                for (auto& pin : unit.items)
                                    if (pin.head() == "pin")
                                    {
                                        const auto* number = pin.find("number");

                                        if (number != nullptr && number->textAt(1) == "5")
                                            setChildText(pin, "number", 1, "8");

                                        if (number != nullptr && (number->textAt(1) == "1" || number->textAt(1) == "8"))
                                            setChildText(pin, "name", 1, "COMP");
                                    }
                            }

                        readPins(symbol);
                        addDrawn(symbol);
                    }

                    return "Celine:LM308";
                }

                return use("Amplifier_Operational:TL072").id;
            }

            //------------------------------------------------------------------
            /** Valves: Céline's drawing, two units for a dual triode or a full-wave
                rectifier, plus the heater. */
            juce::String valveSymbol(const Element& e)
            {
                const auto id = "Celine:" + valveName(e);

                if (used.count(id) > 0)
                    return id;

                std::vector<DrawnUnit> units;
                const auto drawing = artworkGraphics(e);

                const auto unitWith = [&](std::vector<juce::String> numbers, std::vector<std::pair<juce::String, int>> stacked = {})
                {
                    DrawnUnit unit;
                    unit.graphics = drawing;

                    for (int p = 0; p < e.getPinCount(); ++p)
                        unit.pins.push_back({numbers[(size_t) p], celinePinInLibrary(e, p), 0, 0.0});

                    // A second pin at the same point: strapped in the socket's wiring (an
                    // EL34's suppressor grid on its cathode).
                    for (const auto& [number, onPin] : stacked)
                        unit.pins.push_back({number, celinePinInLibrary(e, onPin), 0, 0.0});

                    return unit;
                };

                if (e.type == ElementType::Triode)
                {
                    // 12AX7, 12AT7, 12AU7 and 12AY7 share the noval pinout.
                    units.push_back(unitWith({"1", "2", "3"}));
                    units.push_back(unitWith({"6", "7", "8"}));
                    units.push_back(heaterUnit({"4", "5", "9"}));
                }
                else if (e.type == ElementType::Pentode)
                {
                    const auto model = modelId(e);

                    if (model == "celine:pentode-el84-jj")
                    {
                        units.push_back(unitWith({"7", "9", "2", "3"}));
                        units.push_back(heaterUnit({"4", "5"}));
                    }
                    else
                    {
                        // Octal power valves: plate 3, screen 4, grid 5, cathode 8. A true
                        // pentode's suppressor is pin 1, joined to the cathode as Céline's
                        // model has it; a beam tetrode's pin 1 is not connected.
                        const bool suppressorOnPin1 = model == "celine:pentode-el34-jj" || model == "celine:pentode-kt77-jj";
                        units.push_back(unitWith({"3", "4", "5", "8"}, suppressorOnPin1 ? std::vector<std::pair<juce::String, int>> {{"1", 3}}
                                                                                          : std::vector<std::pair<juce::String, int>> {}));
                        units.push_back(heaterUnit({"2", "7"}));
                    }
                }
                else // VacuumDiode: full-wave rectifiers, plates 4 and 6, filament or cathode 8 and 2.
                {
                    units.push_back(unitWith({"4", "8"}));
                    units.push_back(unitWith({"6", "8"}));
                    units.push_back(heaterUnit({"2"}));
                }

                addDrawn(drawnSymbol(id, "V", units, false));
                return id;
            }

            juce::String transformerSymbol(const Element& e)
            {
                const auto id = juce::String(e.type == ElementType::CenterTapTransformer ? "Celine:Transformer_CT" : "Celine:Transformer");

                if (used.count(id) == 0)
                {
                    DrawnUnit unit;
                    unit.graphics = artworkGraphics(e);

                    for (int p = 0; p < e.getPinCount(); ++p)
                        unit.pins.push_back({juce::String(p + 1), celinePinInLibrary(e, p), 0, 0.0});

                    addDrawn(drawnSymbol(id, "T", {unit}, true));
                }

                return id;
            }

            juce::String powerAmpSymbol(const Element& e)
            {
                const juce::String id = "Celine:LM386";

                if (used.count(id) == 0)
                {
                    // Céline's pins (3, 2, 5, 1, 8), and the supply, ground and bypass pins
                    // on the triangle's free edges, with leads drawn to them.
                    DrawnUnit unit;
                    unit.graphics = artworkGraphics(e);
                    const juce::StringArray numbers {"3", "2", "5", "1", "8"};

                    for (int p = 0; p < e.getPinCount(); ++p)
                        unit.pins.push_back({numbers[p], celinePinInLibrary(e, p), 0, 0.0});

                    unit.pins.push_back({"6", {2, 8}, 270, 2.54 * 3.0, "power_in"});
                    unit.pins.push_back({"4", {-2, -8}, 90, 2.54 * 1.7, "power_in"});
                    unit.pins.push_back({"7", {0, -8}, 90, 2.54 * 2.3, "passive"});

                    addDrawn(drawnSymbol(id, "U", {unit}, true));
                }

                return id;
            }

            //==================================================================
            juce::String valueOf(const Element& e) const
            {
                switch (e.type)
                {
                    case ElementType::Resistor: return Celine::SymbolPainter::formatValue(e.value, "");
                    case ElementType::Capacitor: return Celine::SymbolPainter::formatValue(e.value, "F");
                    case ElementType::Inductor: return Celine::SymbolPainter::formatValue(e.value, "H");
                    case ElementType::Potentiometer:
                        return getTaperCode(e.taper) + Celine::SymbolPainter::formatValue(e.value, "");
                    case ElementType::Switch: return "SPST";
                    case ElementType::Spdt:
                        return e.switchAction == SwitchAction::OnOn ? juce::String("SPDT ON-ON")
                               : e.switchAction == SwitchAction::OnOffOn ? juce::String("SPDT ON-OFF-ON")
                                                                          : juce::String("SP3T ON-ON-ON");
                    case ElementType::Transformer:
                    case ElementType::CenterTapTransformer:
                        return Celine::SymbolPainter::formatRatio(e.value, e.valueB) + " " + modelName(e);
                    case ElementType::Triode:
                    case ElementType::Pentode:
                    case ElementType::VacuumDiode: return valveName(e);
                    case ElementType::Speaker: return modelName(e) + " 8 ohm";
                    case ElementType::OpAmp:
                        return modelId(e) == "celine:opamp-ideal" ? juce::String("TL072") : modelName(e).upToFirstOccurrenceOf("/", false, false);
                    case ElementType::Ground:
                    case ElementType::Input:
                    case ElementType::Output:
                    case ElementType::VoltageSource:
                    case ElementType::Diode:
                    case ElementType::Transistor:
                    case ElementType::Jfet:
                    case ElementType::Mosfet:
                    case ElementType::PowerAmp:
                    case ElementType::Text:
                    case ElementType::Rectangle:
                    case ElementType::Node:
                    case ElementType::Scope:
                    case ElementType::Probe:
                        break;
                }

                return modelName(e).isNotEmpty() ? modelName(e) : juce::String(getElementInfo(e.type).name);
            }

            /** Reading order: a part further left first, then higher. */
            P keyOf(const Element& e) const { return {e.x, e.y}; }

            static bool readingOrder(const Element& a, const Element& b)
            {
                return a.x != b.x ? a.x < b.x : a.y < b.y;
            }

            Unit drawnUnit(int element, int unit, std::vector<juce::String> pinNumbers)
            {
                Unit u;
                u.element = element;
                u.unit = unit;
                u.pinOfCeline = std::move(pinNumbers);
                return u;
            }

            Package& newPackage(const juce::String& libId, const juce::String& prefix, const Element& e)
            {
                Package p;
                p.libId = libId;
                p.prefix = prefix;
                p.value = valueOf(e);
                p.footprint = footprintFor(e);
                p.sortKey = keyOf(e);
                packages.push_back(p);
                return packages.back();
            }

            void addControlField(Package& p, const Element& e)
            {
                if (e.label.isNotEmpty())
                    p.extraFields.push_back({"Control", e.label});
            }

            std::vector<int> elementsOf(std::function<bool(const Element&)> wanted) const
            {
                std::vector<int> found;

                for (size_t e = 0; e < elements.size(); ++e)
                    if (wanted(elements[e]))
                        found.push_back((int) e);

                std::stable_sort(found.begin(), found.end(), [&](int a, int b) { return readingOrder(elements[(size_t) a], elements[(size_t) b]); });
                return found;
            }

            //------------------------------------------------------------------
            void buildPackages()
            {
                std::set<int> done;

                for (size_t index = 0; index < elements.size(); ++index)
                {
                    const int i = (int) index;

                    if (done.count(i) > 0)
                        continue;

                    const auto& e = elements[index];

                    switch (e.type)
                    {
                        case ElementType::Resistor:
                            newPackage(use("Device:R").id, "R", e).units.push_back(drawnUnit(i, 1, {"1", "2"}));
                            break;

                        case ElementType::Capacitor:
                            newPackage(use(e.polarised ? "Device:C_Polarized" : "Device:C").id, "C", e).units.push_back(drawnUnit(i, 1, {"1", "2"}));
                            break;

                        case ElementType::Inductor:
                            newPackage(use("Device:L").id, "L", e).units.push_back(drawnUnit(i, 1, {"1", "2"}));
                            break;

                        case ElementType::Diode:
                        {
                            const auto group = getModelGroup(e.type, e.modelIndex);
                            const auto id = group == "LED" ? "Device:LED" : group == "Schottky" ? "Device:D_Schottky" : group == "Zener" ? "Device:D_Zener" : "Device:D";
                            // Céline's first pin is the anode; KiCad's 1 is the cathode.
                            newPackage(use(id).id, "D", e).units.push_back(drawnUnit(i, 1, {"2", "1"}));
                            break;
                        }

                        case ElementType::Transistor:
                        case ElementType::Jfet:
                        case ElementType::Mosfet:
                            newPackage(transistorSymbol(e), "Q", e).units.push_back(drawnUnit(i, 1, leadNumbers(e)));
                            break;

                        case ElementType::Potentiometer:
                        {
                            // Céline's first pin is where the wiper sits fully clockwise: a real
                            // pot's lug 3.
                            auto gang = gangOf(i, [](const Element& x) { return x.type == ElementType::Potentiometer; });

                            if (gang.size() == 2)
                            {
                                auto& p = newPackage(use("Device:R_Potentiometer_Dual_Separate").id, "RV", e);
                                p.footprint = dualPotFootprint();
                                p.value = valueOf(e) + " / " + valueOf(elements[(size_t) gang[1]]);
                                p.units.push_back(drawnUnit(gang[0], 1, {"3", "2", "1"}));
                                p.units.push_back(drawnUnit(gang[1], 2, {"6", "5", "4"}));
                                addControlField(p, e);
                            }
                            else
                            {
                                if (gang.size() > 2)
                                    result.warnings.add("\"" + e.label + "\" turns " + juce::String((int) gang.size())
                                                     + " pots on one shaft; KiCad's library has no such pot, so each is a part of its own.");

                                for (const int g : gang)
                                {
                                    auto& p = newPackage(use("Device:R_Potentiometer").id, "RV", elements[(size_t) g]);
                                    p.units.push_back(drawnUnit(g, 1, {"3", "2", "1"}));
                                    addControlField(p, elements[(size_t) g]);
                                }
                            }

                            done.insert(gang.begin(), gang.end());
                            break;
                        }

                        case ElementType::Switch:
                        case ElementType::Spdt:
                            addSwitches(i, done);
                            break;

                        case ElementType::OpAmp:
                        {
                            const auto model = modelId(e);

                            if (model == "celine:opamp-ideal")
                                result.warnings.add("An ideal op-amp has no part number: exported as a TL072.");

                            if (! isDualOpAmp(model))
                            {
                                auto& p = newPackage(opAmpSymbol(model), "U", e);
                                auto unit = drawnUnit(i, 1, {"3", "2", "6"});
                                unit.attach["7"] = railName(e.value);
                                unit.attach["4"] = juce::exactlyEqual(e.valueB, 0.0) ? juce::String("GND") : railName(-std::abs(e.valueB));

                                if (model == "celine:opamp-lm308")
                                {
                                    unit.attach["1"] = "COMP_A";
                                    unit.attach["8"] = "COMP_B";
                                }
                                else
                                {
                                    unit.attach["1"] = "NC";
                                    unit.attach["5"] = "NC";
                                }

                                p.units.push_back(unit);
                                noteSupply(unit.attach["7"]);
                                noteSupply(unit.attach["4"]);
                                break;
                            }

                            // Two amplifiers to a package, in reading order, among op-amps of the
                            // same model on the same supplies.
                            auto same = elementsOf([&](const Element& x)
                                                   { return x.type == ElementType::OpAmp && modelId(x) == model && juce::exactlyEqual(x.value, e.value) && juce::exactlyEqual(x.valueB, e.valueB); });
                            same.erase(std::remove_if(same.begin(), same.end(), [&](int x) { return done.count(x) > 0; }), same.end());

                            for (size_t k = 0; k < same.size(); k += 2)
                            {
                                const auto& first = elements[(size_t) same[k]];
                                auto& p = newPackage(opAmpSymbol(model), "U", first);
                                p.value = model == "celine:opamp-jrc4558" ? juce::String("JRC4558") : model == "celine:opamp-ne5532" ? juce::String("NE5532") : juce::String("TL072");
                                p.units.push_back(drawnUnit(same[k], 1, {"3", "2", "1"}));

                                if (k + 1 < same.size())
                                    p.units.push_back(drawnUnit(same[k + 1], 2, {"5", "6", "7"}));
                                else
                                {
                                    Unit spare;
                                    spare.unit = 2;
                                    spare.attach = {{"5", "NC"}, {"6", "NC"}, {"7", "NC"}};
                                    p.units.push_back(spare);
                                }

                                Unit power;
                                power.unit = 3;
                                power.attach["8"] = railName(first.value);
                                power.attach["4"] = juce::exactlyEqual(first.valueB, 0.0) ? juce::String("GND") : railName(-std::abs(first.valueB));
                                noteSupply(power.attach["8"]);
                                noteSupply(power.attach["4"]);
                                p.units.push_back(power);
                            }

                            done.insert(same.begin(), same.end());
                            break;
                        }

                        case ElementType::PowerAmp:
                        {
                            auto& p = newPackage(powerAmpSymbol(e), "U", e);
                            auto unit = drawnUnit(i, 1, {"3", "2", "5", "1", "8"});
                            unit.attach["6"] = railName(e.value);
                            unit.attach["4"] = "GND";
                            unit.attach["7"] = "NC";
                            noteSupply(unit.attach["6"]);
                            p.units.push_back(unit);
                            break;
                        }

                        case ElementType::Triode:
                        case ElementType::VacuumDiode:
                        {
                            // Two halves to a bottle, in reading order; a rectifier's halves must
                            // share their cathode, as a full-wave rectifier's do.
                            const auto model = modelId(e);
                            const bool rectifier = e.type == ElementType::VacuumDiode;
                            const int cathode = rectifier ? netOfPin(index, 1) : -1;

                            auto same = elementsOf([&](const Element& x)
                                                   {
                                                       if (x.type != e.type || modelId(x) != model)
                                                           return false;

                                                       if (! rectifier)
                                                           return true;

                                                       const auto at = (size_t) (&x - elements.data());
                                                       return netOfPin(at, 1) == cathode;
                                                   });
                            same.erase(std::remove_if(same.begin(), same.end(), [&](int x) { return done.count(x) > 0; }), same.end());

                            const auto symbol = valveSymbol(e);

                            for (size_t k = 0; k < same.size(); k += 2)
                            {
                                auto& p = newPackage(symbol, "V", elements[(size_t) same[k]]);
                                const auto pins = rectifier ? std::vector<juce::String> {"4", "8"} : std::vector<juce::String> {"1", "2", "3"};
                                p.units.push_back(drawnUnit(same[k], 1, pins));

                                if (k + 1 < same.size())
                                    p.units.push_back(drawnUnit(same[k + 1], 2, rectifier ? std::vector<juce::String> {"6", "8"}
                                                                                         : std::vector<juce::String> {"6", "7", "8"}));
                                else
                                {
                                    Unit spare;
                                    spare.unit = 2;
                                    spare.attach = rectifier ? std::map<juce::String, juce::String> {{"6", "NC"}}
                                                             : std::map<juce::String, juce::String> {{"6", "NC"}, {"7", "NC"}, {"8", "NC"}};
                                    p.units.push_back(spare);
                                }

                                Unit heater;
                                heater.unit = 3;

                                if (rectifier)
                                    heater.attach["2"] = "RECT_FIL";
                                else
                                    heater.attach = {{"4", "HTR_B"}, {"5", "HTR_B"}, {"9", "HTR_A"}}; // 6.3 V: 4 and 5 together

                                for (const auto& [pin, net] : heater.attach)
                                    noteSupply(net);

                                p.units.push_back(heater);
                            }

                            done.insert(same.begin(), same.end());
                            break;
                        }

                        case ElementType::Pentode:
                        {
                            auto& p = newPackage(valveSymbol(e), "V", e);
                            const bool el84 = modelId(e) == "celine:pentode-el84-jj";
                            p.units.push_back(drawnUnit(i, 1, el84 ? std::vector<juce::String> {"7", "9", "2", "3"}
                                                                   : std::vector<juce::String> {"3", "4", "5", "8"}));
                            Unit heater;
                            heater.unit = 2;
                            heater.attach = el84 ? std::map<juce::String, juce::String> {{"4", "HTR_A"}, {"5", "HTR_B"}}
                                                 : std::map<juce::String, juce::String> {{"2", "HTR_A"}, {"7", "HTR_B"}};
                            noteSupply("HTR_A");
                            noteSupply("HTR_B");
                            p.units.push_back(heater);
                            break;
                        }

                        case ElementType::Transformer:
                        case ElementType::CenterTapTransformer:
                        {
                            std::vector<juce::String> numbers;
                            for (int pin = 0; pin < e.getPinCount(); ++pin)
                                numbers.push_back(juce::String(pin + 1));

                            newPackage(transformerSymbol(e), "T", e).units.push_back(drawnUnit(i, 1, numbers));
                            break;
                        }

                        case ElementType::Speaker:
                            newPackage(use("Device:Speaker").id, "LS", e).units.push_back(drawnUnit(i, 1, {"1", "2"}));
                            break;

                        case ElementType::Input:
                        case ElementType::Output:
                        {
                            // One jack per terminal kind; any further terminal of the kind is
                            // a label on the same net.
                            const auto first = elementsOf([&](const Element& x) { return x.type == e.type; });

                            if (first.front() != i)
                                break;

                            const bool neutrik = footprintFor(e).startsWith("Connector_Audio:");
                            auto& p = newPackage(use(neutrik ? "Connector_Audio:AudioJack2" : "Connector_Generic:Conn_01x02").id, "J", e);
                            p.value = e.type == ElementType::Input ? "Input" : "Output";
                            auto unit = drawnUnit(i, 1, {neutrik ? "T" : "1"});
                            unit.attach[neutrik ? "S" : "2"] = "GND";
                            p.units.push_back(unit);
                            break;
                        }

                        case ElementType::Probe:
                        case ElementType::Scope:
                        {
                            for (int pin = 0; pin < e.getPinCount(); ++pin)
                            {
                                if (supplyOf(netOfPin(index, pin)) == "GND")
                                    continue;

                                auto& p = newPackage(use("Connector:TestPoint").id, "TP", e);
                                p.value = e.label.isNotEmpty() ? e.label : juce::String("TP");
                                auto unit = drawnUnit(i, 1, {});
                                unit.pinOfCeline.assign((size_t) e.getPinCount(), {});
                                unit.pinOfCeline[(size_t) pin] = "1";
                                p.units.push_back(unit);
                            }

                            break;
                        }

                        case ElementType::VoltageSource:
                        {
                            const auto plus = supplyOf(netOfPin(index, 0)), minus = supplyOf(netOfPin(index, 1));

                            if (plus.isNotEmpty() && minus.isNotEmpty())
                            {
                                noteSupply(plus);
                                noteSupply(minus);
                                break; // two supply symbols, placed with the labels
                            }

                            auto& p = newPackage(use("Connector_Generic:Conn_01x02").id, "J", e);
                            p.value = Celine::SymbolPainter::formatValue(e.value, "V") + " supply";
                            p.footprint = "Connector_Wire:SolderWire-0.5sqmm_1x02_P4.6mm_D0.9mm_OD2.1mm";
                            p.units.push_back(drawnUnit(i, 1, {"1", "2"}));
                            result.warnings.add("A " + p.value + " is not tied to ground: exported as a two-pin connector.");
                            break;
                        }

                        case ElementType::Ground:
                        case ElementType::Node:
                        case ElementType::Text:
                        case ElementType::Rectangle:
                            break;
                    }
                }
            }

            /** The parts on one control as this one: those of a kind sharing its label, in
                reading order. An unlabelled part is its own control. */
            std::vector<int> gangOf(int i, std::function<bool(const Element&)> kind) const
            {
                const auto& e = elements[(size_t) i];

                if (e.label.isEmpty())
                    return {i};

                return elementsOf([&](const Element& x) { return kind(x) && x.label == e.label; });
            }

            void addSwitches(int i, std::set<int>& done)
            {
                const auto gang = gangOf(i, [](const Element& x) { return isSwitch(x.type); });
                done.insert(gang.begin(), gang.end());

                const auto& e = elements[(size_t) i];
                int throws = 1;
                for (const int g : gang)
                    throws = juce::jmax(throws, elements[(size_t) g].type == ElementType::Spdt ? elements[(size_t) g].getSwitchThrows() : 1);

                // Céline pins: the common then the throws, A first. KiCad's SPDT: 2 common, 1 and
                // 3 the throws; its SP3T: 3 common, 1, 2 and 4 the throws.
                const auto pinsFor = [&](const Element& x, int pole, bool threeThrow)
                {
                    const int base = pole * (threeThrow ? 4 : 3);
                    const auto n = [&](int k) { return juce::String(base + k); };

                    if (x.type == ElementType::Switch)
                        return threeThrow ? std::vector<juce::String> {n(3), n(1)} : std::vector<juce::String> {n(2), n(1)};

                    if (threeThrow)
                        return x.getSwitchThrows() == 3 ? std::vector<juce::String> {n(3), n(1), n(2), n(4)}
                                                        : std::vector<juce::String> {n(3), n(1), n(4)};

                    return std::vector<juce::String> {n(2), n(1), n(3)};
                };

                const auto unused = [&](const std::vector<juce::String>& usedPins, int pole, bool threeThrow)
                {
                    std::map<juce::String, juce::String> attach;
                    const int base = pole * (threeThrow ? 4 : 3);

                    for (int k = 1; k <= (threeThrow ? 4 : 3); ++k)
                        if (std::find(usedPins.begin(), usedPins.end(), juce::String(base + k)) == usedPins.end())
                            attach[juce::String(base + k)] = "NC";

                    return attach;
                };

                if (gang.size() == 1)
                {
                    if (e.type == ElementType::Switch)
                    {
                        auto& p = newPackage(use("Switch:SW_SPST").id, "SW", e);
                        p.units.push_back(drawnUnit(i, 1, {"1", "2"}));
                        addControlField(p, e);
                    }
                    else if (e.getSwitchThrows() == 3)
                    {
                        auto& p = newPackage(use("Switch:SW_SP3T").id, "SW", e);
                        p.units.push_back(drawnUnit(i, 1, {"3", "1", "2", "4"}));
                        addControlField(p, e);
                    }
                    else
                    {
                        auto& p = newPackage(use("Switch:SW_SPDT").id, "SW", e);
                        p.units.push_back(drawnUnit(i, 1, {"2", "1", "3"}));
                        addControlField(p, e);
                    }

                    return;
                }

                if (gang.size() == 2)
                {
                    const bool threeThrow = throws == 3;
                    auto& p = newPackage(use(threeThrow ? "Switch:SW_DP3T" : "Switch:SW_DPDT_x2").id, "SW", e);
                    p.footprint = wirePads(2 * (throws + 1));
                    p.value = (threeThrow ? "DP3T " : "DPDT ") + valueOf(e).fromFirstOccurrenceOf(" ", false, false);
                    if (e.type == ElementType::Switch)
                        p.value = threeThrow ? "DP3T" : "DPDT";
                    addControlField(p, e);

                    for (int pole = 0; pole < 2; ++pole)
                    {
                        const auto& x = elements[(size_t) gang[(size_t) pole]];
                        auto unit = drawnUnit(gang[(size_t) pole], pole + 1, pinsFor(x, pole, threeThrow));
                        unit.attach = unused(unit.pinOfCeline, pole, threeThrow);
                        p.units.push_back(unit);
                    }

                    return;
                }

                result.warnings.add("\"" + e.label + "\" throws " + juce::String((int) gang.size())
                                 + " switches at once; KiCad's library has no such switch, so each pole is a part of its own.");

                for (const int g : gang)
                {
                    const auto& x = elements[(size_t) g];
                    auto& p = newPackage(use(x.type == ElementType::Switch ? "Switch:SW_SPST" : x.getSwitchThrows() == 3 ? "Switch:SW_SP3T" : "Switch:SW_SPDT").id, "SW", x);
                    p.units.push_back(drawnUnit(g, 1, x.type == ElementType::Switch ? std::vector<juce::String> {"1", "2"}
                                                      : x.getSwitchThrows() == 3 ? std::vector<juce::String> {"3", "1", "2", "4"}
                                                                                 : std::vector<juce::String> {"2", "1", "3"}));
                    addControlField(p, x);
                }
            }

            void noteSupply(const juce::String& net)
            {
                if (net.isEmpty() || net == "NC")
                    return;

                supplyNets.insert(net);

                if (net != "GND" && (net.startsWith("+") || net.startsWith("-")) && railVolts.count(net) == 0)
                    railVolts[net] = net.dropLastCharacters(1).getDoubleValue();
            }

            //------------------------------------------------------------------
            /** What the board needs that the sheet does not draw: the LM308's compensation
                capacitor and the power connector. */
            void addSupplyParts()
            {
                for (size_t k = 0, count = packages.size(); k < count; ++k)
                    if (packages[k].libId == "Celine:LM308")
                    {
                        Package c;
                        c.libId = use("Device:C").id;
                        c.prefix = "C";
                        c.value = "30pF";
                        c.footprint = "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm";
                        c.sortKey = packages[k].sortKey + P(0, 1);
                        c.extraFields.push_back({"Note", "LM308 compensation"});

                        // Each LM308 gets its own pair of names, so the labels join the right pins.
                        const auto suffix = juce::String((int) k);
                        packages[k].units.front().attach["1"] = "COMP" + suffix + "_A";
                        packages[k].units.front().attach["8"] = "COMP" + suffix + "_B";

                        Unit u;
                        u.attach = {{"1", "COMP" + suffix + "_A"}, {"2", "COMP" + suffix + "_B"}};
                        c.units.push_back(u);
                        packages.push_back(c);
                    }

                // Every supply comes onto the board at pads to solder its wires to: ground,
                // then the rails from the highest, then anything else.
                bool rails = false;
                for (const auto& net : supplyNets)
                    rails |= net != "GND";

                if (! rails)
                    return;

                std::vector<juce::String> order {"GND"};
                std::vector<std::pair<double, juce::String>> sorted;
                for (const auto& net : supplyNets)
                    if (railVolts.count(net) > 0)
                        sorted.push_back({-railVolts.at(net), net});
                std::sort(sorted.begin(), sorted.end());
                for (const auto& [v, net] : sorted)
                    order.push_back(net);
                for (const auto& net : supplyNets)
                    if (net != "GND" && railVolts.count(net) == 0)
                        order.push_back(net);

                Package p;
                p.prefix = "J";
                p.sortKey = {std::numeric_limits<int>::max() / 2, 0};
                Unit u;

                const auto count = (int) order.size();
                p.libId = use("Connector_Generic:Conn_01x" + juce::String(count).paddedLeft('0', 2)).id;
                p.value = "Power";
                p.footprint = wirePads(count);

                for (int k = 0; k < count; ++k)
                    u.attach[juce::String(k + 1)] = order[(size_t) k];

                p.units.push_back(u);
                packages.push_back(p);
                powerConnector = (int) packages.size() - 1;
            }

            int powerConnector = -1;

            //------------------------------------------------------------------
            void assignReferences()
            {
                std::vector<size_t> order(packages.size());
                std::iota(order.begin(), order.end(), size_t {0});
                std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b)
                                 {
                                     const auto ka = packages[a].sortKey, kb = packages[b].sortKey;
                                     return ka.x != kb.x ? ka.x < kb.x : ka.y < kb.y;
                                 });

                std::map<juce::String, int> next;

                for (const auto k : order)
                    packages[k].reference = packages[k].prefix + juce::String(++next[packages[k].prefix]);
            }

            //==================================================================
            // Placement.
            std::vector<const LibPin*> pinsOfUnit(const LibSymbol& symbol, int unit) const
            {
                std::vector<const LibPin*> pins;
                for (const auto& p : symbol.pins)
                    if (p.unit == unit || p.unit == 0)
                        pins.push_back(&p);
                return pins;
            }

            static const LibPin* findPin(const std::vector<const LibPin*>& pins, const juce::String& number)
            {
                for (const auto* p : pins)
                    if (p->number == number)
                        return p;
                return nullptr;
            }

            /** The way a pin points out of its symbol, on the sheet. */
            static P outwardOf(const LibPin& pin, Orientation o)
            {
                // A pin's angle points from its end into the body.
                const P inward = pin.angle == 0 ? P(1, 0) : pin.angle == 90 ? P(0, 1) : pin.angle == 180 ? P(-1, 0) : P(0, -1);
                const auto v = o.apply(inward);
                return {-v.x, -v.y};
            }

            bool liesOnCelineWire(P p) const
            {
                for (const auto& w : sheet.getWires())
                {
                    const auto a = fromGrid(w.a), b = fromGrid(w.b);
                    if (a.x == b.x && p.x == a.x && p.y > juce::jmin(a.y, b.y) && p.y < juce::jmax(a.y, b.y))
                        return true;
                    if (a.y == b.y && p.y == a.y && p.x > juce::jmin(a.x, b.x) && p.x < juce::jmax(a.x, b.x))
                        return true;
                }
                return false;
            }

            struct Placement
            {
                P origin;
                Orientation orientation;
                double score = 1.0e18;
            };

            /** Where a unit drawn on the sheet goes: the turn, mirror and position that bring
                its pins nearest Céline's, every stub leaving its pin outwards, and none of its
                pins landing on anything else. */
            Placement placeOn(const Unit& u, const LibSymbol& symbol)
            {
                const auto& e = elements[(size_t) u.element];
                const auto pins = pinsOfUnit(symbol, u.unit);
                const P centre = fromGrid({e.x, e.y});

                std::vector<std::pair<const LibPin*, P>> mapped; // KiCad pin, Céline point
                PointSet own;

                for (size_t c = 0; c < u.pinOfCeline.size(); ++c)
                    if (u.pinOfCeline[c].isNotEmpty())
                        if (const auto* pin = findPin(pins, u.pinOfCeline[c]))
                        {
                            const auto q = fromGrid(e.getPinPosition((int) c));
                            mapped.push_back({pin, q});
                            own.insert(q);
                        }

                Placement best;

                for (const auto& o : allOrientations)
                {
                    std::vector<P> origins {centre};
                    for (const auto& [pin, q] : mapped)
                        origins.push_back(q - o.apply(pin->at));

                    for (const auto origin : origins)
                    {
                        double score = 0.3 * manhattan(origin, centre) + (o.mirror ? 0.05 : 0.0) + o.angle * 0.0001;

                        for (const auto& [pin, q] : mapped)
                        {
                            const auto end = origin + o.apply(pin->at);
                            const auto d = q - end;
                            score += std::abs(d.x) + std::abs(d.y);

                            if (d != P())
                            {
                                const auto out = outwardOf(*pin, o);
                                if (d.x * out.x + d.y * out.y < 0)
                                    score += 200.0; // the stub would run back through the body
                                if (d.x != 0 && d.y != 0)
                                    score += 1.0;
                            }
                        }

                        // No pin may land where something else connects.
                        for (const auto* pin : pins)
                        {
                            const auto end = origin + o.apply(pin->at);

                            if (own.count(end) > 0)
                                continue;

                            if (occupied.count(end) > 0)
                                score += 10000.0;
                            else if (liesOnCelineWire(end))
                                score += 5000.0;
                        }

                        if (score < best.score)
                            best = {origin, o, score};
                    }
                }

                return best;
            }

            void placeDrawnUnits()
            {
                for (auto& package : packages)
                    for (auto& u : package.units)
                    {
                        if (u.element < 0)
                            continue;

                        const auto& symbol = used.at(package.libId);
                        const auto placement = placeOn(u, symbol);
                        u.origin = placement.origin;
                        u.orientation = placement.orientation;

                        const auto& e = elements[(size_t) u.element];
                        const auto pins = pinsOfUnit(symbol, u.unit);

                        // Stubs from KiCad's pins to Céline's.
                        for (size_t c = 0; c < u.pinOfCeline.size(); ++c)
                        {
                            if (u.pinOfCeline[c].isEmpty())
                                continue;

                            const auto* pin = findPin(pins, u.pinOfCeline[c]);
                            if (pin == nullptr)
                                continue;

                            const auto end = u.origin + u.orientation.apply(pin->at);
                            const auto q = fromGrid(e.getPinPosition((int) c));
                            occupied.insert(end);
                            ++pinEnds[end];
                            result.pins.push_back({u.element, (int) c, package.reference, pin->number});

                            if (end == q)
                                continue;

                            if (end.x == q.x || end.y == q.y)
                            {
                                extraWires.push_back({end, q});
                                continue;
                            }

                            // An L: first along the pin, out of the body, then across.
                            const auto out = outwardOf(*pin, u.orientation);
                            auto corner = out.x != 0 ? P(q.x, end.y) : P(end.x, q.y);

                            if (occupied.count(corner) > 0)
                                corner = out.x != 0 ? P(end.x, q.y) : P(q.x, end.y);

                            occupied.insert(corner);
                            extraWires.push_back({end, corner});
                            extraWires.push_back({corner, q});
                        }

                        // Pins Céline does not draw: supplies, labels and no-connects.
                        for (const auto* pin : pins)
                        {
                            const auto it = u.attach.find(pin->number);
                            if (it != u.attach.end())
                            {
                                const auto end = u.origin + u.orientation.apply(pin->at);
                                ++pinEnds[end];
                                attach(end, outwardOf(*pin, u.orientation), it->second);
                            }
                        }

                        includeInDrawing(u.origin);
                    }
            }

            void includeInDrawing(P p)
            {
                if (! anyDrawn)
                    drawingMin = drawingMax = p;

                drawingMin = {juce::jmin(drawingMin.x, p.x), juce::jmin(drawingMin.y, p.y)};
                drawingMax = {juce::jmax(drawingMax.x, p.x), juce::jmax(drawingMax.y, p.y)};
                anyDrawn = true;
            }

            /** Joins a pin end to a supply, a label or nothing, a square or more out from the
                symbol if the end itself is taken. */
            void attach(P end, P out, const juce::String& net, bool asLabel = false)
            {
                if (net == "NC")
                {
                    items.push_back(SExpr::node("no_connect", {SExpr::node("at", {SExpr::number(mm(end.x)), SExpr::number(mm(end.y))}), uuid()}));
                    return;
                }

                P point = end;

                for (int step = 0; step <= 4; ++step)
                {
                    point = end + P(out.x * 2 * step, out.y * 2 * step);

                    bool clear = step == 0 ? true : occupied.count(point) == 0 && ! liesOnCelineWire(point);

                    if (step > 0)
                        for (int k = 1; k < 2 * step && clear; ++k)
                            clear = occupied.count(end + P(out.x * k, out.y * k)) == 0;

                    if (clear)
                        break;
                }

                if (point != end)
                    extraWires.push_back({end, point});

                occupied.insert(point);
                placeNet(point, out, net, asLabel);
            }

            /** A supply symbol, or a label, at a point, pointing away along `out`. */
            void placeNet(P point, P out, const juce::String& net, bool asLabel = false)
            {
                const bool supply = ! asLabel && (net == "GND" || ((net.startsWith("+") || net.startsWith("-")) && railVolts.count(net) > 0));

                if (! supply)
                {
                    const int angle = out.x > 0 ? 0 : out.x < 0 ? 180 : out.y < 0 ? 90 : 270;
                    items.push_back(SExpr::node("label", {SExpr::string(net), at(point, angle),
                                                          effects(false, angle == 180 || angle == 270 ? "right bottom" : "left bottom"), uuid()}));
                    return;
                }

                placePower(point, out, net == "GND" ? use("power:GND").id : powerSymbolFor(net), net);
            }

            void placePower(P point, P out, const juce::String& id, const juce::String& value, bool flag = false)
            {
                const auto& symbol = flag ? use("power:PWR_FLAG") : used.at(id);
                const auto& pin = symbol.pins.front();

                // Turned so its body lies along `out`.
                Orientation o;
                for (const int angle : {0, 90, 180, 270})
                {
                    const auto inward = Orientation {angle, false}.apply(pin.angle == 90 ? P(0, 1) : pin.angle == 270 ? P(0, -1) : pin.angle == 0 ? P(1, 0) : P(-1, 0));
                    if (inward == out)
                        o.angle = angle;
                }

                const auto origin = point - o.apply(pin.at);
                ++pinEnds[point];
                const auto reference = flag ? "#FLG" + juce::String(nextFlag++).paddedLeft('0', 2)
                                            : "#PWR" + juce::String(nextPower++).paddedLeft('0', 2);
                // Its name beyond it, upright: KiCad turns a field with its symbol, so a
                // quarter-turned one is given the turn back.
                const auto textPoint = point + P(out.x * (out.x != 0 ? 5 : 4), out.y * 4);
                const int textAngle = o.angle == 90 || o.angle == 270 ? 90 : 0;
                const auto justify = out.x < 0 ? juce::String("right") : out.x > 0 ? juce::String("left") : juce::String();

                auto instance = SExpr::node("symbol", {SExpr::node("lib_id", {SExpr::string(symbol.id)}), at(origin, o.angle)});
                instance.add(SExpr::node("unit", {SExpr::number(1)}))
                    .add(SExpr::node("exclude_from_sim", {SExpr::symbol("no")}))
                    .add(SExpr::node("in_bom", {SExpr::symbol("no")}))
                    .add(SExpr::node("on_board", {SExpr::symbol("yes")}))
                    .add(SExpr::node("dnp", {SExpr::symbol("no")}))
                    .add(uuid())
                    .add(SExpr::node("property", {SExpr::string("Reference"), SExpr::string(reference), at(point), effects(true)}))
                    .add(SExpr::node("property", {SExpr::string("Value"), SExpr::string(flag ? juce::String("PWR_FLAG") : value), at(textPoint, textAngle),
                                                  effects(flag || value == "GND", justify)}))
                    .add(SExpr::node("property", {SExpr::string("Footprint"), SExpr::string(""), at(point), effects(true)}))
                    .add(SExpr::node("property", {SExpr::string("Datasheet"), SExpr::string(""), at(point), effects(true)}))
                    .add(SExpr::node("pin", {SExpr::string(pin.number), uuid()}))
                    .add(instancesOf(reference, 1));

                symbolInstances.push_back(instance);
            }

            SExpr instancesOf(const juce::String& reference, int unit) const
            {
                return SExpr::node("instances", {SExpr::node("project", {SExpr::string(name),
                                                                         SExpr::node("path", {SExpr::string("/" + rootUuid),
                                                                                              SExpr::node("reference", {SExpr::string(reference)}),
                                                                                              SExpr::node("unit", {SExpr::number(unit)})})})});
            }

            //------------------------------------------------------------------
            /** The units Céline does not draw (supply units, heaters, spare halves, the
                compensation capacitors, the power connector), in a row under the drawing. */
            void placeExtraUnits()
            {
                // The drawing's extent, Céline's points included.
                for (const auto& [point, net] : celinePoints)
                    includeInDrawing(point);

                for (const auto& e : elements)
                    if (e.type == ElementType::Text || e.type == ElementType::Rectangle)
                    {
                        const auto box = e.type == ElementType::Rectangle ? e.getRectangleBounds()
                                                                          : juce::Rectangle<int>(e.x - e.getTextHalfWidth(), e.y - 1, 2 * e.getTextHalfWidth(), 2);
                        includeInDrawing(fromGrid(box.getTopLeft()));
                        includeInDrawing(fromGrid(box.getBottomRight()));
                    }

                if (! anyDrawn)
                    drawingMin = drawingMax = {};

                int x = drawingMin.x + 6;
                const int y = drawingMax.y + 16;

                for (auto& package : packages)
                    for (auto& u : package.units)
                    {
                        if (u.element >= 0)
                            continue;

                        const auto& symbol = used.at(package.libId);
                        const auto pins = pinsOfUnit(symbol, u.unit);

                        int minX = 0, maxX = 0;
                        for (const auto* pin : pins)
                        {
                            minX = juce::jmin(minX, pin->at.x);
                            maxX = juce::jmax(maxX, pin->at.x);
                        }

                        u.origin = {x - minX + 4, y};
                        u.orientation = {};

                        for (const auto* pin : pins)
                        {
                            const auto end = u.origin + pin->at.withY(-pin->at.y);
                            occupied.insert(end);
                            ++pinEnds[end];
                        }

                        for (const auto* pin : pins)
                        {
                            const auto end = u.origin + pin->at.withY(-pin->at.y);
                            const auto it = u.attach.find(pin->number);

                            // The power connector's pins are labelled, each label joined to its
                            // supply in the row of flags beside it: a column of supply symbols
                            // turned on their sides would print their names over each other.
                            attach(end, outwardOf(*pin, u.orientation), it != u.attach.end() ? it->second : juce::String("NC"),
                                   &package == &packages[(size_t) juce::jmax(0, powerConnector)] && powerConnector >= 0);
                        }

                        x = u.origin.x + maxX + 16;
                    }

                // Every supply comes from off the board (the power connector, or the jacks'
                // sleeves for ground alone): flag each one as driven, in a row beside them.
                std::set<juce::String> supplies;
                for (const auto& net : supplyNets)
                    if (net == "GND" || railVolts.count(net) > 0)
                        supplies.insert(net);
                if (used.count("power:GND") > 0)
                    supplies.insert("GND");

                for (const auto& net : supplies)
                {
                    const P point {x, y + 6};
                    placePower(point, {0, -1}, {}, {}, true);
                    placeNet(point, {0, 1}, net);

                    if (powerConnector >= 0)
                    {
                        extraWires.push_back({point, point + P(4, 0)});
                        placeNet(point + P(4, 0), {1, 0}, net, true);
                        x += 8;
                    }

                    x += 8;
                }

                extraRowBottom = y + 16;
                extraRowRight = x;
            }

            int extraRowBottom = 0, extraRowRight = 0;

            //==================================================================
            std::vector<std::pair<P, P>> segments;

            void writeWires()
            {

                // Céline joins anything lying along a wire; KiCad only joins at ends. So every
                // wire is cut wherever a point of Céline's lies inside it.
                for (const auto& w : sheet.getWires())
                {
                    const auto a = fromGrid(w.a), b = fromGrid(w.b);
                    std::vector<P> cuts {a, b};

                    for (const auto& [point, net] : celinePoints)
                    {
                        if (point == a || point == b)
                            continue;

                        const bool inside = a.y == b.y ? (point.y == a.y && point.x > juce::jmin(a.x, b.x) && point.x < juce::jmax(a.x, b.x))
                                                       : (point.x == a.x && point.y > juce::jmin(a.y, b.y) && point.y < juce::jmax(a.y, b.y));
                        if (inside)
                            cuts.push_back(point);
                    }

                    std::sort(cuts.begin(), cuts.end(), PointLess());

                    for (size_t i = 1; i < cuts.size(); ++i)
                        segments.push_back({cuts[i - 1], cuts[i]});
                }

                for (const auto& [a, b] : extraWires)
                    segments.push_back({a, b});

                // A dot wherever three or more wire ends and pins meet.
                auto ends = pinEnds;
                for (const auto& [a, b] : segments)
                {
                    ++ends[a];
                    ++ends[b];
                    wires.push_back(wire(a, b));
                }

                for (const auto& [point, count] : ends)
                    if (count >= 3)
                        wires.push_back(SExpr::node("junction", {SExpr::node("at", {SExpr::number(mm(point.x)), SExpr::number(mm(point.y))}),
                                                                 SExpr::node("diameter", {SExpr::number(0)}),
                                                                 SExpr::node("color", {SExpr::number(0), SExpr::number(0), SExpr::number(0), SExpr::number(0)}),
                                                                 uuid()}));
            }

            std::vector<SExpr> wires;

            void writeLabelsAndNotes()
            {
                // Node tags as net labels. Céline matches them ignoring case, KiCad does not, so
                // every spelling of one becomes its first.
                std::map<juce::String, juce::String> spelling;

                for (size_t index = 0; index < elements.size(); ++index)
                {
                    const auto& e = elements[index];
                    const auto pin = e.getPinCount() > 0 ? fromGrid(e.getPinPosition(0)) : P();

                    // Away from the part: Céline's body lies from the pin towards its centre.
                    const auto centre = fromGrid({e.x, e.y});
                    const P away {juce::jlimit(-1, 1, pin.x - centre.x), juce::jlimit(-1, 1, pin.y - centre.y)};
                    const P towardsBody {-away.x, -away.y};

                    switch (e.type)
                    {
                        case ElementType::Node:
                        {
                            const auto key = e.label.trim().toLowerCase();
                            if (key.isEmpty())
                                break;

                            const auto& text = spelling.emplace(key, e.label.trim()).first->second;
                            placeNet(pin, towardsBody, text);
                            break;
                        }

                        case ElementType::Ground:
                            placeNet(pin, towardsBody, "GND");
                            break;

                        case ElementType::Input:
                        case ElementType::Output:
                            placeNet(pin, away, e.type == ElementType::Input ? "IN" : "OUT");
                            break;

                        case ElementType::Probe:
                        case ElementType::Scope:
                            if (e.label.isNotEmpty() && e.type == ElementType::Probe && supplyOf(netOfPin(index, 0)).isEmpty())
                                placeNet(pin, away.x != 0 ? P(-away.y, away.x) : P(1, 0), e.label.trim().replaceCharacter(' ', '_'));
                            break;

                        case ElementType::VoltageSource:
                        {
                            const auto plus = supplyOf(netOfPin(index, 0)), minus = supplyOf(netOfPin(index, 1));

                            if (plus.isNotEmpty() && minus.isNotEmpty())
                                for (int p = 0; p < 2; ++p)
                                {
                                    const auto point = fromGrid(e.getPinPosition(p));
                                    const P outward {juce::jlimit(-1, 1, point.x - centre.x), juce::jlimit(-1, 1, point.y - centre.y)};
                                    placeNet(point, outward, p == 0 ? plus : minus);
                                }
                            break;
                        }

                        case ElementType::Text:
                            items.push_back(SExpr::node("text", {SExpr::string(e.label), at(fromGrid({e.x, e.y})), effects(false), uuid()}));
                            break;

                        case ElementType::Rectangle:
                        {
                            const auto box = e.getRectangleBounds();
                            const auto colour = Celine::Theme::groupBox(e.modelIndex);
                            const auto tl = fromGrid(box.getTopLeft()), br = fromGrid(box.getBottomRight());

                            items.push_back(SExpr::node("rectangle", {SExpr::node("start", {SExpr::number(mm(tl.x)), SExpr::number(mm(tl.y))}),
                                                                      SExpr::node("end", {SExpr::number(mm(br.x)), SExpr::number(mm(br.y))}),
                                                                      SExpr::node("stroke", {SExpr::node("width", {SExpr::number(0.254)}), SExpr::node("type", {SExpr::symbol("dash")}),
                                                                                             SExpr::node("color", {SExpr::number(colour.getRed()), SExpr::number(colour.getGreen()),
                                                                                                                   SExpr::number(colour.getBlue()), SExpr::number(1)})}),
                                                                      fill(false), uuid()}));

                            if (e.label.isNotEmpty())
                                items.push_back(SExpr::node("text", {SExpr::string(e.label), at(tl + P(1, -1)), effects(false, "left bottom"), uuid()}));
                            break;
                        }

                        case ElementType::Resistor:
                        case ElementType::Capacitor:
                        case ElementType::Inductor:
                        case ElementType::Potentiometer:
                        case ElementType::Switch:
                        case ElementType::Diode:
                        case ElementType::Transistor:
                        case ElementType::Triode:
                        case ElementType::OpAmp:
                        case ElementType::Transformer:
                        case ElementType::CenterTapTransformer:
                        case ElementType::Jfet:
                        case ElementType::VacuumDiode:
                        case ElementType::Pentode:
                        case ElementType::Spdt:
                        case ElementType::Mosfet:
                        case ElementType::PowerAmp:
                        case ElementType::Speaker:
                            break;
                    }
                }
            }

            //==================================================================
            std::vector<juce::Rectangle<int>> bodies, texts;

            /** The area a placed unit covers: its pins' extent, and Céline's. */
            juce::Rectangle<int> bodyOf(const Unit& u) const
            {
                P lo = u.origin, hi = u.origin;
                for (const auto* pin : pinsOfUnit(used.at(packageOf(u).libId), u.unit))
                {
                    const auto end = u.origin + u.orientation.apply(pin->at);
                    lo = {juce::jmin(lo.x, end.x), juce::jmin(lo.y, end.y)};
                    hi = {juce::jmax(hi.x, end.x), juce::jmax(hi.y, end.y)};
                }

                return juce::Rectangle<int>::leftTopRightBottom(lo.x, lo.y, hi.x, hi.y).expanded(1);
            }

            const Package& packageOf(const Unit& u) const
            {
                for (const auto& p : packages)
                    for (const auto& x : p.units)
                        if (&x == &u)
                            return p;

                jassertfalse;
                return packages.front();
            }

            /** Where a part's text goes: a block of lines, its top-left corner on the sheet. */
            juce::Rectangle<int> textBlock(const std::vector<std::pair<juce::String, juce::String>>& lines, P lo, P hi,
                                           juce::Rectangle<int> own) const
            {
                int chars = 0;
                for (const auto& [field, value] : lines)
                    chars = juce::jmax(chars, value.length());

                const int w = juce::jmax(2, (chars * 17 + 19) / 20), h = 2 * (int) lines.size();
                const int cx = (lo.x + hi.x) / 2, cy = (lo.y + hi.y) / 2;
                const bool tall = (hi.y - lo.y) >= (hi.x - lo.x);

                std::vector<juce::Rectangle<int>> candidates {{hi.x + 2, cy - h / 2, w, h},
                                                              {lo.x - 2 - w, cy - h / 2, w, h},
                                                              {cx - w / 2, lo.y - 2 - h, w, h},
                                                              {cx - w / 2, hi.y + 2, w, h}};
                if (! tall)
                    std::swap(candidates[0], candidates[2]), std::swap(candidates[1], candidates[3]);

                const auto overlap = [](juce::Rectangle<int> a, juce::Rectangle<int> b)
                {
                    const auto i = a.getIntersection(b);
                    return (double) i.getWidth() * i.getHeight();
                };

                double bestScore = 1.0e18;
                juce::Rectangle<int> best = candidates.front();

                for (size_t c = 0; c < candidates.size(); ++c)
                {
                    const auto r = candidates[c];
                    double score = 0.5 * (double) c;

                    for (const auto& b : bodies)
                        if (b != own)
                            score += 3.0 * overlap(r, b);

                    for (const auto& t : texts)
                        score += 3.0 * overlap(r, t.expanded(1, 0));

                    for (const auto& [a, b] : segments)
                    {
                        const auto line = juce::Rectangle<int>::leftTopRightBottom(juce::jmin(a.x, b.x), juce::jmin(a.y, b.y),
                                                                                   juce::jmax(a.x, b.x) + 1, juce::jmax(a.y, b.y) + 1);
                        if (line.intersects(r))
                            score += 4.0;
                    }

                    if (score < bestScore)
                        bestScore = score, best = r;
                }

                return best;
            }

            SExpr symbolInstance(const Package& package, const Unit& u)
            {
                const auto& symbol = used.at(package.libId);
                const auto pins = pinsOfUnit(symbol, u.unit);

                // The extent of the unit's pins, to put its text beside it.
                P lo = u.origin, hi = u.origin;
                for (const auto* pin : pins)
                {
                    const auto end = u.origin + u.orientation.apply(pin->at);
                    lo = {juce::jmin(lo.x, end.x), juce::jmin(lo.y, end.y)};
                    hi = {juce::jmax(hi.x, end.x), juce::jmax(hi.y, end.y)};
                }

                if (u.element >= 0)
                    for (int p = 0; p < elements[(size_t) u.element].getPinCount(); ++p)
                    {
                        const auto q = fromGrid(elements[(size_t) u.element].getPinPosition(p));
                        lo = {juce::jmin(lo.x, q.x), juce::jmin(lo.y, q.y)};
                        hi = {juce::jmax(hi.x, q.x), juce::jmax(hi.y, q.y)};
                    }

                std::vector<std::pair<juce::String, juce::String>> shown {{"Reference", package.reference}, {"Value", package.value}};
                for (const auto& f : package.extraFields)
                    shown.push_back(f);

                // Beside the part where it is clearest: right, left, above or below, whichever
                // covers the fewest other parts, texts and wires; right of a tall part and above a
                // wide one when there is a tie. Text is always upright: KiCad turns a field with
                // its symbol, so a quarter-turned one is given the turn back.
                const auto box = textBlock(shown, lo, hi, u.element >= 0 ? bodyOf(u) : juce::Rectangle<int>());
                texts.push_back(box);
                const int fieldAngle = u.orientation.angle == 90 || u.orientation.angle == 270 ? 90 : 0;

                auto instance = SExpr::node("symbol", {SExpr::node("lib_id", {SExpr::string(package.libId)}), at(u.origin, u.orientation.angle)});

                if (u.orientation.mirror)
                    instance.add(SExpr::node("mirror", {SExpr::symbol("y")}));

                instance.add(SExpr::node("unit", {SExpr::number(u.unit)}))
                    .add(SExpr::node("exclude_from_sim", {SExpr::symbol("no")}))
                    .add(SExpr::node("in_bom", {SExpr::symbol(package.inBom ? "yes" : "no")}))
                    .add(SExpr::node("on_board", {SExpr::symbol("yes")}))
                    .add(SExpr::node("dnp", {SExpr::symbol("no")}))
                    .add(uuid());

                // KiCad keeps a field readable by flipping its justification on a symbol turned
                // a quarter or a half round anticlockwise, and mirroring flips it again (read off
                // kicad-cli's drawings), so the text is anchored on the side it will run from.
                const bool flipped = (u.orientation.angle == 90 || u.orientation.angle == 180) != u.orientation.mirror;

                for (size_t k = 0; k < shown.size(); ++k)
                    instance.add(SExpr::node("property", {SExpr::string(shown[k].first), SExpr::string(shown[k].second),
                                                          at({flipped ? box.getRight() : box.getX(), box.getY() + 1 + 2 * (int) k}, fieldAngle),
                                                          effects(false, flipped ? "right" : "left")}));

                instance.add(SExpr::node("property", {SExpr::string("Footprint"), SExpr::string(package.footprint), at(u.origin), effects(true)}))
                    .add(SExpr::node("property", {SExpr::string("Datasheet"), SExpr::string(""), at(u.origin), effects(true)}));

                for (const auto* pin : pins)
                    instance.add(SExpr::node("pin", {SExpr::string(pin->number), uuid()}));

                instance.add(instancesOf(package.reference, u.unit));
                return instance;
            }

            KiCadProject finish()
            {
                for (const auto& package : packages)
                    for (const auto& u : package.units)
                        bodies.push_back(bodyOf(u));

                for (const auto& package : packages)
                    for (const auto& u : package.units)
                        symbolInstances.push_back(symbolInstance(package, u));

                // The page: the smallest ISO sheet the drawing and the row under it fit, with a
                // margin; everything shifted so it starts inside the frame.
                const int margin = 16;
                const int width = juce::jmax(drawingMax.x, extraRowRight) - drawingMin.x + 2 * margin;
                const int height = extraRowBottom - drawingMin.y + 2 * margin;

                juce::String paper = "User";
                for (const auto& [sheetName, w, h] : std::initializer_list<std::tuple<const char*, double, double>> {
                         {"A4", 297.0, 210.0}, {"A3", 420.0, 297.0}, {"A2", 594.0, 420.0}, {"A1", 841.0, 594.0}, {"A0", 1189.0, 841.0}})
                    if (mm(width) <= w && mm(height) <= h - 40.0) // the title block's room at the bottom
                    {
                        paper = sheetName;
                        break;
                    }

                const P shift = P(margin, margin) - drawingMin;

                auto root = SExpr::node("kicad_sch", {SExpr::node("version", {SExpr::symbol("20250114")}),
                                                       SExpr::node("generator", {SExpr::string("celine")}),
                                                       SExpr::node("generator_version", {SExpr::string("1.5")}),
                                                       SExpr::node("uuid", {SExpr::string(rootUuid)})});

                if (paper == "User")
                    root.add(SExpr::node("paper", {SExpr::string("User"), SExpr::number(mm(width)), SExpr::number(mm(height) + 40.0)}));
                else
                    root.add(SExpr::node("paper", {SExpr::string(paper)}));

                root.add(SExpr::node("title_block", {SExpr::node("title", {SExpr::string(name)}),
                                                     SExpr::node("date", {SExpr::string(juce::Time::getCurrentTime().formatted("%Y-%m-%d"))}),
                                                     SExpr::node("comment", {SExpr::number(1), SExpr::string(juce::String::fromUTF8("Exported from Céline"))})}));

                auto library = SExpr::node("lib_symbols");
                for (const auto& [id, symbol] : used)
                    library.add(symbol.expr);
                root.add(library);

                for (auto e : wires)
                    root.add(shifted(e, shift));
                for (auto e : items)
                    root.add(shifted(e, shift));
                for (auto e : symbolInstances)
                    root.add(shifted(e, shift));

                root.add(SExpr::node("sheet_instances", {SExpr::node("path", {SExpr::string("/"), SExpr::node("page", {SExpr::string("1")})})}));

                result.schematic = root.toString();

                juce::StringArray withoutFootprint;
                for (const auto& package : packages)
                    if (package.footprint.isEmpty())
                        withoutFootprint.add(package.reference);

                if (! withoutFootprint.isEmpty())
                    result.warnings.add(withoutFootprint.joinIntoString(", ") + " have no footprint: valves, transformers and speakers "
                                        "are not parts of a pedal board.");

                for (const auto& package : packages)
                    if (package.footprint.startsWith(celineFootprintLibrary + ":"))
                        result.footprints[package.footprint.fromFirstOccurrenceOf(":", false, false) + ".kicad_mod"] = celineFootprint(package.footprint);

                if (! result.footprints.empty())
                    result.footprintTable = "(fp_lib_table\n\t(version 7)\n\t(lib (name \"" + celineFootprintLibrary
                                            + "\") (type \"KiCad\") (uri \"${KIPRJMOD}/" + celineFootprintLibrary
                                            + ".pretty\") (options \"\") (descr \"Footprints from Céline\"))\n)\n";
                result.project = projectFile();
                return result;
            }

            /** Moves every position in an item by `shift` units (the whole drawing moves onto the page). */
            static SExpr shifted(SExpr e, P shift)
            {
                const auto move = [&](SExpr& item)
                {
                    if (item.items.size() >= 3)
                    {
                        item.items[1] = SExpr::number(item.numberAt(1) + mm(shift.x));
                        item.items[2] = SExpr::number(item.numberAt(2) + mm(shift.y));
                    }
                };

                std::function<void(SExpr&)> walk = [&](SExpr& item)
                {
                    const auto head = item.head();

                    if (head == "at" || head == "xy" || head == "start" || head == "end")
                    {
                        move(item);
                        return;
                    }

                    if (head == "lib_symbols")
                        return;

                    for (auto& child : item.items)
                        if (child.isList())
                            walk(child);
                };

                walk(e);
                return e;
            }

            juce::String projectFile() const
            {
                auto meta = std::make_unique<juce::DynamicObject>();
                meta->setProperty("filename", name + ".kicad_pro");
                meta->setProperty("version", 3);

                auto sheetEntry = juce::Array<juce::var> {juce::var(rootUuid), juce::var("Root")};

                auto project = std::make_unique<juce::DynamicObject>();
                project->setProperty("meta", juce::var(meta.release()));
                project->setProperty("sheets", juce::Array<juce::var> {juce::var(sheetEntry)});

                return juce::JSON::toString(juce::var(project.release()));
            }
        };
    } // namespace

    //==========================================================================
    KiCadProject toKiCadProject(const Schematic& sheet, const juce::String& name)
    {
        return Exporter(sheet, name).run();
    }

    juce::Result writeKiCadProject(const Schematic& sheet, const juce::File& folder, const juce::String& name,
                                   juce::StringArray& warnings)
    {
        if (! folder.createDirectory())
            return juce::Result::fail("Could not create " + folder.getFullPathName());

        const auto project = toKiCadProject(sheet, name);
        warnings = project.warnings;

        if (! folder.getChildFile(name + ".kicad_sch").replaceWithText(project.schematic, false, false, "\n"))
            return juce::Result::fail("Could not write " + name + ".kicad_sch");

        if (! folder.getChildFile(name + ".kicad_pro").replaceWithText(project.project, false, false, "\n"))
            return juce::Result::fail("Could not write " + name + ".kicad_pro");

        // Céline's own footprints, in a library beside the project that the project's own
        // footprint table names, so KiCad finds them wherever the folder is moved.
        if (! project.footprints.empty())
        {
            const auto library = folder.getChildFile(celineFootprintLibrary + ".pretty");

            if (! library.createDirectory())
                return juce::Result::fail("Could not create " + library.getFullPathName());

            for (const auto& [file, text] : project.footprints)
                if (! library.getChildFile(file).replaceWithText(text, false, false, "\n"))
                    return juce::Result::fail("Could not write " + file);

            if (! folder.getChildFile("fp-lib-table").replaceWithText(project.footprintTable, false, false, "\n"))
                return juce::Result::fail("Could not write fp-lib-table");
        }

        return juce::Result::ok();
    }
} // namespace Interchange
