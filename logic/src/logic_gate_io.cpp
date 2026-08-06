#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cassert>
#include <cmath>
using namespace std;
#include "logic_gate.h"
#include "logic_circuit.h"

// ******************************** CLOCK GATE ***********************************


// Initialize the half cycle:
Gate_CLOCK::Gate_CLOCK( TimeType newHalfCycle ) : Gate(), halfCycle(newHalfCycle) {
	theState = ZERO;
	
	// Declare the output:
	declareOutput("CLK");
}


// Handle gate events:
void Gate_CLOCK::gateProcess( void ) {
	
	TimeType now = getSimTime();
	
	if( (halfCycle > 0) && ( now % halfCycle == 0 ) ) {
		if( theState == ZERO ) theState = ONE;
		else theState = ZERO;
	}

	setOutputState( "CLK", theState, 0 );
}


// Set the clock rate:
bool Gate_CLOCK::setParameter( string paramName, string value ) {
	istringstream iss(value);
	if( paramName == "HALF_CYCLE" ) {
		iss >> halfCycle;
		return false;
	} else {
		return Gate::setParameter( paramName, value );
	}
}


// Get the clock rate:
string Gate_CLOCK::getParameter( string paramName ) {
	ostringstream oss;
	if( paramName == "HALF_CYCLE" ) {
		oss << halfCycle;
		return oss.str();
	} else {
		return Gate::getParameter( paramName );
	}
}


// **************************** END CLOCK GATE ***********************************


// ******************************** Pulse GATE ***********************************
// The PULSE gate simply creates a pulse of a specified duration
// in simulation steps. By setting the parameter PULSE, it sets the
// remaining duration of the pulse. Once the duration expires, the
// output will return to 0. If a pulse is still going when another
// PULSE parameter is sent, then the pulse is extended to the normal
// end time of the last pulse.
//NOTE: This is a "polled" gate, so it will always be checked, just
// like Gate_CLOCK.

Gate_PULSE::Gate_PULSE() : Gate() {
	pulseRemaining = 0;
	
	// Declare the output:
	declareOutput("OUT_0");
}


// Handle gate events:
void Gate_PULSE::gateProcess( void ) {
	// The output is ONE if there is pulse remaining, and ZERO otherwise:
	setOutputState( "OUT_0", (pulseRemaining > 0) ? ONE : ZERO, 0 );

	// Decrement the remaining number of steps that the pulse is high.
	if( pulseRemaining != 0 ) pulseRemaining--;
}


// Set the pulses:
bool Gate_PULSE::setParameter( string paramName, string value ) {
	istringstream iss(value);
	if( paramName == "PULSE" ) {
		iss >> pulseRemaining;
		return false; // It's a polled gate, so don't update it otherwise or the pulse count will be wrong.
	} else {
		return Gate::setParameter( paramName, value );
	}
}

// **************************** END Pulse GATE ***********************************


// ******************************** Driver GATE ***********************************
// Can be used to drive a bus of n bits to a specific binary number.

// Initialize the starting state and the output:
Gate_DRIVER::Gate_DRIVER() : Gate() {
	// The default output number is 0:
	output_num = 0;

	// Default of 0 outputs (Must be specified in library file, or no inputs will be made!):
	setParameter("OUTPUT_BITS", "0");
}


// Handle gate events:
void Gate_DRIVER::gateProcess( void ) {
	// All the driver gate does is throw events IMMEDIATELY
	// whenever the gate has changed state:
	setOutputBusState( "OUT", ulong_to_bus(output_num, outBits), 0 );
}


// Set the toggle state variable:
bool Gate_DRIVER::setParameter( string paramName, string value ) {
	istringstream iss(value);
	if( paramName == "OUTPUT_NUM" ) {
		iss >> output_num;
		return true; // Update the gate during the next step!
	} else if( paramName == "OUTPUT_BITS" ) {
		iss >> outBits;

		// Declare the output pins!		
		if( outBits > 0 ) {
			declareOutputBus( "OUT", outBits );
		}

		//NOTE: Don't return "true" from this, because
		// you shouldn't be setting this param during simulation while
		// anything is connected anyhow!
	} else {
		return Gate::setParameter( paramName, value );
	}
	return false;
}


// Get the toggle state variable:
string Gate_DRIVER::getParameter( string paramName ) {
	ostringstream oss;
	if( paramName == "OUTPUT_NUM" ) {
		oss << output_num;
		return oss.str();
	} else if( paramName == "OUTPUT_BITS" ) {
		oss << outBits;
		return oss.str();
	} else {
		return Gate::getParameter( paramName );
	}
}


// **************************** END Driver GATE ***********************************


