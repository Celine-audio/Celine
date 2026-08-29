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
                case 6: return Circuit::DiodeModel::zener(9.0);
                case 7: return Circuit::DiodeModel::zener(5.1);

                // Listed separately because it is a part people look for by
                // name, not because it differs: ON Semi publish it and the
                // 1N4148 on one datasheet line. Same model, deliberately.
                case 8: return Circuit::DiodeModel::d1n4148();

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

        Circuit::VacuumDiodeModel rectifierModelFor(int index)
        {
            switch (index)
            {
                case 1: return Circuit::VacuumDiodeModel::u5u4gb();
                case 2: return Circuit::VacuumDiodeModel::u5y3gt();
                default: return Circuit::VacuumDiodeModel::gz34();
            }
        }

        /** Applies the build options to a valve model. Turning the capacitance
            off is a matter of zeroing it: the engine already reads a zero as
            "don't model this", so there is no second switch to keep in step. */
        template <typename Model>
        Model withOptions(Model model, const BuildOptions& options)
        {
            if (! options.interelectrodeCapacitance)
                model.capGridCathode = model.capGridPlate = model.capPlateCathode = 0.0;

            return model;
        }

        /** Same idea for transistors: off zeroes the fields and the device
            model reads a zero as "don't model it". */
        Circuit::BjtModel withOptions(Circuit::BjtModel model, const BuildOptions& options)
        {
            if (! options.transistorJunctionCapacitance)
                model.capBaseEmitter = model.capBaseCollector = 0.0;

            if (! options.transistorEarlyEffect)
                model.forwardEarlyVoltage = 0.0;

            return model;
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

        /** Index into getModelChoices(OpAmp) for the ideal one. It is the only
            model that isn't a macro model, so it takes a different code path. */
        constexpr int idealOpAmpIndex = 4;

        /** Transformer models. Ideal is 0, so a sheet saved before the choice
            existed still builds the transformer it built. */
        constexpr int realTransformerIndex = 1;

        /** The strays that make a transformer a real one.

            Sized from one stated assumption -- the secondary works into 8 ohms
            -- which fixes the reflected primary impedance at ratio^2 * 8 and
            makes everything else a proportion of it, which is how real
            transformer data is organised.

            A 22:1 gives 21 H of magnetising inductance, 71 mH of leakage and
            100 ohms of primary DCR, against real ones measuring 15-30 H,
            20-100 mH and 60-120 ohms. A shape, then, not a specific
            transformer, and not fitted the way the valve models are. */
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

        /** The same strays for a *centre-tapped* transformer, which is the other
            way up and cannot use the function above.

            straysFor() puts the high impedance on the primary. A tapped one
            inverts that, and not by choice: the tap has to sit on the winding
            carrying the valve plates, and this element puts the tap on the
            secondary -- so a push-pull output stage wires its plates there and
            the speaker to the primary. A full-wave rectifier is the same shape.

            Sized the wrong way up, a 15:1 output transformer comes out as a
            ratio of 0.065 and 17 dB of the output disappears into a 0.18 mH
            magnetising inductance across the speaker winding.

            The strays stay on the primary, where the stamping puts them, but are
            *referred* to it -- which cancels the ratio out of the magnetising
            inductance altogether, so it falls out as a constant. Only the copper
            still scales, each winding keeping its own. */
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

            // Halved because the stamping puts this resistor on each half of the
            // tapped winding separately, and the two together are the winding.
            strays.secondaryResistance = 0.5 * ratio * ratio * referenceLoad / copperRatio;
            return strays;
        }

        /** A readable name for a control that hasn't been given a label. */
        juce::String defaultControlName(const Element& element, int ordinal)
        {
            if (element.label.isNotEmpty())
                return element.label;

            return (element.type == ElementType::Potentiometer ? "Knob " : "Switch ") + juce::String(ordinal);
        }

        /** Checks the drawing makes sense before anything is stamped, so a
            mistake produces a sentence rather than a singular matrix.

            Fills `result` with one message per problem -- per *part*, where
            there is a part -- and returns the one that should stop the build,
            or nothing if it can go ahead. */
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

            // Refused rather than combined, and the wording has to say so: a
            // non-empty return from here stops the build, so the old message --
            // "their nodes have been combined" -- promised an outcome that never
            // happened. Nothing was combined, because nothing was built.
            //
            // Combining would in fact be well defined. Every input terminal names
            // its net "in", and Circuit maps nodes by name, so two of them become
            // one node. That is exactly the problem: two *unwired* regions of the
            // drawing would silently become one, with nothing on the sheet saying
            // so. One terminal each way is the rule worth keeping.
            if (inputs > 1)
                return "More than one input terminal. Only one is allowed.";

            if (outputs > 1)
                return "More than one output terminal. Only one is allowed.";

            // An output sharing a net with ground is silence by construction,
            // and surfaces unhelpfully: the net takes ground's name, so
            // setOutputNode("out") invents an empty node whose all-zero row
            // makes the matrix singular. Better to say what was drawn.
            //
            // Compared by net *index*, not by name -- the check has no business
            // depending on which name a shared net ended up with. An input wired
            // to its output fails the same way and means the same slip.
            //
            // Three ways to draw a terminal that cannot work, and the third is
            // the one worth the extra check. Output-on-ground and input-on-
            // output both end in a singular matrix, which is at least loud. An
            // *input* on ground does not: the net keeps ground's name, so
            // setInputNode("in") invents a node nothing is attached to, and
            // because the input node is a known voltage rather than an unknown
            // it takes no row at all. Nothing is singular, so the build passes,
            // the bias point solves, and every sample comes out at zero.
            // Measured on a resistor divider drawn that way: build valid, no
            // diagnostics, usable factorisation, and a peak output of exactly
            // 0.0 for a full-scale input. Silence that reports itself as
            // success is the worst of the three to debug from the sheet.
            {
                const auto nets = schematic.extractNets();
                const auto& elements = schematic.getElements();

                // Every ground's net, since a sheet can hold several and they
                // only merge (by name) once the circuit is built. Input and
                // output are single: more than one of either failed above.
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

                // Order preserved from when there were two: a sheet with both
                // terminals on ground satisfies all three tests, and the output
                // message is the one that build already reported.
                if (onGround(outputNet))
                    return "The output terminal is wired to ground.";

                if (onGround(inputNet))
                    return "The input terminal is wired to ground.";

                if (inputNet >= 0 && inputNet == outputNet)
                    return "The input and output terminals are wired together.";
            }

            // Parts still sitting at zero. Every value starts there rather than
            // at a plausible default, so this catches the thing a default hides:
            // a part you placed and never got round to filling in. It has to be
            // an error rather than a warning -- a zero-ohm resistor is a short
            // and a zero-farad capacitor isn't a component, so the matrix that
            // came out the other side would be singular or meaningless.
            int unset = 0;

            for (const auto& element : schematic.getElements())
            {
                if (element.hasNumericValue() && element.value <= 0.0)
                {
                    result.add(Severity::Error, "Part has no value.", {}, &element);
                    ++unset;
                }
                else if (element.hasSecondValue() && element.valueB <= 0.0)
                {
                    result.add(Severity::Error, "Part is missing a value.", {}, &element);
                    ++unset;
                }
                else if (element.type == ElementType::Node && element.label.trim().isEmpty())
                {
                    // A warning, not an error: the label is the whole of what a
                    // node is *for*, but one without a label is still a plain
                    // junction on its own net, which builds and behaves. It just
                    // joins nothing, which is worth saying out loud because the
                    // symbol looks identical either way.
                    result.add(Severity::Warning,
                               "This node has no label.", {}, &element);
                }
            }

            if (unset > 0)
                return juce::String(unset) + (unset == 1 ? " part has" : " parts have")
                     + " no value yet.";

            return {};
        }

        /** A dangling pin is legal -- a test point, a deliberately unconnected
            leg -- but it is usually a wire that stopped a square short, so name
            the part and let the drawing go ahead. */
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

        /** Transformers here are ideal, and an ideal transformer passes DC --
            which a real one cannot, because at DC its primary is a short.

            The fix is an inductor across the primary for the magnetising
            inductance, which is a short at DC and so keeps the standing voltage
            off the secondary. That is a composition the drawing has to make
            rather than something the component can do for itself, so it is easy
            to leave out and confusing when you do: the bias point comes through
            the transformer and nothing looks obviously wrong.

            Detected by looking for an inductor sitting on the same two nets as
            the primary. */
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

                // A Real one wires its own magnetising inductance, so there is
                // nothing to warn about.
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

        /** The net name on each of one element's pins -- all Circuit needs,
            since it maps nodes by name. */
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

        /** The control this part belongs to: the one it gangs onto if it shares
            a label with an earlier part of the same kind, otherwise a new one.

            Ganging keys on the label because that is the only thing on the
            drawing that says two parts are one control. `ordinal` is consumed
            only when a control is actually created. */
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

            // Turns, not a ratio: addTransformer only cares about the proportion
            // between windings, so the two boxes go straight in.
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

            // Copper and leakage in series with the primary, magnetising
            // inductance across it -- which is also what stops it passing DC --
            // and copper on each secondary. Internal nodes are named after the
            // element id, which is already unique on the sheet.
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
                // Each half is its own winding and gets its own copper. The tap
                // is a node, not a winding.
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
                control.toggles.push_back(circuit.addSwitch(net(0), net(1), element.closed));
                return;
            }

            // Pin order is common, ON throw, OFF throw. addChangeoverSwitch
            // always starts on throw A, and the bias point is solved before any
            // knob position reaches us, so it is selected here.
            auto changeover = circuit.addChangeoverSwitch(net(0), net(1), net(2));
            changeover.select(circuit, ! element.closed);

            control.changeovers.push_back(changeover);
        }

        /** Stamps one element. Terminals, annotations and scopes stamp nothing:
            a terminal has already named its net, an annotation has no pins, and
            a scope must not change the node it watches. */
        void addElement(Circuit& circuit, const Element& element, const PinNets& net,
                        const BuildOptions& options, std::vector<LiveControl>& controls,
                        int& potOrdinal, int& switchOrdinal)
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
                    // Pin 0 is an electrolytic's marked positive terminal, which
                    // is what lets prepare() report one wired in backwards.
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
                    circuit.addTransistor(net(0), net(1), net(2),
                                          withOptions(transistorModelFor(element.modelIndex), options));
                    break;

                case ElementType::Jfet:
                    circuit.addJfet(net(0), net(1), net(2), jfetModelFor(element.modelIndex));
                    break;

                case ElementType::Triode:
                    circuit.addTriode(net(0), net(1), net(2),
                                      withOptions(triodeModelFor(element.modelIndex), options));
                    break;

                case ElementType::VacuumDiode:
                    circuit.addVacuumDiode(net(0), net(1), rectifierModelFor(element.modelIndex));
                    break;

                case ElementType::Pentode:
                    // No suppressor node: the Koren model has no term for it,
                    // g3 being taken to sit at the cathode.
                    circuit.addPentode(net(0), net(1), net(2), net(3),
                                       withOptions(pentodeModelFor(element.modelIndex), options));
                    break;

                case ElementType::OpAmp:
                    if (element.modelIndex == idealOpAmpIndex)
                    {
                        circuit.addIdealOpAmp(net(0), net(1), net(2));
                    }
                    else
                    {
                        // The name must be unique on the sheet; the element id is.
                        circuit.addOpAmp("U" + juce::String(element.id), net(0), net(1), net(2),
                                         opAmpModelFor(element.modelIndex).withRails(element.value, 0.0));
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

        /** Where every probe's pins landed. Collected from the drawing rather
            than from a circuit, so it happens once however many channels run. */
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

        /** Resolves the probes' node names to indices, once the netlist is
            complete -- setInputNode and setOutputNode can both mint a node, so
            this cannot run before them. */
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

        /** What the bias point has to say. Returns true if it stopped the build. */
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

            // The DC system and the per-sample system are different matrices, so
            // a circuit can settle on a sensible bias point and still have a
            // per-sample matrix the solver cannot factorise -- at which point
            // process() returns silence for every sample with nothing to show
            // for it. Guarded on there being something to solve: a circuit whose
            // every node is ground or the input has no factorisation by
            // definition, and is obvious on the sheet.
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

        // Process-wide, so it is set once here rather than per circuit.
        CircuitComponents::FastMath::setEnabled(options.fastMath);

        const auto nets = schematic.extractNets();
        result.error = validate(schematic, result);

        if (result.error.isNotEmpty())
        {
            // The headline goes in the list too, so the console shows one
            // ordered account rather than a banner plus a separate list.
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
                addElement(*circuit, elements[e], PinNets{nets, e}, options,
                           controls, potOrdinal, switchOrdinal);

            circuit->setPortVoltagePrediction(options.predictNewtonSeed);
            circuit->setInputNode("in");
            circuit->setOutputNode("out");
            circuit->prepare(sampleRate);

            // Everything below describes the drawing rather than the channel, and
            // the audio thread only ever samples probes against circuits[0], so
            // the indices are resolved against exactly the circuit that reads
            // them.
            if (channel == 0)
            {
                resolveProbes(*circuit, result);

                if (reportBiasPoint(*circuit, result))
                    return result;

                // Stable, so controls left at order 0 keep the order they were
                // drawn in and only the ones you numbered move.
                std::stable_sort(controls.begin(), controls.end(),
                                 [](const LiveControl& a, const LiveControl& b)
                                 { return a.order < b.order; });

                result.controls = std::move(controls);
            }

            // A biased stage rests some volts above ground; subtracting that is
            // the DC blocking capacitor every real pedal has on its output,
            // without the extra node.
            circuit->setOutputOffsetToOperatingPoint();
            result.circuits.push_back(std::move(circuit));
        }

        return result;
    }
} // namespace SchematicModel
