#include "helpers/test_helpers.h"

#include <Schematic/ExampleSchematics.h>
#include <interchange/KiCadFootprints.h>
#include <interchange/KiCadSchematic.h>
#include <interchange/SExpression.h>

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>

// The KiCad export, held to KiCad itself: every sheet is written as a project, kicad-cli
// reads it back as a netlist, and that netlist must join exactly the pins Céline joins, on
// footprints that exist and have a pad for every pin. Without KiCad installed (KICAD_CLI
// names kicad-cli, KICAD_FOOTPRINTS the footprint folder) only the file's own checks run.

using namespace SchematicModel;
using namespace Interchange;

namespace
{
    juce::File kicadCli()
    {
        if (const auto env = juce::SystemStats::getEnvironmentVariable("KICAD_CLI", {}); env.isNotEmpty())
            return juce::File(env);

        for (const auto* path : {"/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli", "/usr/bin/kicad-cli",
                                 "/usr/local/bin/kicad-cli", "C:\\Program Files\\KiCad\\9.0\\bin\\kicad-cli.exe"})
            if (juce::File(path).existsAsFile())
                return juce::File(path);

        return {};
    }

    juce::File footprintFolder()
    {
        if (const auto env = juce::SystemStats::getEnvironmentVariable("KICAD_FOOTPRINTS", {}); env.isNotEmpty())
            return juce::File(env);

        for (const auto* path : {"/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints", "/usr/share/kicad/footprints"})
            if (juce::File(path).isDirectory())
                return juce::File(path);

        return {};
    }

    /** A footprint's pad names, or nothing if the library has no such footprint. Céline's own
        are read from Céline, KiCad's from KiCad's library. */
    std::optional<std::set<juce::String>> padsOf(const juce::String& id)
    {
        auto text = celineFootprint(id);

        if (text.isEmpty())
        {
            const auto file = footprintFolder()
                                  .getChildFile(id.upToFirstOccurrenceOf(":", false, false) + ".pretty")
                                  .getChildFile(id.fromFirstOccurrenceOf(":", false, false) + ".kicad_mod");

            if (! file.existsAsFile())
                return std::nullopt;

            text = file.loadFileAsString();
        }

        std::set<juce::String> pads;

        for (int at = text.indexOf("(pad \""); at >= 0; at = text.indexOf(at + 1, "(pad \""))
            pads.insert(text.substring(at + 6).upToFirstOccurrenceOf("\"", false, false));

        return pads;
    }

    juce::String run(const juce::StringArray& command, int& exitCode)
    {
        juce::ChildProcess process;

        if (! process.start(command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        {
            exitCode = -1;
            return {};
        }

        const auto output = process.readAllProcessOutput();
        process.waitForProcessToFinish(60000);
        exitCode = (int) process.getExitCode();
        return output;
    }

    struct Netlist
    {
        std::map<juce::String, juce::String> footprintOf;              // reference -> footprint
        std::map<std::pair<juce::String, juce::String>, juce::String> netOf; // (reference, pin) -> net
    };

    /** Exports the sheet, has kicad-cli read it back, and checks KiCad's own ERC. */
    std::optional<Netlist> exportAndRead(const Schematic& sheet, const juce::String& name, Interchange::KiCadProject& project,
                                         bool requireCleanErc = true)
    {
        project = Interchange::toKiCadProject(sheet, name);

        const auto cli = kicadCli();
        if (! cli.existsAsFile())
            return std::nullopt;

        const auto folder = juce::File::createTempFile("kicad-export");
        folder.createDirectory();
        juce::StringArray ignored;
        REQUIRE(Interchange::writeKiCadProject(sheet, folder, name, ignored).wasOk());
        const auto schematic = folder.getChildFile(name + ".kicad_sch");

        // Céline's own footprints must load in KiCad.
        if (const auto library = folder.getChildFile(Interchange::celineFootprintLibrary + ".pretty"); library.isDirectory())
        {
            int code = 0;
            const auto log = run({cli.getFullPathName(), "fp", "export", "svg", "-o", folder.getChildFile("svg").getFullPathName(),
                                  library.getFullPathName()},
                                 code);
            INFO(log);
            CHECK(code == 0);
            CHECK(folder.getChildFile("fp-lib-table").loadFileAsString().contains("${KIPRJMOD}/Celine.pretty"));
        }

        const auto xml = folder.getChildFile("netlist.xml");
        int code = 0;
        const auto log = run({cli.getFullPathName(), "sch", "export", "netlist", "--format", "kicadxml", "-o", xml.getFullPathName(),
                              schematic.getFullPathName()},
                             code);
        INFO(log);
        REQUIRE(code == 0);

        // ERC: no errors (warnings are KiCad's style advice).
        const auto report = folder.getChildFile("erc.rpt");
        const auto ercLog = run({cli.getFullPathName(), "sch", "erc", "--severity-error", "--exit-code-violations", "-o",
                                 report.getFullPathName(), schematic.getFullPathName()},
                                code);
        INFO(name << ": " << report.loadFileAsString());
        if (requireCleanErc)
            CHECK(code == 0);

        Netlist netlist;
        const auto doc = juce::XmlDocument::parse(xml);
        REQUIRE(doc != nullptr);

        if (const auto* components = doc->getChildByName("components"))
            for (const auto* comp : components->getChildIterator())
                netlist.footprintOf[comp->getStringAttribute("ref")] = comp->getChildElementAllSubText("footprint", {});

        if (const auto* nets = doc->getChildByName("nets"))
            for (const auto* net : nets->getChildIterator())
                for (const auto* node : net->getChildIterator())
                    netlist.netOf[{node->getStringAttribute("ref"), node->getStringAttribute("pin")}] = net->getStringAttribute("name");

        // KICAD_KEEP names a folder to keep each export in, to open it in KiCad.
        if (const auto keep = juce::SystemStats::getEnvironmentVariable("KICAD_KEEP", {}); keep.isNotEmpty())
            folder.copyDirectoryTo(juce::File(keep).getChildFile(name));

        folder.deleteRecursively();
        return netlist;
    }

    /** KiCad must join exactly the pins Céline joins, on footprints with a pad for each. */
    void checkAgainstCeline(const Schematic& sheet, const juce::String& name, bool requireCleanErc = true)
    {
        Interchange::KiCadProject project;
        const auto netlist = exportAndRead(sheet, name, project, requireCleanErc);

        INFO(name);
        CHECK(project.schematic.startsWith("(kicad_sch"));
        CHECK(Interchange::SExpr::parse(project.schematic).has_value());

        if (! netlist)
            return;

        const auto nets = sheet.extractNets();
        std::map<juce::String, std::set<juce::String>> kicadOfCeline;
        std::map<juce::String, std::set<juce::String>> celineOfKicad;

        for (const auto& pin : project.pins)
        {
            const auto celine = nets.netNames[(size_t) nets.netOfPin[(size_t) pin.element][(size_t) pin.pin]];
            const auto it = netlist->netOf.find({pin.reference, pin.number});
            INFO(pin.reference << "." << pin.number << " for element " << pin.element << " pin " << pin.pin);
            REQUIRE(it != netlist->netOf.end());

            kicadOfCeline[celine].insert(it->second);
            celineOfKicad[it->second].insert(celine);

            if (celine == "gnd")
                CHECK(it->second == "GND");
        }

        for (const auto& [celine, kicad] : kicadOfCeline)
        {
            INFO("Céline net " << celine << " became " << kicad.size() << " KiCad nets: " << juce::StringArray([&]
                                                                                                         {
                                                                                                             juce::StringArray a;
                                                                                                             for (const auto& k : kicad)
                                                                                                                 a.add(k);
                                                                                                             return a;
                                                                                                         }())
                                                                                                         .joinIntoString(", "));
            CHECK(kicad.size() == 1);
        }

        // A supply is one net per voltage: two ideal 9 V sources on ground are one +9V rail
        // on a board, however many nets they drive on the sheet.
        for (const auto& [kicad, celine] : celineOfKicad)
        {
            const auto bare = kicad.fromLastOccurrenceOf("/", false, false);
            const bool supply = bare == "GND" || ((bare.startsWith("+") || bare.startsWith("-")) && bare.endsWith("V"));

            INFO("KiCad net " << kicad << " joins " << celine.size() << " Céline nets");
            CHECK((celine.size() == 1 || supply));
        }

        // Every part with a footprint on one KiCad has, with a pad for every pin it uses (the
        // parts of no pedal board have none: "The footprints are a pedal board's").
        if (footprintFolder().isDirectory())
            for (const auto& [key, net] : netlist->netOf)
            {
                const auto& [reference, pin] = key;
                const auto found = netlist->footprintOf.find(reference);
                const auto footprint = found != netlist->footprintOf.end() ? found->second : juce::String();
                INFO(reference << " pin " << pin << " on " << footprint);

                if (footprint.isEmpty())
                    continue;

                const auto pads = padsOf(footprint);
                REQUIRE(pads.has_value());
                CHECK(pads->count(pin) == 1);
            }
    }

    Element& add(Schematic& sheet, ElementType type, int x, int y, int orientation = 0, bool mirrored = false)
    {
        auto& e = *sheet.findElement(sheet.addElement(type, x, y));
        e.orientation = orientation;
        e.mirrored = mirrored;
        e.value = getElementInfo(type).valueLabel[0] != 0 ? 1000.0 : 0.0;
        if (type == ElementType::Capacitor)
            e.value = 1.0e-7;
        if (type == ElementType::OpAmp || type == ElementType::PowerAmp)
            e.value = 9.0, e.valueB = 0.0;
        if (type == ElementType::Transformer || type == ElementType::CenterTapTransformer)
            e.value = 20.0, e.valueB = 1.0;
        if (type == ElementType::VoltageSource)
            e.value = 9.0;
        return e;
    }
} // namespace

TEST_CASE("The footprints are a pedal board's", "[kicad]")
{
    // Every part a pedal board holds has a footprint KiCad's library (or Céline's) has: every
    // type, every model, values across the range. Through-hole only.
    for (int t = 0; t < numElementTypes; ++t)
    {
        const auto type = static_cast<ElementType>(t);

        for (int m = 0; m < juce::jmax(1, getModelRecords(type).size()); ++m)
            for (const bool polarised : {false, true})
                for (const double value : {1.0, 1.0e-12, 1.0e-10, 4.7e-8, 2.2e-7, 4.7e-7, 1.0e-6, 2.2e-6, 4.7e-5, 1.0e-4, 2.2e-4, 1.0e-3, 2.2e-3})
                {
                    Element e;
                    e.type = type;
                    e.modelIndex = m;
                    e.polarised = polarised;
                    e.value = value;

                    const auto id = footprintFor(e);
                    const bool notOnAPedalBoard = type == ElementType::Triode || type == ElementType::Pentode || type == ElementType::VacuumDiode
                                                  || type == ElementType::Transformer || type == ElementType::CenterTapTransformer
                                                  || type == ElementType::Speaker;
                    const bool notAPart = type == ElementType::Ground || type == ElementType::VoltageSource || type == ElementType::Node
                                          || type == ElementType::Text || type == ElementType::Rectangle;

                    INFO(getElementInfo(type).name << " " << m << " " << value << ": " << id);
                    CHECK(id.isEmpty() == (notOnAPedalBoard || notAPart));
                    CHECK(! id.containsIgnoreCase("SMD"));

                    if (id.isNotEmpty() && footprintFolder().isDirectory())
                        CHECK(padsOf(id).has_value());
                }
    }

    // The rules: pots by their three pins, op-amps socketed, film capacitors at 5 mm, a
    // pedal's electrolytics, parts lying on the board, transistors as packaged.
    Element pot;
    pot.type = ElementType::Potentiometer;
    CHECK(*padsOf(footprintFor(pot)) == std::set<juce::String> {"1", "2", "3"});
    CHECK(*padsOf(dualPotFootprint()) == std::set<juce::String> {"1", "2", "3", "4", "5", "6"});

    Element opAmp;
    opAmp.type = ElementType::OpAmp;
    CHECK(footprintFor(opAmp) == "Package_DIP:DIP-8_W7.62mm_Socket");

    Element film;
    film.type = ElementType::Capacitor;
    for (const double value : {1.0e-9, 1.0e-7, 2.2e-7, 4.7e-7, 1.0e-6, 2.2e-6})
    {
        film.value = value;
        CHECK(footprintFor(film).contains("_P5.00mm"));
    }
    film.value = 1.0e-7; // Musikding's MKT 100 nF: 7.2 x 2.5 mm
    CHECK(footprintFor(film) == "Capacitor_THT:C_Rect_L7.2mm_W2.5mm_P5.00mm_FKS2_FKP2_MKS2_MKP2");

    Element electrolytic;
    electrolytic.type = ElementType::Capacitor;
    electrolytic.polarised = true;
    electrolytic.value = 4.7e-5; // Musikding's 47 uF/35 V: 5 mm across, 2 mm apart
    CHECK(footprintFor(electrolytic) == "Capacitor_THT:CP_Radial_D5.0mm_P2.00mm");
    electrolytic.value = 1.0e-3; // and its 1000 uF/16 V: 10 mm, 5 mm apart
    CHECK(footprintFor(electrolytic) == "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm");

    Element resistor;
    resistor.type = ElementType::Resistor;
    CHECK(footprintFor(resistor).endsWith("_Horizontal"));

    Element bjt;
    bjt.type = ElementType::Transistor;
    bjt.modelIndex = findModelById(ElementType::Transistor, "celine:bjt-2n3904");
    CHECK(footprintFor(bjt) == "Package_TO_SOT_THT:TO-92_Inline");
    CHECK(pinoutFor(bjt) == "EBC");
    bjt.modelIndex = findModelById(ElementType::Transistor, "celine:bjt-bc109c");
    CHECK(footprintFor(bjt) == "Package_TO_SOT_THT:TO-18-3");
    bjt.modelIndex = findModelById(ElementType::Transistor, "celine:bjt-2sc2240gr");
    CHECK(pinoutFor(bjt) == "ECB");

    Element jfet;
    jfet.type = ElementType::Jfet;
    jfet.modelIndex = findModelById(ElementType::Jfet, "celine:jfet-2n5952");
    CHECK(pinoutFor(jfet) == "GSD");

    // Switches and power: pads to solder their wires to.
    for (const auto type : {ElementType::Switch, ElementType::Spdt})
    {
        Element e;
        e.type = type;
        CHECK(footprintFor(e).startsWith("Connector_Wire:"));
    }

    // Jacks: a real 6.35 mm PCB jack, its tip and sleeve the symbol's T and S.
    for (const auto type : {ElementType::Input, ElementType::Output})
    {
        Element e;
        e.type = type;
        CHECK(footprintFor(e) == "Connector_Audio:Jack_6.35mm_Neutrik_NMJ4HCD2_Horizontal");

        if (footprintFolder().isDirectory())
        {
            const auto pads = padsOf(footprintFor(e));
            REQUIRE(pads.has_value());
            CHECK(pads->count("T") == 1);
            CHECK(pads->count("S") == 1);
        }
    }

    for (int pins = 2; pins <= 8; ++pins)
        CHECK(padsOf(wirePads(pins)).has_value());
}

TEST_CASE("Every factory example exports to a KiCad netlist that joins what Céline joins", "[kicad]")
{
    const auto names = Examples::getNames();

    for (int i = 0; i < names.size(); ++i)
    {
        Schematic sheet;
        Examples::load(sheet, i);
        checkAgainstCeline(sheet, "example" + juce::String(i));
    }
}

TEST_CASE("Every part exports in every turn and mirror, connected and on its footprint", "[kicad]")
{
    // Each part alone, a wire out of every pin so each pin is its own net.
    for (int t = 0; t < numElementTypes; ++t)
    {
        const auto type = static_cast<ElementType>(t);

        if (type == ElementType::Text || type == ElementType::Rectangle || type == ElementType::Node || type == ElementType::Ground
            || type == ElementType::VoltageSource)
            continue;

        const int models = juce::jmax(1, getModelRecords(type).size());

        for (int m = 0; m < models; ++m)
        {
            Schematic sheet;
            add(sheet, ElementType::Ground, -40, -40);

            for (int o = 0; o < 8; ++o)
            {
                auto& e = add(sheet, type, (o % 4) * 30, (o / 4) * 30 + m * 0, o % 4, o >= 4);
                e.modelIndex = m;

                if (type == ElementType::Spdt)
                    e.switchAction = static_cast<SwitchAction>(o % 3);

                const auto copy = e;
                for (int p = 0; p < copy.getPinCount(); ++p)
                {
                    const auto pin = copy.getPinPosition(p);
                    const auto out = pin - juce::Point<int>(copy.x, copy.y);
                    const juce::Point<int> step {juce::jlimit(-1, 1, out.x), out.x == 0 ? juce::jlimit(-1, 1, out.y) : 0};
                    sheet.addWire(pin, pin + step * 3);
                }
            }

            // Every pin is left open here, which KiCad's ERC rightly reports.
            checkAgainstCeline(sheet, juce::String(getElementInfo(type).name).removeCharacters(" ()") + juce::String(m), false);
        }
    }
}

TEST_CASE("Ganged parts, dual packages, supplies and the power connector", "[kicad]")
{
    Schematic sheet;
    add(sheet, ElementType::Ground, 0, 40);

    // Two pots on one shaft, two switches on one lever.
    for (int k = 0; k < 2; ++k)
    {
        add(sheet, ElementType::Potentiometer, k * 12, 0).label = "Volume";
        auto& s = add(sheet, ElementType::Spdt, k * 12, 12);
        s.label = "Channel";
        s.switchAction = SwitchAction::OnOffOn;
    }

    // Three TL072 halves (one and a half packages), two 12AX7 halves, an LM308, a 9 V and a
    // 300 V supply.
    for (int k = 0; k < 3; ++k)
        add(sheet, ElementType::OpAmp, 40 + 14 * k, 0);

    auto& lm308 = add(sheet, ElementType::OpAmp, 40, 14);
    lm308.modelIndex = findModelById(ElementType::OpAmp, "celine:opamp-lm308");

    add(sheet, ElementType::Triode, 40, 30);
    add(sheet, ElementType::Triode, 52, 30);

    auto& supply = add(sheet, ElementType::VoltageSource, 80, 30);
    supply.value = 300.0;
    add(sheet, ElementType::Ground, 80, 34);

    Interchange::KiCadProject project;
    const auto netlist = exportAndRead(sheet, "ganged", project, false); // its pins are left open

    CHECK(project.schematic.contains("Device:R_Potentiometer_Dual_Separate"));
    CHECK(project.schematic.contains("Switch:SW_DPDT_x2"));
    CHECK(project.schematic.contains(dualPotFootprint()));
    CHECK(project.footprints.count("Potentiometer_16mm_Dual_PinsOnly_P5.00mm.kicad_mod") == 1);
    CHECK(project.schematic.contains("\"30pF\""));
    CHECK(project.schematic.contains("\"HTR_A\""));
    CHECK(project.schematic.contains("power:+300V"));

    if (! netlist)
        return;

    // Two packages of TL072 for three halves, one 12AX7 for two, one dual pot.
    CHECK(netlist->footprintOf.count("U1") == 1);
    std::set<juce::String> references;
    for (const auto& [reference, footprint] : netlist->footprintOf)
        references.insert(reference);

    int opAmps = 0, valves = 0, pots = 0;
    for (const auto& r : references)
    {
        opAmps += r.startsWith("U");
        valves += r.startsWith("V");
        pots += r.startsWith("RV");
    }

    CHECK(opAmps == 3); // two TL072 and the LM308
    CHECK(valves == 1);
    CHECK(pots == 1);

    // The op-amps and the valve heaters reach the power connector.
    bool nineVolts = false, heater = false;
    for (const auto& [key, net] : netlist->netOf)
    {
        nineVolts |= net == "/+9V" || net == "+9V";
        heater |= net.endsWith("HTR_A");
    }

    CHECK(nineVolts);
    CHECK(heater);
}

TEST_CASE("Real sheets export cleanly", "[kicad][.sheets]")
{
    // KICAD_SHEETS names a folder of .celsch files to put through the same checks.
    const auto folder = juce::File(juce::SystemStats::getEnvironmentVariable("KICAD_SHEETS", {}));

    if (! folder.isDirectory())
        SKIP("KICAD_SHEETS is not set");

    for (const auto& file : folder.findChildFiles(juce::File::findFiles, true, "*.celsch"))
    {
        const auto xml = juce::XmlDocument::parse(file);
        if (xml == nullptr)
            continue;

        const auto document = juce::ValueTree::fromXml(*xml);
        const auto tree = document.getChildWithName("SCHEMATIC");

        Schematic sheet;
        sheet.restoreFromValueTree(tree.isValid() ? tree : document);
        checkAgainstCeline(sheet, file.getFileNameWithoutExtension().retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-"));
    }
}
