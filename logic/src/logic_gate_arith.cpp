#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cassert>
#include <cmath>
using namespace std;
#include "logic_gate.h"
#include "logic_circuit.h"

// ******************************** Full Adder GATE ***********************************
// Performs an addition of two input busses. Assumes that unknown-type inputs are
// all ZEROs.
// NOTE: Will work with busses of up to 32-bits.

Gate_ADDER::Gate_ADDER() : Gate_PASS() {
	// Declare the inputs:
	declareInput("carry_in");

	// (Load input bus and the output bus are declared by Gate_PASS and in setParams.):
	setParameter("INPUT_BITS", "0");

	// The outputs:
	declareOutput("carry_out");
	declareOutput("overflow");
}


// Handle gate events:
void Gate_ADDER::gateProcess( void ) {
	vector< StateType > inBusA = getInputBusState("IN");
	unsigned long inA = bus_to_ulong( inBusA );

	vector< StateType > inBusB = getInputBusState("IN_B");
	unsigned long inB = bus_to_ulong( inBusB );
	
	// Do the addition:
	unsigned long sum = inA + inB;

	// Add in the carry bit:
	if( getInputState("carry_in") == ONE ) sum++;

	// Convert the sum back to binary (with an extra bit):
	vector< StateType > preOutBus = ulong_to_bus( sum, inBits + 1 );
	vector< StateType > outBus = ulong_to_bus( sum, inBits );

	// Decide if there was a carry output:
	StateType carryOut = preOutBus[inBits];
	if( inBits >= 32 ) {
		// Fix the carry out if we are using 32-bit arithmetic:
		unsigned long long longA = inA;
		unsigned long long longB = inB;
		unsigned long long sum = longA + longB;
		if( sum > 0xFFFFFFFF ) {
			carryOut = ONE;
		}
	}

	// Determine overflow:
	StateType overflow = UNKNOWN;
	StateType lastBitA = (inBusA[inBits-1] == ONE) ? ONE : ZERO;
	StateType lastBitB = (inBusB[inBits-1] == ONE) ? ONE : ZERO;
	StateType lastBitSum = (preOutBus[inBits-1] == ONE) ? ONE : ZERO;
	if( lastBitA != lastBitB ) {
		// Differing input signs. No overflow:
		overflow = ZERO;
	} else {
		if(lastBitSum != lastBitA) {
			// Same input signs, yet different output sign. Overflow!
			overflow = ONE;
		} else {
			overflow = ZERO;
		}
	}

	// Set the output values:
	setOutputState("carry_out", carryOut);
	setOutputState("overflow", overflow);
	setOutputBusState("OUT", outBus);
}


// Set the parameters:
bool Gate_ADDER::setParameter( string paramName, string value ) {
	istringstream iss(value);
	if( paramName == "INPUT_BITS" ) {
		iss >> inBits;

		// Declare the second input pins:
		if( inBits > 0 ) {
			declareInputBus( "IN_B", inBits );
		}

		//NOTE: Don't return "true" from this, because
		// you shouldn't be setting this param during simulation while
		// anything is connected anyhow!
		// Also, allow the Gate_PASS class to change the number of inputs:
		return Gate_PASS::setParameter( paramName, value );
	} else {
		return Gate_PASS::setParameter( paramName, value );
	}
	return false;
}


// **************************** END Adder GATE ***********************************


// ******************* Magnitude Comparator Gate *********************
// Compares two input busses by magnitude.
// Outputs "A==B", "A<B", "A>B" depending on results.
// Has "A==B", "A<B", "A>B" inputs to allow cascading comparators.
// "A==B" input defaults HIGH with an unknown input, but all others default LOW.

Gate_COMPARE::Gate_COMPARE() : Gate_N_INPUT() {
	// Declare the inputs:
	declareInput("in_A_equal_B");
	declareInput("in_A_greater_B");
	declareInput("in_A_less_B");

	// Input busses are declared by Gate_N_INPUT and in setParams():
	setParameter("INPUT_BITS", "0");

	// The outputs:
	declareOutput("A_equal_B");
	declareOutput("A_greater_B");
	declareOutput("A_less_B");
}


// Handle gate events:
void Gate_COMPARE::gateProcess( void ) {
	unsigned long inA = bus_to_ulong( getInputBusState("IN") );
	unsigned long inB = bus_to_ulong( getInputBusState("IN_B") );

	StateType equal = ZERO;
	StateType less = ZERO;
	StateType greater = ZERO;

	if( inA == inB ) {
		if( getInputState("in_A_greater_B") == ONE ) {
			greater = ONE;
		} else if( getInputState("in_A_less_B") == ONE ) {
			less = ONE;
		} else if( getInputState("in_A_equal_B") != ZERO ) {
			equal = ONE;
		}
	} else if( inA < inB ) {
		less = ONE;
	} else if( inA > inB ) {
		greater = ONE;
	}
	
	// Set the output values:
	setOutputState("A_equal_B", equal);
	setOutputState("A_less_B", less);
	setOutputState("A_greater_B", greater);
}


// Set the parameters:
bool Gate_COMPARE::setParameter( string paramName, string value ) {
	istringstream iss(value);
	if( paramName == "INPUT_BITS" ) {
		iss >> inBits;

		// Declare the second input pins:
		if( inBits > 0 ) {
			declareInputBus( "IN_B", inBits );
		}

		//NOTE: Don't return "true" from this, because
		// you shouldn't be setting this param during simulation while
		// anything is connected anyhow!
		// Also, allow the Gate_N_INPUT class to change the number of inputs:
		return Gate_N_INPUT::setParameter( paramName, value );
	} else {
		return Gate_N_INPUT::setParameter( paramName, value );
	}
	return false;
}


// **************************** END Comparator GATE ***********************************


