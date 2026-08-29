#pragma once

//==============================================================================
/**
    Umbrella include for every component type. Engine.h pulls this in; you
    shouldn't need to include the individual headers yourself.

    Each component lives in its own header with its parameters, its device
    models and whatever maths is specific to it. None of them knows how to stamp
    itself into a matrix or solve anything -- that is Circuit's job -- so a new
    component type can be added without touching the solver.

    Linear -- constant conductance, so the factorisation can be cached:
        Resistor, Capacitor, Inductor, Vccs (a voltage-controlled current
        source), and two helpers that drive resistors rather than being
        components in their own right: Potentiometer (a knob) and Switch (a
        toggle)

    Nonlinear -- re-linearised on every Newton iteration, each described purely
    by its ports (see Ports.h):
        Diode, Bjt, Jfet        semiconductors (Junction.h); a Diode with a
                                breakdown voltage set is a Zener
        VacuumDiode, Triode,    valves, 3/2-power space charge (SpaceCharge.h)
        Pentode

    An op-amp is not in that list. It is assembled out of the parts above --
    resistors, a capacitor, two controlled sources and two diodes -- rather than
    being a device of its own. See OpAmp.h for why that matters.

    Neither -- states a constraint instead of describing a current, and so needs
    extra rows in the system rather than a conductance stamp:
        VoltageSource   fixes a voltage, its current becomes an unknown
        IdealOpAmp      fixes its inputs equal, its output current is an unknown
        Transformer     locks its windings' volts-per-turn together, one unknown
                        current per winding
*/

#include "Ports.h"
#include "Types.h"
#include "Vccs.h"

#include "Capacitor.h"
#include "Diode.h"
#include "IdealOpAmp.h"
#include "Inductor.h"
#include "Jfet.h"
#include "Junction.h"
#include "OpAmp.h"
#include "Pentode.h"
#include "Potentiometer.h"
#include "Resistor.h"
#include "SpaceCharge.h"
#include "Switch.h"
#include "Transformer.h"
#include "Transistor.h"
#include "Triode.h"
#include "VacuumDiode.h"
#include "VoltageSource.h"
