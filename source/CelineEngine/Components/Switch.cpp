#include "Switch.h"
#include "../Engine.h"

void CircuitComponents::Switch::setClosed(Circuit& circuit, bool closed) const
{
    if (resistor < 0)
        return;

    circuit.setResistance(resistor, closed ? closedOhms : openOhms);
}

void CircuitComponents::Changeover::select(Circuit& circuit, bool useThrowB) const
{
    throwA.setClosed(circuit, ! useThrowB);
    throwB.setClosed(circuit, useThrowB);
}
