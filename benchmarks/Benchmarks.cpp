// The prefab circuits used to arrive via PluginProcessor.h, which included them
// back when the plugin was one hard-coded pedal. It now builds whatever is drawn,
// so the benchmarks ask for them directly.
#include <CelineEngine/Circuits.h>

TEST_CASE ("Boot performance")
{
    BENCHMARK_ADVANCED ("Processor constructor")
    (Catch::Benchmark::Chronometer meter)
    {
        std::vector<Catch::Benchmark::storage_for<PluginProcessor>> storage (size_t (meter.runs()));
        meter.measure ([&] (int i) { storage[(size_t) i].construct(); });
    };

    BENCHMARK_ADVANCED ("Processor destructor")
    (Catch::Benchmark::Chronometer meter)
    {
        std::vector<Catch::Benchmark::destructable_object<PluginProcessor>> storage (size_t (meter.runs()));
        for (auto& s : storage)
            s.construct();
        meter.measure ([&] (int i) { storage[(size_t) i].destruct(); });
    };

    BENCHMARK_ADVANCED ("Editor open and close")
    (Catch::Benchmark::Chronometer meter)
    {
        PluginProcessor plugin;

        // due to complex construction logic of the editor, let's measure open/close together
        meter.measure ([&] (int /* i */) {
            auto editor = plugin.createEditorAndMakeActive();
            plugin.editorBeingDeleted (editor);
            delete editor;
            return plugin.getActiveEditor();
        });
    };
}

//==============================================================================
// Circuit engine
//
// These measure one 512-sample block of a single channel. To read them as a CPU
// budget: at 48 kHz a 512-sample block has 10.67 ms of wall clock to fit into,
// so a block time of 100 us is roughly 1% of one core per channel.
//
// Build these in Release before drawing any conclusions -- the solver's inner
// loops are exactly the kind of code a Debug build makes look ten times worse
// than it is.
//==============================================================================

TEST_CASE ("Circuit engine performance")
{
    constexpr double benchmarkSampleRate = 48000.0;
    constexpr int blockSize = 512;

    // A 440 Hz sine at a level that drives the nonlinear circuits well into clipping.
    auto makeBlock = [] (float amplitude) {
        std::vector<float> block (blockSize);
        for (int i = 0; i < blockSize; ++i)
            block[(size_t) i] = amplitude * std::sin (2.0f * 3.14159265f * 440.0f * (float) i / (float) benchmarkSampleRate);
        return block;
    };

    BENCHMARK_ADVANCED ("Classic tone stack, 512 samples (linear, cached factorisation)")
    (Catch::Benchmark::Chronometer meter)
    {
        auto toneStack = Circuits::makeClassicToneStack();
        toneStack.circuit.prepare (benchmarkSampleRate);
        auto block = makeBlock (0.5f);

        std::vector<float> scratch (blockSize);

        meter.measure ([&] (int) {
            std::copy (block.begin(), block.end(), scratch.begin());
            toneStack.circuit.process (scratch.data(), blockSize);
            return scratch[0];
        });
    };

    BENCHMARK_ADVANCED ("Classic tone stack, 512 samples (knob moved every block)")
    (Catch::Benchmark::Chronometer meter)
    {
        auto toneStack = Circuits::makeClassicToneStack();
        toneStack.circuit.prepare (benchmarkSampleRate);
        auto block = makeBlock (0.5f);
        float knob = 0.0f;

        // Forces a re-stamp and re-factorisation once per block, which is what a
        // moving knob costs.
        std::vector<float> scratch (blockSize);

        meter.measure ([&] (int) {
            knob = knob >= 1.0f ? 0.0f : knob + 0.01f;
            toneStack.setControls (knob, knob, knob);
            std::copy (block.begin(), block.end(), scratch.begin());
            toneStack.circuit.process (scratch.data(), blockSize);
            return scratch[0];
        });
    };

    BENCHMARK_ADVANCED ("Diode clipper, 512 samples (clean, barely conducting)")
    (Catch::Benchmark::Chronometer meter)
    {
        auto clipper = Circuits::makeDiodeClipper();
        clipper.prepare (benchmarkSampleRate);
        auto block = makeBlock (0.05f);

        std::vector<float> scratch (blockSize);

        meter.measure ([&] (int) {
            std::copy (block.begin(), block.end(), scratch.begin());
            clipper.process (scratch.data(), blockSize);
            return scratch[0];
        });
    };

    BENCHMARK_ADVANCED ("Diode clipper, 512 samples (driven hard into clipping)")
    (Catch::Benchmark::Chronometer meter)
    {
        auto clipper = Circuits::makeDiodeClipper();
        clipper.prepare (benchmarkSampleRate);
        auto block = makeBlock (10.0f);

        std::vector<float> scratch (blockSize);

        meter.measure ([&] (int) {
            std::copy (block.begin(), block.end(), scratch.begin());
            clipper.process (scratch.data(), blockSize);
            return scratch[0];
        });
    };

    BENCHMARK_ADVANCED ("Transistor booster, 512 samples (clean)")
    (Catch::Benchmark::Chronometer meter)
    {
        auto booster = Circuits::makeTransistorBooster();
        booster.prepare (benchmarkSampleRate);
        auto block = makeBlock (0.1f);

        std::vector<float> scratch (blockSize);

        meter.measure ([&] (int) {
            std::copy (block.begin(), block.end(), scratch.begin());
            booster.process (scratch.data(), blockSize);
            return scratch[0];
        });
    };

    BENCHMARK_ADVANCED ("Transistor booster, 512 samples (overdriven)")
    (Catch::Benchmark::Chronometer meter)
    {
        auto booster = Circuits::makeTransistorBooster();
        booster.prepare (benchmarkSampleRate);
        auto block = makeBlock (5.0f);

        std::vector<float> scratch (blockSize);

        meter.measure ([&] (int) {
            std::copy (block.begin(), block.end(), scratch.begin());
            booster.process (scratch.data(), blockSize);
            return scratch[0];
        });
    };

    // The same circuit solved both ways. DK eliminates the linear part once and
    // iterates on the 4 ports; full Newton stamps the devices into the whole
    // 10x10 and re-factorises every iteration. The gap here is what the method
    // buys, and it widens with the node-to-port ratio -- which is exactly the
    // direction a valve preamp goes.
    for (const auto strategy : {Circuit::SolverStrategy::DiscreteK, Circuit::SolverStrategy::FullNewton})
    {
        const auto* label = strategy == Circuit::SolverStrategy::DiscreteK
                              ? "Germanium fuzz, 512 samples (n=10, m=4) -- DK"
                              : "Germanium fuzz, 512 samples (n=10, m=4) -- full Newton";

        BENCHMARK_ADVANCED (label)
        (Catch::Benchmark::Chronometer meter)
        {
            auto pedal = Circuits::makeGermaniumFuzz();
            pedal.circuit.setSolverStrategy (strategy);
            pedal.circuit.prepare (benchmarkSampleRate);
            auto block = makeBlock (0.2f);

            std::vector<float> scratch (blockSize);

            meter.measure ([&] (int) {
                std::copy (block.begin(), block.end(), scratch.begin());
                pedal.circuit.process (scratch.data(), blockSize);
                return scratch[0];
            });
        };
    }

    // Valves. The triode stage is the shape a preamp is built from -- repeat it
    // four times for a high-gain channel and multiply accordingly.
    BENCHMARK_ADVANCED ("12AX7 triode stage, 512 samples (n=6, m=2)")
    (Catch::Benchmark::Chronometer meter)
    {
        auto triode = Circuits::makeTriodeStage();
        auto& stage = triode.circuit;
        stage.prepare (benchmarkSampleRate);
        auto block = makeBlock (0.5f);

        std::vector<float> scratch (blockSize);

        meter.measure ([&] (int) {
            std::copy (block.begin(), block.end(), scratch.begin());
            stage.process (scratch.data(), blockSize);
            return scratch[0];
        });
    };

    BENCHMARK_ADVANCED ("EL34 pentode stage, 512 samples (n=6, m=3)")
    (Catch::Benchmark::Chronometer meter)
    {
        auto stage = Circuits::makePentodeStage();
        stage.prepare (benchmarkSampleRate);
        auto block = makeBlock (5.0f);

        std::vector<float> scratch (blockSize);

        meter.measure ([&] (int) {
            std::copy (block.begin(), block.end(), scratch.begin());
            stage.process (scratch.data(), blockSize);
            return scratch[0];
        });
    };

    BENCHMARK_ADVANCED ("Valve rectifier supply, 512 samples (n=5, m=2)")
    (Catch::Benchmark::Chronometer meter)
    {
        auto psu = Circuits::makeValveRectifierSupply();
        psu.prepare (benchmarkSampleRate);

        std::vector<float> scratch (blockSize, 0.0f);

        meter.measure ([&] (int) {
            psu.process (scratch.data(), blockSize);
            return scratch[0];
        });
    };

    // The circuit the plugin actually ships, so this is the number that decides
    // whether it is comfortable to use. Two op-amps, each contributing a pair of
    // rail-clamp diodes, plus the antiparallel clipping pair -- six ports.
    //
    // Measured at two levels because the cost is level-dependent: clean, the
    // clipping diodes barely conduct and Newton settles almost immediately; hard
    // driven, they are the whole signal path.
    for (const float driveLevel : {0.02f, 0.5f})
    {
        const auto* label = driveLevel < 0.1f
                              ? "Mid-hump overdrive, 512 samples (clean)"
                              : "Mid-hump overdrive, 512 samples (driven into clipping)";

        BENCHMARK_ADVANCED (label)
        (Catch::Benchmark::Chronometer meter)
        {
            auto pedal = Circuits::makeMidHumpOverdrive();
            pedal.circuit.prepare (benchmarkSampleRate);
            auto block = makeBlock (driveLevel);

            std::vector<float> scratch (blockSize);

            meter.measure ([&] (int) {
                std::copy (block.begin(), block.end(), scratch.begin());
                pedal.circuit.process (scratch.data(), blockSize);
                return scratch[0];
            });
        };
    }

    // The largest circuit here: three valves, six ports, nineteen unknowns.
    BENCHMARK_ADVANCED ("Three-stage valve preamp, 512 samples (n=19, m=6)")
    (Catch::Benchmark::Chronometer meter)
    {
        auto amp = Circuits::makeThreeStagePreamp();
        amp.circuit.prepare (benchmarkSampleRate);
        auto block = makeBlock (0.05f);

        std::vector<float> scratch (blockSize);

        meter.measure ([&] (int) {
            std::copy (block.begin(), block.end(), scratch.begin());
            amp.circuit.process (scratch.data(), blockSize);
            return scratch[0];
        });
    };
}

//==============================================================================
// The drawing
//
// Painting the sheet is the one thing that happens tens of times a second while
// someone is working, and unlike the solver it had never been measured. A
// repaint costing a millisecond is invisible; costing ten is a canvas that
// stutters as you drag a part across it.
//
// Two sheet sizes, because the interesting question is how the cost *scales*:
// per-element work shows up in the first, anything quadratic in the second.
//==============================================================================
TEST_CASE ("Drawing performance")
{
    using namespace SchematicModel;
    using namespace SchematicUI;

    // A sheet of the shape someone actually builds: parts in rows, wired along
    // each row, so there are junctions and nets to work out as well as symbols.
    auto makeSheet = [] (int parts)
    {
        Schematic sheet;
        std::vector<int> ids;

        const ElementType cycle[] = { ElementType::Resistor, ElementType::Capacitor,
                                     ElementType::Diode, ElementType::Triode,
                                     ElementType::Transistor, ElementType::Potentiometer };

        int x = 0, y = 0;

        for (int i = 0; i < parts; ++i)
        {
            const auto id = sheet.addElement (cycle[i % 6], x, y);
            sheet.findElement (id)->value = 1000.0;
            ids.push_back (id);

            if (ids.size() >= 2)
                sheet.addWire (sheet.findElement (ids[ids.size() - 2])->getPinPosition (1),
                               sheet.findElement (id)->getPinPosition (0));

            x += 12;

            if (x > 240) { x = 0; y += 16; }
        }

        return sheet;
    };

    for (const int parts : { 50, 400 })
    {
        auto sheet = makeSheet (parts);

        SchematicCanvas canvas { sheet };
        canvas.setBounds (0, 0, 1200, 800);

        juce::Image image (juce::Image::ARGB, 1200, 800, true);

        BENCHMARK ("Canvas repaint, " + std::to_string (parts) + " parts")
        {
            juce::Graphics g (image);
            canvas.paintEntireComponent (g, false);
            return canvas.getWidth();
        };

        BENCHMARK ("Net extraction, " + std::to_string (parts) + " parts")
        {
            return sheet.extractNets().netNames.size();
        };

        BENCHMARK ("Junction search, " + std::to_string (parts) + " parts")
        {
            return sheet.findJunctions().size();
        };
    }
}
