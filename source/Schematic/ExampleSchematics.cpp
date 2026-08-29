#include "ExampleSchematics.h"

namespace SchematicModel::Examples
{
    namespace
    {
        /** Places an element and sets the fields worth setting, so the layouts
            below read as circuits rather than as struct filling. */
        struct Placer
        {
            Schematic& schematic;

            int at(ElementType type, int x, int y, int orientation = 0)
            {
                const int id = schematic.addElement(type, x, y);
                schematic.findElement(id)->orientation = orientation;
                return id;
            }

            int valued(ElementType type, int x, int y, double value, int orientation = 0, juce::String label = {})
            {
                const int id = at(type, x, y, orientation);
                auto* element = schematic.findElement(id);
                element->value = value;
                element->label = std::move(label);
                return id;
            }

            int model(ElementType type, int x, int y, int modelIndex, int orientation = 0)
            {
                const int id = at(type, x, y, orientation);
                schematic.findElement(id)->modelIndex = modelIndex;
                return id;
            }

            void wire(int x1, int y1, int x2, int y2) { schematic.addWire({x1, y1}, {x2, y2}); }
        };

        //======================================================================
        /** A shunt diode clipper with a volume control -- the smallest circuit
            that both distorts and has something to turn.

                in --[10k]--+--|>|--+-- gnd        (antiparallel pair)
                            |  |<|  |
                            +--[Volume 100k]-- gnd
                                    `--> out       (wiper)
        */
        void diodeClipper(Schematic& s)
        {
            Placer p{s};

            p.at(ElementType::Input, 0, 0);
            p.valued(ElementType::Resistor, 8, 0, 10000.0, 1); // horizontal, pins at 6 and 10
            p.wire(2, 0, 6, 0);

            // The signal rail the diodes and the pot all hang off.
            p.wire(10, 0, 26, 0);

            // Antiparallel pair: the second one turned round, so one clips each
            // half of the wave.
            p.model(ElementType::Diode, 14, 6, 0, 0);
            p.model(ElementType::Diode, 18, 6, 0, 2);
            p.wire(14, 0, 14, 4);
            p.wire(18, 0, 18, 4);
            p.wire(14, 8, 18, 8);
            p.wire(16, 8, 16, 10);
            p.at(ElementType::Ground, 16, 12);

            p.valued(ElementType::Potentiometer, 26, 6, 100000.0, 0, "Volume");
            p.wire(26, 0, 26, 4);
            p.wire(26, 8, 26, 10);
            p.at(ElementType::Ground, 26, 12);

            // Wiper out.
            p.wire(29, 6, 32, 6);
            p.at(ElementType::Output, 34, 6);
        }

        //======================================================================
        /** A guitar tone control: a volume pot with a capacitor from the wiper
            to ground, so turning it down also takes the top off. */
        void toneControl(Schematic& s)
        {
            Placer p{s};

            p.at(ElementType::Input, 0, 0);
            p.wire(2, 0, 8, 0);
            p.wire(8, 0, 8, 2);

            p.valued(ElementType::Potentiometer, 8, 4, 250000.0, 0, "Tone");
            p.wire(8, 6, 8, 8);
            p.at(ElementType::Ground, 8, 10);

            // Wiper to the output, with the treble-bleed capacitor tapped off it.
            p.wire(11, 4, 17, 4);
            p.at(ElementType::Output, 19, 4);

            p.valued(ElementType::Capacitor, 14, 8, 22.0e-9);
            p.wire(14, 4, 14, 6);
            p.wire(14, 10, 14, 12);
            p.at(ElementType::Ground, 14, 14);
        }

        //======================================================================
        /** A 12AX7 gain stage on a 300 V supply: plate load, cathode bias,
            coupling capacitors either side, grid leak to ground.

            Worth having as an example because it is the shape every valve preamp
            is built from, and because it exercises the part of the engine that
            has to find a bias point before it can run at all. */
        void triodeStage(Schematic& s)
        {
            Placer p{s};

            // Supply, standing well clear on the right -- the output terminal
            // and its label reach further than the pin geometry suggests.
            p.valued(ElementType::VoltageSource, 36, 8, 300.0);
            p.wire(36, 10, 36, 12);
            p.at(ElementType::Ground, 36, 14);
            p.wire(14, 2, 36, 2);
            p.wire(36, 2, 36, 6);

            // Plate load down onto the valve.
            p.valued(ElementType::Resistor, 14, 4, 100000.0);
            p.wire(14, 6, 14, 9);

            p.model(ElementType::Triode, 14, 12, 0);

            // Cathode bias resistor.
            p.valued(ElementType::Resistor, 14, 20, 1500.0);
            p.wire(14, 15, 14, 18);
            p.wire(14, 22, 14, 24);
            p.at(ElementType::Ground, 14, 26);

            // In through a coupling capacitor to the grid, with a grid leak.
            // The input terminal's pin and the capacitor's left pin land on the
            // same grid point, which connects them with no wire -- the same rule
            // that makes two touching pins a connection anywhere else.
            p.at(ElementType::Input, 0, 12);
            p.valued(ElementType::Capacitor, 4, 12, 100.0e-9, 1); // pins at 2 and 6
            p.wire(6, 12, 11, 12);

            p.valued(ElementType::Resistor, 11, 18, 1.0e6);
            p.wire(11, 12, 11, 16);
            p.wire(11, 20, 11, 22);
            p.at(ElementType::Ground, 11, 24);

            // Out through another, taken off the plate.
            p.valued(ElementType::Capacitor, 20, 9, 22.0e-9, 1); // pins at 18 and 22
            p.wire(14, 9, 18, 9);
            p.wire(22, 9, 24, 9);
            p.at(ElementType::Output, 26, 9);
        }
    } // namespace

    //==========================================================================

    juce::StringArray getNames()
    {
        return {"Diode clipper with volume", "Tone control", "12AX7 gain stage"};
    }

    void load(Schematic& schematic, int index)
    {
        schematic.clear();

        switch (index)
        {
            case 1: toneControl(schematic); break;
            case 2: triodeStage(schematic); break;
            default: diodeClipper(schematic); break;
        }
    }
} // namespace SchematicModel::Examples
