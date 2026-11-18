#include "KiCadFootprints.h"

namespace Interchange
{
    using namespace SchematicModel;

    namespace
    {
        // Every id here is checked against KiCad's own library by "Every footprint the export
        // names is in KiCad's library". Sizes follow AionFX's build documents and Musikding's
        // parts: film capacitors 5 mm apart for every value (AionFX lays out 1 nF to 470 nF
        // at 5 mm), the box widening with the value as MKS2/MKT parts do (Musikding's MKT
        // 33 nF and 100 nF: 7.2 x 2.5 mm); electrolytics as 25-35 V pedal parts (Musikding's
        // 47 uF/35 V: 5 mm across, 2 mm apart; 1000 uF/16 V: 10 mm, 5 mm apart).

        const juce::String pot16 = "Celine:Potentiometer_16mm_PinsOnly_P5.00mm";
        const juce::String pot16Dual = "Celine:Potentiometer_16mm_Dual_PinsOnly_P5.00mm";

        juce::String model(const Element& e) { return getModelId(e.type, e.modelIndex); }

        juce::String capacitor(const Element& e)
        {
            if (e.polarised)
            {
                const double uF = e.value * 1.0e6;
                return uF <= 47.0     ? "Capacitor_THT:CP_Radial_D5.0mm_P2.00mm"
                       : uF <= 100.0  ? "Capacitor_THT:CP_Radial_D6.3mm_P2.50mm"
                       : uF <= 220.0  ? "Capacitor_THT:CP_Radial_D8.0mm_P3.50mm"
                       : uF <= 1000.0 ? "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm"
                                      : "Capacitor_THT:CP_Radial_D12.5mm_P5.00mm";
            }

            // Below a nanofarad a small ceramic, 5 mm apart like the films.
            if (e.value < 1.0e-9)
                return "Capacitor_THT:C_Disc_D4.3mm_W1.9mm_P5.00mm";

            const auto box = e.value <= 1.0e-7 ? "2.5" : e.value <= 2.2e-7 ? "3.5" : e.value <= 4.7e-7 ? "4.5" : e.value <= 1.0e-6 ? "5.5" : "7.2";
            return juce::String("Capacitor_THT:C_Rect_L7.2mm_W") + box + "mm_P5.00mm_FKS2_FKP2_MKS2_MKP2";
        }

        juce::String resistor()
        {
            // 1/4 W (or Musikding's 0.6 W) metal film, the DIN 0207 body, lying 10.16 mm apart:
            // every resistor in a pedal, whatever its value.
            return "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal";
        }

        juce::String transistor(const Element& e)
        {
            // TO-92, legs 2.54 mm apart; the parts that come in metal cans in theirs (the
            // BC107-109 and 2N2222A in TO-18, the germanium ones in TO-1, TO-5's 5.08 mm circle).
            const auto m = model(e);

            if (e.type == ElementType::Transistor && (m.startsWith("celine:bjt-bc10") || m == "celine:bjt-2n2222a"))
                return "Package_TO_SOT_THT:TO-18-3";

            if (e.type == ElementType::Transistor
                && (m == "celine:bjt-ac127" || m == "celine:bjt-ac128" || m == "celine:bjt-nte103" || m == "celine:bjt-nte102"))
                return "Package_TO_SOT_THT:TO-5-3";

            return "Package_TO_SOT_THT:TO-92_Inline";
        }
    } // namespace

    juce::String wirePads(int count)
    {
        if (count <= 6)
            return "Connector_Wire:SolderWire-0.5sqmm_1x" + juce::String(juce::jmax(1, count)).paddedLeft('0', 2)
                   + "_P4.6mm_D0.9mm_OD2.1mm";

        return "Connector_PinHeader_2.54mm:PinHeader_1x" + juce::String(count).paddedLeft('0', 2) + "_P2.54mm_Vertical";
    }

    juce::String dualPotFootprint() { return pot16Dual; }

    juce::String footprintFor(const Element& e)
    {
        switch (e.type)
        {
            case ElementType::Resistor: return resistor();
            case ElementType::Capacitor: return capacitor(e);
            case ElementType::Inductor: return "Inductor_THT:L_Radial_D10.0mm_P5.00mm_Fastron_07M";

            case ElementType::Diode:
            {
                if (getModelGroup(e.type, e.modelIndex) == "LED")
                    return "LED_THT:LED_D5.0mm";

                // Lying: DO-41 for the rectifiers, DO-35 for the rest.
                const auto m = model(e);
                return m == "celine:diode-1n4001" || m == "celine:diode-schottky" ? "Diode_THT:D_DO-41_SOD81_P10.16mm_Horizontal"
                                                                                  : "Diode_THT:D_DO-35_SOD27_P7.62mm_Horizontal";
            }

            case ElementType::Transistor:
            case ElementType::Jfet:
            case ElementType::Mosfet: return transistor(e);

            // Socketed, as AionFX and Musikding both do.
            case ElementType::OpAmp:
            case ElementType::PowerAmp: return "Package_DIP:DIP-8_W7.62mm_Socket";

            case ElementType::Potentiometer: return pot16;

            // Mounted on the enclosure and wired to the board.
            case ElementType::Switch:
            case ElementType::Spdt:
            case ElementType::Input:
            case ElementType::Output: return wirePads(e.getPinCount() + (e.type == ElementType::Input || e.type == ElementType::Output ? 1 : 0));

            case ElementType::Probe:
            case ElementType::Scope: return "TestPoint:TestPoint_THTPad_D1.5mm_Drill0.7mm";

            // Not a pedal board's parts.
            case ElementType::Triode:
            case ElementType::Pentode:
            case ElementType::VacuumDiode:
            case ElementType::Transformer:
            case ElementType::CenterTapTransformer:
            case ElementType::Speaker:

            // Not parts at all.
            case ElementType::Ground:
            case ElementType::VoltageSource:
            case ElementType::Node:
            case ElementType::Text:
            case ElementType::Rectangle:
                break;
        }

        return {};
    }

    //==========================================================================
    juce::String pinoutFor(const Element& e)
    {
        // From each maker's sheet (docs/specs), pad 1 first. TO-18 and TO-1 cans are E-B-C.
        const auto m = model(e);

        if (e.type == ElementType::Transistor)
            return m == "celine:bjt-2sc2240gr" || m == "celine:bjt-2sc2240bl" ? "ECB" : "EBC"; // Toshiba's TO-92: E, C, B

        if (e.type == ElementType::Jfet)
            return m == "celine:jfet-2n5952" ? "GSD" : "DSG"; // Fairchild's 2N5952; the J201 (Central) and onsemi's 2N54xx

        if (e.type == ElementType::Mosfet)
            return m == "celine:mosfet-bs250" ? "DGS" : "SGD"; // SGD: the 2N7000

        return {};
    }

    juce::StringArray pinRolesFor(ElementType type)
    {
        if (type == ElementType::Transistor)
            return {"B", "C", "E"};

        if (type == ElementType::Jfet || type == ElementType::Mosfet)
            return {"D", "G", "S"};

        return {};
    }

    //==========================================================================
    namespace
    {
        /** Pads in rows, with an outline on the silkscreen and the courtyard, in KiCad's
            format: `rows` rows of `perRow` pads `pitch` apart, numbered along each row. */
        juce::String padRows(const juce::String& name, const juce::String& description, int rows, int perRow, double pitch, double pad,
                             double drill)
        {
            const auto n = [](double v) { return juce::String(v, 3).trimCharactersAtEnd("0").trimCharactersAtEnd("."); };
            const double width = (perRow - 1) * pitch, height = (rows - 1) * pitch;
            const double silk = pad * 0.5 + 0.5, yard = pad * 0.5 + 0.75;

            juce::String text;
            text << "(footprint \"" << name << "\"\n"
                 << "\t(version 20241229)\n\t(generator \"celine\")\n\t(layer \"F.Cu\")\n"
                 << "\t(descr \"" << description << "\")\n"
                 << "\t(tags \"potentiometer 16mm\")\n"
                 << "\t(property \"Reference\" \"REF**\" (at " << n(width * 0.5) << " " << n(-silk - 1.5) << " 0) (layer \"F.SilkS\")"
                 << " (effects (font (size 1 1) (thickness 0.15))))\n"
                 << "\t(property \"Value\" \"" << name << "\" (at " << n(width * 0.5) << " " << n(height + silk + 1.5) << " 0) (layer \"F.Fab\")"
                 << " (effects (font (size 1 1) (thickness 0.15))))\n"
                 << "\t(attr through_hole)\n";

            for (const auto& [inset, layer, stroke] : {std::tuple {silk, "F.SilkS", 0.12}, std::tuple {yard, "F.CrtYd", 0.05}})
                text << "\t(fp_rect (start " << n(-inset) << " " << n(-inset) << ") (end " << n(width + inset) << " " << n(height + inset)
                     << ") (stroke (width " << n(stroke) << ") (type solid)) (fill no) (layer \"" << layer << "\"))\n";

            for (int r = 0; r < rows; ++r)
                for (int p = 0; p < perRow; ++p)
                {
                    const int number = r * perRow + p + 1;
                    text << "\t(pad \"" << number << "\" thru_hole " << (number == 1 ? "rect" : "circle") << " (at " << n(p * pitch) << " "
                         << n(r * pitch) << ") (size " << n(pad) << " " << n(pad) << ") (drill " << n(drill) << ") (layers \"*.Cu\" \"*.Mask\"))\n";
                }

            return text << ")\n";
        }
    } // namespace

    juce::String celineFootprint(const juce::String& id)
    {
        // KiCad's library has no 16 mm pot. Soldered by its three pins, 5 mm apart, with no
        // holes for the support tabs, as pedal boards have it.
        if (id == pot16)
            return padRows(id.fromFirstOccurrenceOf(":", false, false), "16 mm pot soldered by its three pins, no support lugs", 1, 3, 5.0, 2.2, 1.3);

        if (id == pot16Dual)
            return padRows(id.fromFirstOccurrenceOf(":", false, false),
                           "Dual 16 mm pot soldered by its pins, no support lugs; rows 5 mm apart, check against the pot", 2, 3, 5.0, 2.2, 1.3);

        return {};
    }
} // namespace Interchange
