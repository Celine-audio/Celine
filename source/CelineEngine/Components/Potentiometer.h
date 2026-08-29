#pragma once

#include "Types.h"

namespace CircuitComponents
{
    //==========================================================================
    /**
        A potentiometer -- not a component the solver knows about, but a helper
        that drives one or two ordinary resistors from a single knob position.

        Supports 3-terminal potentiometers (pin1, wiper, pin3) used as voltage
        dividers, as well as 2-terminal variable resistors (rheostats).
    */
    struct Potentiometer
    {
        /** How the track's resistance follows the shaft.

            The first three are the smooth ones: linear, a square law, and that
            square law reflected. The rest are the numbered audio tapers, where
            the number is the percentage of the track the wiper has passed at
            half rotation -- the figure a pot's curve is actually specified by.

            Those follow a different law, and deliberately: a real audio pot is
            *made* of two resistive segments meeting at half rotation, so it is
            two straight lines rather than a curve, and the difference from a
            power law is largest exactly where it matters. A 10A pot reads about
            4% of its track one fifth of the way round; the power law that also
            passes through 10% at noon reads 0.5% there, which is the difference
            between a volume control that works at 1 and one that does not. */
        enum class Taper
        {
            Linear,
            Logarithmic, // Audio / Log taper -- a square law, 25% at midpoint
            ReverseLogarithmic,

            Audio5,      //  5% of the track at half rotation
            Audio10,
            Audio15,
            Audio20,
            Audio30
        };

        ComponentId upperResistor = -1;
        ComponentId lowerResistor = -1;
        double maxResistance = 1000000.0; // Total resistance in ohms (e.g., 1M, 250k, 25k)
        double minResistance = 1.0;       // Minimum resistance floor to prevent 0-ohm division by zero
        Taper taper = Taper::Linear;

        /** Sets the knob position between 0.0 (fully CCW) and 1.0 (fully CW).
            Updates the circuit's underlying resistor(s) according to position
            and taper. Implemented in Potentiometer.cpp. */
        void setPosition(Circuit& circuit, float position) const;
    };
} // namespace CircuitComponents
