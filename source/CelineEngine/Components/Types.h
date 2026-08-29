#pragma once

// Forward declaration only: Potentiometer::setPosition() calls back into
// Circuit, and nothing else here does. Keeps the component headers free of any
// dependency on the engine or on JUCE.
class Circuit;

//==============================================================================
/**
    Shared aliases for everything in the Components folder.

    Component values and the solve are double throughout; only the audio in and
    out of Circuit is float. See Solver.h for the dynamic range a circuit matrix
    spans.
*/
namespace CircuitComponents
{
    /** Index of a node in the netlist. Node 0 is always ground. */
    using NodeIndex = int;

    /** Handle to a component, returned by Circuit's add* functions. Ids are
        per-component-type, not global. */
    using ComponentId = int;
} // namespace CircuitComponents
