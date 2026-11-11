#include "SchematicBuilder.h"

namespace SchematicModel
{
    namespace
    {
        Circuit::DiodeModel diodeModelFor(int index)
        {
            switch (index)
            {
                case 1: return Circuit::DiodeModel::germanium();
                case 2: return Circuit::DiodeModel::schottky();
                case 3: return Circuit::DiodeModel::redLed();
                case 4: return Circuit::DiodeModel::greenLed();
                case 5: return Circuit::DiodeModel::blueLed();
                case 6: return Circuit::DiodeModel::zener(9.1);
                case 7: return Circuit::DiodeModel::zener(5.1);

                // The 1N914 is the same part as the 1N4148.
                case 8: return Circuit::DiodeModel::d1n4148();
                case 9: return Circuit::DiodeModel::bat41();
                case 10: return Circuit::DiodeModel::bat46();
                case 11: return Circuit::DiodeModel::d1n4001();
                case 12: return Circuit::DiodeModel::zener(4.3);
                case 13: return Circuit::DiodeModel::zener(3.9);

                default: return Circuit::DiodeModel::d1n4148();
            }
        }

        Circuit::BjtModel transistorModelFor(int index)
        {
            switch (index)
            {
                case 1: return Circuit::BjtModel::pnpSilicon();
                case 2: return Circuit::BjtModel::npnBC109C();
                case 3: return Circuit::BjtModel::npn2N2222A();
                case 4: return Circuit::BjtModel::npnGermanium();
                case 5: return Circuit::BjtModel::pnpGermanium();
                case 6: return Circuit::BjtModel::npn2N5133();
                case 7: return Circuit::BjtModel::npnBC108A();
                case 8: return Circuit::BjtModel::npnBC108B();
                case 9: return Circuit::BjtModel::npn2N5088();
                case 10: return Circuit::BjtModel::npn2N5089();
                case 11: return Circuit::BjtModel::npnMPSA18();
                case 12: return Circuit::BjtModel::npnMPSA13();
                case 13: return Circuit::BjtModel::npnNTE103();
                case 14: return Circuit::BjtModel::pnpNTE102();
                case 15: return Circuit::BjtModel::npn2SC2240GR();
                case 16: return Circuit::BjtModel::npn2SC2240BL();
                default: return Circuit::BjtModel::npnSilicon();
            }
        }

        Circuit::TriodeModel triodeModelFor(int index)
        {
            switch (index)
            {
                case 1: return Circuit::TriodeModel::ecc81();
                case 2: return Circuit::TriodeModel::ecc12ay7();
                case 3: return Circuit::TriodeModel::ecc82();
                case 4: return Circuit::TriodeModel::measured12ax7RSD();
                case 5: return Circuit::TriodeModel::measured12ax7RSD2();
                case 6: return Circuit::TriodeModel::measured12ax7EHX();
                case 7: return Circuit::TriodeModel::ecc83ge();
                default: return Circuit::TriodeModel::ecc83();
            }
        }

        Circuit::JfetModel jfetModelFor(int index)
        {
            switch (index)
            {
                case 1: return Circuit::JfetModel::n2n5457();
                case 2: return Circuit::JfetModel::n2n5485();
                case 3: return Circuit::JfetModel::p2n5460();
                case 4: return Circuit::JfetModel::n2n5952();
                default: return Circuit::JfetModel::j201();
            }
        }

        Circuit::MosfetModel mosfetModelFor(int index)
        {
            switch (index)
            {
                case 1: return Circuit::MosfetModel::pBS250();
                default: return Circuit::MosfetModel::n2N7000();
            }
        }

        Circuit::VacuumDiodeModel rectifierModelFor(int index)
        {
            switch (index)
            {
                case 1: return Circuit::VacuumDiodeModel::u5u4gb();
                case 2: return Circuit::VacuumDiodeModel::u5y3gt();
                default: return Circuit::VacuumDiodeModel::gz34();
            }
        }

        Circuit::PentodeModel pentodeModelFor(int index)
        {
            switch (index)
            {
                case 1: return Circuit::PentodeModel::u6l6gc();
                case 2: return Circuit::PentodeModel::el84();
                case 3: return Circuit::PentodeModel::kt66();
                case 4: return Circuit::PentodeModel::kt77();
                case 5: return Circuit::PentodeModel::u6v6();
                case 6: return Circuit::PentodeModel::kt88();
                default: return Circuit::PentodeModel::el34();
            }
        }

        Circuit::OpAmpModel opAmpModelFor(int index)
        {
            switch (index)
            {
                case 1: return Circuit::OpAmpModel::jrc4558();
                case 2: return Circuit::OpAmpModel::ne5532();
                case 3: return Circuit::OpAmpModel::lm308();
                case 5: return Circuit::OpAmpModel::lm741();
                default: return Circuit::OpAmpModel::tl072();
            }
        }

        Circuit::Potentiometer::Taper engineTaperFor(Taper taper)
        {
            switch (taper)
            {
                case Taper::Logarithmic: return Circuit::Potentiometer::Taper::Logarithmic;
                case Taper::ReverseLogarithmic: return Circuit::Potentiometer::Taper::ReverseLogarithmic;
                case Taper::Log5A: return Circuit::Potentiometer::Taper::Audio5;
                case Taper::Log10A: return Circuit::Potentiometer::Taper::Audio10;
                case Taper::Log15A: return Circuit::Potentiometer::Taper::Audio15;
                case Taper::Log20A: return Circuit::Potentiometer::Taper::Audio20;
                case Taper::Log30A: return Circuit::Potentiometer::Taper::Audio30;
                case Taper::Linear: break;
            }

            return Circuit::Potentiometer::Taper::Linear;
        }

        /** Index of the ideal op-amp model, which takes its own code path. */
        constexpr int idealOpAmpIndex = 4;

        /** Index of the Real transformer model (0 is Ideal). */
        constexpr int realTransformerIndex = 1;

        /** The strays that make a transformer real, all derived from the turns ratio assuming
            an 8 ohm secondary: primary impedance is ratio^2 * 8 and the rest are proportions
            of it. A typical shape, not a specific transformer. */
        struct TransformerStrays
        {
            double magnetising;          // henries, across the primary
            double leakage;              // henries, in series with the primary
            double primaryResistance;    // ohms
            double secondaryResistance;  // ohms
        };

        TransformerStrays straysFor(double ratio)
        {
            constexpr double referenceLoad = 8.0;   // ohms, assumed on the secondary
            constexpr double bassCorner = 30.0;     // Hz, where the magnetising inductance gives up
            constexpr double leakageRatio = 300.0;  // Lm/Ll, so the treble corner is 300x the bass one
            constexpr double copperRatio = 40.0;    // winding impedance over its DC resistance

            const double primaryImpedance = ratio * ratio * referenceLoad;

            TransformerStrays strays;
            strays.magnetising = primaryImpedance / (2.0 * juce::MathConstants<double>::pi * bassCorner);
            strays.leakage = strays.magnetising / leakageRatio;
            strays.primaryResistance = primaryImpedance / copperRatio;
            strays.secondaryResistance = referenceLoad / copperRatio;
            return strays;
        }

        /** Strays for a centre-tapped transformer, where the tapped (plate) winding is the
            secondary and the speaker is on the primary, so the impedances are the other way
            up from straysFor(). The strays sit on the primary, referred to it; only the copper
            scales with the ratio. */
        TransformerStrays straysForCenterTapped(double ratio)  // secondary : primary
        {
            constexpr double referenceLoad = 8.0;   // ohms, now assumed on the *primary*
            constexpr double bassCorner = 30.0;
            constexpr double leakageRatio = 300.0;
            constexpr double copperRatio = 40.0;

            TransformerStrays strays;
            strays.magnetising = referenceLoad / (2.0 * juce::MathConstants<double>::pi * bassCorner);
            strays.leakage = strays.magnetising / leakageRatio;
            strays.primaryResistance = referenceLoad / copperRatio;

            // Halved: one resistor per half of the tapped winding.
            strays.secondaryResistance = 0.5 * ratio * ratio * referenceLoad / copperRatio;
            return strays;
        }

        /** Name for an unlabelled control. */
        juce::String defaultControlName(const Element& element, int ordinal)
        {
            if (element.label.isNotEmpty())
                return element.label;

            return (element.type == ElementType::Potentiometer ? "Knob " : "Switch ") + juce::String(ordinal);
        }

        /** Checks the drawing before anything is stamped, so mistakes become messages rather
            than a singular matrix. Adds one message per problem (per part where there is one)
            and returns the error that stops the build, or empty. */
        juce::String validate(const Schematic& schematic, BuildResult& result)
        {
            using Severity = Diagnostic::Severity;

            if (schematic.getElements().empty())
                return "Schematic is empty.";

            if (schematic.countElementsOfType(ElementType::Ground) == 0)
                return "Ground terminal is missing.";

            const int inputs = schematic.countElementsOfType(ElementType::Input);
            const int outputs = schematic.countElementsOfType(ElementType::Output);

            if (inputs == 0)
                return "Input terminal is missing from the schematic.";

            if (outputs == 0)
                return "Output terminal is missing from the schematic.";

            // Refused rather than merged: every input names its net "in", so two would silently
            // join unconnected parts of the drawing.
            if (inputs > 1)
                return "More than one input terminal. Only one is allowed.";

            if (outputs > 1)
                return "More than one output terminal. Only one is allowed.";

            // Terminals on the wrong net. An output on ground, or input and output on one net,
            // gives a singular matrix. An input on ground is worse: it builds fine and outputs
            // silence. Compared by net index, not name.
            {
                const auto nets = schematic.extractNets();
                const auto& elements = schematic.getElements();

                // Every ground's net (a sheet can have several). Input and output are single by now.
                std::vector<int> groundNets;
                int inputNet = -1, outputNet = -1;

                for (size_t e = 0; e < elements.size(); ++e)
                {
                    const int net = nets.netOfPin[e][0];

                    if (elements[e].type == ElementType::Ground)
                        groundNets.push_back(net);
                    else if (elements[e].type == ElementType::Input)
                        inputNet = net;
                    else if (elements[e].type == ElementType::Output)
                        outputNet = net;
                }

                const auto onGround = [&](int net)
                {
                    return net >= 0 && std::find(groundNets.begin(), groundNets.end(), net) != groundNets.end();
                };

                // Output is checked first, so a sheet with both on ground reports the output.
                if (onGround(outputNet))
                    return "The output terminal is wired to ground.";

                if (onGround(inputNet))
                    return "The input terminal is wired to ground.";

                if (inputNet >= 0 && inputNet == outputNet)
                    return "The input and output terminals are wired together.";
            }

            // Parts still at zero: values start at zero so an unfilled part is caught. An error,
            // since zero ohms is a short and zero farads isn't a part.
            int unset = 0;

            for (const auto& element : schematic.getElements())
            {
                if (element.hasNumericValue() && element.value <= 0.0)
                {
                    result.add(Severity::Error, "Part has no value.", {}, &element);
                    ++unset;
                }
                else if (element.hasSecondValue() && ! element.isSecondValueRail() && element.valueB <= 0.0)
                {
                    result.add(Severity::Error, "Part is missing a value.", {}, &element);
                    ++unset;
                }
                else if (element.isSecondValueRail() && element.valueB > 0.0)
                {
                    // Can't be entered in the inspector, but a file may contain it.
                    result.add(Severity::Error, "Negative supply is above ground.", {}, &element);
                    ++unset;
                }
                else if (element.type == ElementType::Node && element.label.trim().isEmpty())
                {
                    // A warning: an unlabelled node still works as a junction, it just joins nothing
                    // else.
                    result.add(Severity::Warning,
                               "This node has no label.", {}, &element);
                }
            }

            if (unset > 0)
                return juce::String(unset) + (unset == 1 ? " part has" : " parts have")
                     + " no value yet.";

            // An amplifier supply below what the model can run on: the output would sit at one
            // level whatever the input, which looks like a broken circuit.
            int starved = 0;

            for (const auto& element : schematic.getElements())
            {
                double range = 1.0;

                if (element.type == ElementType::OpAmp && element.modelIndex != idealOpAmpIndex)
                    range = opAmpModelFor(element.modelIndex).withRails(element.value, element.valueB).outputRange();
                else if (element.type == ElementType::PowerAmp)
                    range = Circuit::PowerAmpModel::lm386().withSupply(element.value).outputRange();

                if (range > 0.0)
                    continue;

                const auto models = getModelChoices(element.type);
                const auto name = juce::isPositiveAndBelow(element.modelIndex, models.size())
                                    ? models[element.modelIndex]
                                    : juce::String("part");

                // Only a real second rail counts as the bottom (not the LM386's second value).
                const double bottom = element.isSecondValueRail() ? element.valueB : 0.0;
                const double needed = element.value - bottom - range;

                result.add(Severity::Error,
                           "Supply too low for the " + name + ": it needs more than "
                               + juce::String(needed, 1)
                               + (bottom < 0.0 ? " V between its two supplies." : " V."),
                           {}, &element);
                ++starved;
            }

            if (starved > 0)
                return juce::String(starved) + (starved == 1 ? " amplifier has" : " amplifiers have")
                     + " too low a supply to run.";

            return {};
        }

        /** Reports pins connected to nothing. A warning: usually a wire that stopped short. */
        void reportDanglingPins(const Schematic& schematic, const NetList& nets, BuildResult& result)
        {
            const auto& elements = schematic.getElements();

            for (const int net : nets.danglingNets)
            {
                for (size_t e = 0; e < elements.size(); ++e)
                {
                    for (int pin = 0; pin < elements[e].getPinCount(); ++pin)
                    {
                        if (nets.netOfPin[e][static_cast<size_t>(pin)] != net)
                            continue;

                        // A power amp's gain pins are often left open (gain 20).
                        if (elements[e].type == ElementType::PowerAmp && pin >= 3)
                            continue;

                        const auto at = elements[e].getPinPosition(pin);

                        result.add(Diagnostic::Severity::Warning,
                                   "Pin " + juce::String(pin + 1) + " at " + juce::String(at.x) + ","
                                       + juce::String(at.y)
                                       + " touches nothing else. Check for a wire that stops short.",
                                   {}, &elements[e]);
                    }
                }
            }
        }

        /** Warns about Ideal transformers with nothing across the primary: an ideal
            transformer passes DC. Detected as no inductor on the same two nets as the primary. */
        void warnAboutUnmagnetisedTransformers(const Schematic& schematic, const NetList& nets,
                                               BuildResult& result)
        {
            const auto& elements = schematic.getElements();

            auto netOf = [&](size_t element, int pin)
            { return nets.netOfPin[element][static_cast<size_t>(pin)]; };

            for (size_t e = 0; e < elements.size(); ++e)
            {
                if (elements[e].type != ElementType::Transformer
                    && elements[e].type != ElementType::CenterTapTransformer)
                    continue;

                // The Real model includes its own magnetising inductance.
                if (elements[e].modelIndex == realTransformerIndex)
                    continue;

                const int primaryA = netOf(e, 0);
                const int primaryB = netOf(e, 1);
                bool magnetised = false;

                for (size_t other = 0; other < elements.size() && ! magnetised; ++other)
                {
                    if (elements[other].type != ElementType::Inductor)
                        continue;

                    const int a = netOf(other, 0);
                    const int b = netOf(other, 1);
                    magnetised = (a == primaryA && b == primaryB) || (a == primaryB && b == primaryA);
                }

                if (! magnetised)
                    result.add(Diagnostic::Severity::Warning,
                               "Nothing across the primary coil of a transformer. An ideal transformer passes DC. Add an "
                               "inductor for the magnetising inductance, or switch the model to Real.",
                               {}, &elements[e]);
            }
        }

        /** The net names of one element's pins (Circuit maps nodes by name). */
        struct PinNets
        {
            const NetList& nets;
            size_t element;

            const juce::String& operator()(int pin) const
            {
                return nets.netNames[static_cast<size_t>(
                    nets.netOfPin[element][static_cast<size_t>(pin)])];
            }
        };

        /** The control this part belongs to: an existing one with the same label and kind
            (ganged), or a new one. `ordinal` is used only for a new control. */
        LiveControl& attachControl(std::vector<LiveControl>& controls, LiveControl::Kind kind,
                                   const Element& element, int& ordinal)
        {
            if (element.label.isNotEmpty())
            {
                const auto sameControl = [&](const LiveControl& c)
                { return c.kind == kind && c.name == element.label; };

                if (const auto it = std::find_if(controls.begin(), controls.end(), sameControl);
                    it != controls.end())
                {
                    it->elementIds.push_back(element.id);
                    return *it;
                }
            }

            LiveControl control;
            control.kind = kind;
            control.name = defaultControlName(element, ordinal++);
            control.order = element.controlOrder;
            control.elementIds.push_back(element.id);

            controls.push_back(std::move(control));
            return controls.back();
        }

        void addTransformer(Circuit& circuit, const Element& element, const PinNets& net)
        {
            const bool tapped = element.type == ElementType::CenterTapTransformer;

            // Turn counts go straight in; only their proportion matters.
            const double primaryTurns = element.value;
            const double secondaryTurns = element.valueB;

            if (element.modelIndex != realTransformerIndex)
            {
                if (tapped)
                    circuit.addCenterTapTransformer(net(0), net(1), net(2), net(3), net(4),
                                                    primaryTurns, secondaryTurns);
                else
                    circuit.addTransformer({{net(0), net(1), primaryTurns},
                                            {net(2), net(3), secondaryTurns}});

                return;
            }

            // Copper and leakage in series with the primary, magnetising inductance across it,
            // copper on each secondary. Internal nodes are named after the element id.
            const auto strays = tapped
                ? straysForCenterTapped(primaryTurns > 0.0 ? secondaryTurns / primaryTurns : 1.0)
                : straysFor(element.getTurnsRatio());

            const auto tag = "XF" + juce::String(element.id);
            const auto afterCopper = tag + "p1";
            const auto afterLeakage = tag + "p2";
            const auto secondaryInner = tag + "s1";

            circuit.addResistor(net(0), afterCopper, strays.primaryResistance);
            circuit.addInductor(afterCopper, afterLeakage, strays.leakage);
            circuit.addInductor(afterLeakage, net(1), strays.magnetising);

            if (tapped)
            {
                // Each half of the tapped winding gets its own copper; the tap is just a node.
                const auto secondaryInnerB = tag + "s2";

                circuit.addCenterTapTransformer(afterLeakage, net(1), secondaryInner,
                                                net(3), secondaryInnerB,
                                                primaryTurns, secondaryTurns);
                circuit.addResistor(secondaryInner, net(2), strays.secondaryResistance);
                circuit.addResistor(secondaryInnerB, net(4), strays.secondaryResistance);
            }
            else
            {
                circuit.addTransformer({{afterLeakage, net(1), primaryTurns},
                                        {secondaryInner, net(3), secondaryTurns}});
                circuit.addResistor(secondaryInner, net(2), strays.secondaryResistance);
            }
        }

        void addPotentiometer(Circuit& circuit, const Element& element, const PinNets& net,
                              std::vector<LiveControl>& controls, int& ordinal)
        {
            auto pot = circuit.addPotentiometer(net(0), net(1), net(2), element.value,
                                                engineTaperFor(element.taper));

            attachControl(controls, LiveControl::Kind::Pot, element, ordinal).pots.push_back(pot);
        }

        void addSwitch(Circuit& circuit, const Element& element, const PinNets& net,
                       std::vector<LiveControl>& controls, int& ordinal)
        {
            auto& control = attachControl(controls, LiveControl::Kind::Switch, element, ordinal);

            if (element.type != ElementType::Spdt)
            {
                // Closed in the upper ("on") position.
                control.contacts.push_back({circuit.addSwitch(net(0), net(1), element.closed), 2, 0b10u});
                return;
            }

            // One contact per throw, from the common, closed in the positions that put the lever
            // there. Built in its current position, since the bias point is solved before any
            // control is applied. Further poles are separate switches with the same label.
            const int positions = element.getSwitchPositions();

            for (int t = 0; t < element.getSwitchThrows(); ++t)
            {
                unsigned closedIn = 0;

                for (int k = 0; k < positions; ++k)
                    if (element.getThrowAt(k) == t)
                        closedIn |= 1u << k;

                const bool closedNow = element.getThrowAt(element.switchPosition) == t;
                control.contacts.push_back({circuit.addSwitch(net(0), net(1 + t), closedNow), positions, closedIn});
            }

            // The strip uses the control's widest switch, and shows an ON-OFF-ON's middle unlit.
            if (positions > control.positions)
                control.positions = positions;

            if (element.switchAction == SwitchAction::OnOffOn && control.elementIds.size() == 1)
                control.offPosition = 1;
        }

        /** Stamps one element. Terminals, annotations and scopes stamp nothing. */
        void addElement(Circuit& circuit, const Element& element, const PinNets& net, double sampleRate,
                        std::vector<LiveControl>& controls, int& potOrdinal, int& switchOrdinal)
        {
            switch (element.type)
            {
                case ElementType::Ground:
                case ElementType::Input:
                case ElementType::Output:
                case ElementType::Node:
                case ElementType::Text:
                case ElementType::Rectangle:
                case ElementType::Scope:
                    break;

                case ElementType::Resistor:
                    circuit.addResistor(net(0), net(1), element.value);
                    break;

                case ElementType::Capacitor:
                    // Pin 0 is the positive terminal, so prepare() can report a reversed electrolytic.
                    if (element.polarised)
                        circuit.addPolarisedCapacitor(net(0), net(1), element.value);
                    else
                        circuit.addCapacitor(net(0), net(1), element.value);

                    break;

                case ElementType::Inductor:
                    circuit.addInductor(net(0), net(1), element.value);
                    break;

                case ElementType::VoltageSource:
                    circuit.addVoltageSource(net(0), net(1), element.value);
                    break;

                case ElementType::Diode:
                    circuit.addDiode(net(0), net(1), diodeModelFor(element.modelIndex));
                    break;

                case ElementType::Transistor:
                    circuit.addTransistor(net(0), net(1), net(2), transistorModelFor(element.modelIndex));
                    break;

                case ElementType::Jfet:
                    circuit.addJfet(net(0), net(1), net(2), jfetModelFor(element.modelIndex));
                    break;

                case ElementType::Mosfet:
                    circuit.addMosfet(net(0), net(1), net(2), mosfetModelFor(element.modelIndex));
                    break;

                case ElementType::PowerAmp:
                    // The supply is the part's value. The name must be unique.
                    circuit.addPowerAmp("A" + juce::String(element.id), net(0), net(1), net(2), net(3), net(4),
                                        Circuit::PowerAmpModel::lm386().withSupply(element.value));
                    break;

                case ElementType::Triode:
                    circuit.addTriode(net(0), net(1), net(2), triodeModelFor(element.modelIndex));
                    break;

                case ElementType::VacuumDiode:
                    circuit.addVacuumDiode(net(0), net(1), rectifierModelFor(element.modelIndex));
                    break;

                case ElementType::Pentode:
                    // No suppressor node: the Koren model has none (g3 at the cathode).
                    circuit.addPentode(net(0), net(1), net(2), net(3), pentodeModelFor(element.modelIndex));
                    break;

                case ElementType::OpAmp:
                    if (element.modelIndex == idealOpAmpIndex)
                    {
                        circuit.addIdealOpAmp(net(0), net(1), net(2));
                    }
                    else
                    {
                        // The element id makes the name unique.
                        auto model = opAmpModelFor(element.modelIndex).withRails(element.value, element.valueB);

                        // A part too fast to slew at this sample rate gets the linear input stage, which is
                        // exact here and saves two Newton ports (see OpAmpModel::slewsAt()).
                        if (! model.slewsAt(sampleRate))
                            model.slewRate = 0.0;

                        circuit.addOpAmp("U" + juce::String(element.id), net(0), net(1), net(2), model);
                    }

                    break;

                case ElementType::Transformer:
                case ElementType::CenterTapTransformer:
                    addTransformer(circuit, element, net);
                    break;

                case ElementType::Potentiometer:
                    addPotentiometer(circuit, element, net, controls, potOrdinal);
                    break;

                case ElementType::Switch:
                case ElementType::Spdt:
                    addSwitch(circuit, element, net, controls, switchOrdinal);
                    break;
            }
        }

        /** Where every probe's pins landed, from the drawing. */
        void collectProbes(const Schematic& schematic, const NetList& nets, BuildResult& result)
        {
            const auto& elements = schematic.getElements();

            for (size_t e = 0; e < elements.size(); ++e)
            {
                const auto& element = elements[e];
                const PinNets net{nets, e};

                if (element.type == ElementType::Scope)
                    result.probes.push_back({element.id, net(0), net(1), element.scopeSeconds});
                else if (element.type == ElementType::Resistor && element.value > 0.0)
                    result.currentProbes.push_back({element.id, net(0), net(1), element.value});
            }
        }

        /** Resolves probe node names to indices. Must run after setInputNode/setOutputNode,
            which can create nodes. */
        void resolveProbes(const Circuit& circuit, BuildResult& result)
        {
            for (auto& probe : result.probes)
            {
                probe.positiveIndex = circuit.getNodeIndex(probe.positiveNode);
                probe.referenceIndex = circuit.getNodeIndex(probe.referenceNode);
            }

            for (auto& probe : result.currentProbes)
            {
                probe.indexA = circuit.getNodeIndex(probe.nodeA);
                probe.indexB = circuit.getNodeIndex(probe.nodeB);
            }
        }

        /** Reports bias point problems. Returns true if the build must stop. */
        bool reportBiasPoint(const Circuit& circuit, BuildResult& result)
        {
            if (! circuit.foundOperatingPoint())
                result.add(Diagnostic::Severity::Warning,
                           "Couldn't settle on a bias point. The circuit will still run, but check "
                           "the supply and ground wiring.");

            if (circuit.getReversedCapacitorCount() > 0)
                result.add(Diagnostic::Severity::Warning,
                           juce::String(circuit.getReversedCapacitorCount())
                               + " polarised capacitor(s) are wired backwards.");

            // The per-sample matrix can be singular even when the bias point solved; process()
            // would then output silence. Skipped when there is nothing to solve.
            if (circuit.getSystemSize() > 0 && ! circuit.hasUsableFactorisation())
            {
                result.error = "The solver couldn't factorise the circuit. Check the wiring and try again.";
                result.add(Diagnostic::Severity::Error, result.error);
                return true;
            }

            return false;
        }
    } // namespace

    //==========================================================================

    BuildResult buildCircuits(const Schematic& schematic, double sampleRate, int channelCount,
                              BuildOptions options)
    {
        BuildResult result;

        // Process-wide, so set once here.
        CircuitComponents::FastMath::setEnabled(options.fastMath);

        const auto nets = schematic.extractNets();
        result.error = validate(schematic, result);

        if (result.error.isNotEmpty())
        {
            // The headline goes first in the list too, so the console shows one list.
            result.diagnostics.insert(result.diagnostics.begin(),
                                      Diagnostic{Diagnostic::Severity::Error, result.error, {}, -1, {}, false});
            return result;
        }

        reportDanglingPins(schematic, nets, result);
        warnAboutUnmagnetisedTransformers(schematic, nets, result);
        collectProbes(schematic, nets, result);

        const auto& elements = schematic.getElements();

        for (int channel = 0; channel < channelCount; ++channel)
        {
            auto circuit = std::make_unique<Circuit>();
            std::vector<LiveControl> controls;
            int potOrdinal = 1, switchOrdinal = 1;

            for (size_t e = 0; e < elements.size(); ++e)
                addElement(*circuit, elements[e], PinNets{nets, e}, sampleRate,
                           controls, potOrdinal, switchOrdinal);

            circuit->setInputNode("in");
            circuit->setOutputNode("out");
            circuit->prepare(sampleRate);

            // Probes are resolved against circuits[0], the one the audio thread samples.
            if (channel == 0)
            {
                resolveProbes(*circuit, result);

                if (reportBiasPoint(*circuit, result))
                    return result;

                // Stable: controls at order 0 keep drawing order.
                std::stable_sort(controls.begin(), controls.end(),
                                 [](const LiveControl& a, const LiveControl& b)
                                 { return a.order < b.order; });

                result.controls = std::move(controls);
            }

            // Subtract the bias point's DC from the output, like an output coupling cap.
            circuit->setOutputOffsetToOperatingPoint();
            result.circuits.push_back(std::move(circuit));
        }

        return result;
    }
} // namespace SchematicModel
