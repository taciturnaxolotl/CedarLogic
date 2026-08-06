/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   
*****************************************************************************/

// logic_gate.cpp: implementation of the Gate class.
//
//////////////////////////////////////////////////////////////////////

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cassert>
#include <cmath>
using namespace std;
#include "logic_gate.h"
#include "logic_circuit.h"

const std::map<std::string, GateType> &gateRegistry() {
	static const std::map<std::string, GateType> registry = {
		{ "AND",         { [](Circuit *)  { return GATE_PTR(new Gate_AND); },         false } },
		{ "OR",          { [](Circuit *)  { return GATE_PTR(new Gate_OR); },          false } },
		{ "XOR",         { [](Circuit *)  { return GATE_PTR(new Gate_XOR); },         false } },
		{ "BUFFER",      { [](Circuit *)  { return GATE_PTR(new Gate_PASS); },        false } },
		{ "MUX",         { [](Circuit *)  { return GATE_PTR(new Gate_MUX); },         false } },
		{ "DECODER",     { [](Circuit *)  { return GATE_PTR(new Gate_DECODER); },     false } },
		{ "PRI_ENCODER", { [](Circuit *)  { return GATE_PTR(new Gate_PRI_ENCODER); }, false } },
		{ "BUS_END",     { [](Circuit *c) { return GATE_PTR(new Gate_BUS_END(c)); },  false } },
		{ "CLOCK",       { [](Circuit *)  { return GATE_PTR(new Gate_CLOCK); },       true  } },
		{ "PULSE",       { [](Circuit *)  { return GATE_PTR(new Gate_PULSE); },       true  } },
		{ "DRIVER",      { [](Circuit *)  { return GATE_PTR(new Gate_DRIVER); },      false } },
		{ "ADDER",       { [](Circuit *)  { return GATE_PTR(new Gate_ADDER); },       false } },
		{ "COMPARE",     { [](Circuit *)  { return GATE_PTR(new Gate_COMPARE); },     false } },
		{ "JKFF",        { [](Circuit *)  { return GATE_PTR(new Gate_JKFF); },        false } },
		{ "RAM",         { [](Circuit *)  { return GATE_PTR(new Gate_RAM); },         false } },
		{ "REGISTER",    { [](Circuit *)  { return GATE_PTR(new Gate_REGISTER); },    false } },
		{ "FROM",        { [](Circuit *c) { return GATE_PTR(new Gate_JUNCTION(c)); }, false } },
		{ "TO",          { [](Circuit *c) { return GATE_PTR(new Gate_JUNCTION(c)); }, false } },
		{ "TGATE",       { [](Circuit *c) { return GATE_PTR(new Gate_T(c)); },        false } },
		{ "NODE",        { [](Circuit *c) { return GATE_PTR(new Gate_NODE(c)); },     false } },
		{ "EQUIVALENCE", { [](Circuit *)  { return GATE_PTR(new Gate_EQUIVALENCE); }, false } },
		{ "Pauseulator", { [](Circuit *)  { return GATE_PTR(new Gate_pauseulator()); }, false } },
	};
	return registry;
}

// ***************************** GENERIC GATE ***********************************


Gate::Gate()
{
	ourCircuit = NULL;
	defaultDelay = DEFAULT_GATE_DELAY;
	myID = ID_NONE;
	
	// Declare default ENABLE pins, so that any gate can
	// link them to its outputs:
	declareInputBus("ENABLE", 8);
/*	declareInput("ENABLE_0");
	declareInput("ENABLE_1");
	declareInput("ENABLE_2");
	declareInput("ENABLE_3");

	declareInput("ENABLE_4");
	declareInput("ENABLE_5");
	declareInput("ENABLE_6");
	declareInput("ENABLE_7");
*/

}


Gate::~Gate()
{

}


// Update the gate's outputs:
void Gate::updateGate( IDType myID, Circuit * theCircuit )
{
	// Store the Circuit variable in the gate to be used during this call to updateGate():
	ourCircuit = theCircuit;
	this->myID = myID;
	
	//******************************************
	//Edit by Joshua Lansford 4/22/07
	//This goes ahead and lists all paramiters
	//that wanted to be listed betwean updateGate
	//class and couldn't
	for( vector<string>::iterator I = changedParamWaitingList.begin();
	    	I != changedParamWaitingList.end(); ++I ){
		listChangedParam( *I );
	}
	changedParamWaitingList.clear();
	//*******************************************
	
	// Call the subclassed gate's function to process the events for this gate:
	this->gateProcess();

	// Handle the enabled/disabled outputs:
	ID_MAP< string, GateOutput >::iterator theOutput = outputList.begin();
	while( theOutput != outputList.end() ) {
		string enableIn = (theOutput->second).enableInput;
		if( enableIn != "" ) {
			// If the enable pin is NOT set to 0, then it is enabled!
			// (Interprets HI_Z, CONFLICT, and UNKNOWN as 1.)
			if( getInputState( enableIn ) == ZERO ) {
				setOutputState( theOutput->first, HI_Z );
			}
		}
		theOutput++;
	}
		

	// Update the last state of the edge-triggered inputs:
	ID_SET< string >::iterator eInputs = edgeTriggeredInputs.begin();
	while( eInputs != edgeTriggeredInputs.end() ) {
		edgeTriggeredLastState[ *eInputs ] = getInputState( *eInputs );
		eInputs++;
	}
	
	// Invalidate the circuit pointer, because we are done with it:
	ourCircuit = NULL;
	
	return;
}


// Resend the last event to a (probably newly connected) wire:	
void Gate::resendLastEvent( IDType myID, string outputID, Circuit * theCircuit ) {
	if( outputList.find( outputID ) != outputList.end() ) {
		// If a wire is connected now, and there has been a previous event on this gate, then re-send it to the new wire:
		if( ( outputList[outputID].wireID != ID_NONE ) && ( outputList[outputID].lastEventTime != TIME_NONE ) ) {
			// Re-create the event!
			theCircuit->createEvent(outputList[outputID].lastEventTime, outputList[outputID].wireID, myID, outputID, outputList[outputID].lastEventState );
		}
	} else {
		WARNING("Gate::resendLastEvent() - Invalid outputID.");
	}
}


// Connect a wire to the input of this gate:
void Gate::connectInput( string inputID, IDType wireID )
{
	this->inputList[inputID].wireID = wireID;
}


// Connect a wire to the output of this gate:
void Gate::connectOutput( string outputID, IDType wireID )
{
	GateOutput myOut;
	
	// If there was already an output connected on this gate, then
	// copy the old event states over to the new connection. This is because
	// the new wire will need the last event re-sent to it so that it will
	// be activated correctly.
	if( outputList.find( outputID ) != outputList.end() ) {
		myOut = outputList[outputID];
	}

	// Hook up the wire:
	myOut.wireID = wireID;

	// Save our new output structure in the real output list:
	this->outputList[outputID] = myOut;
}


// Disconnect a wire from the input of this gate:
// (Returns the wireID of the wire that was connected.)
IDType Gate::disconnectInput( string inputID ) {
	IDType wireID = getInputWire( inputID );
	if( wireID != ID_NONE ) {
		// Disconnect the input, but don't remove the connection.
		// (The inverted state and other info must stay.)
		inputList[inputID].wireID = ID_NONE;
	} else {
		WARNING("Gate::disconnectInput() - Invalid input ID.");
	}
	return wireID;
}


// Disconnect a wire from the output of this gate:
// (Returns the wireID of the wire that was connected.)
IDType Gate::disconnectOutput( string outputID ) {
	IDType wireID = getOutputWire( outputID );
	if( wireID != ID_NONE ) {
		// Leave the output there, because it has "last state" info
		// even if a wire is not connected currently!
		outputList[outputID].wireID = ID_NONE;
	} else {
		WARNING("Gate::disconnectOutput() - Invalid output ID.");
	}
	return wireID;
}


// Get the first input of the gate that has a wire attached to it:
string Gate::getFirstConnectedInput( void ) {
	if( !inputList.empty() ) {
		ID_MAP< string, GateInput >::iterator inP = inputList.begin();
		while(inP != inputList.end()) {
			if((inP->second).wireID != ID_NONE) {
				return inP->first;
			}
			inP++;
		}
	}
	
	return "";
}


// Get the first output of the gate that has a wire attached to it:
string Gate::getFirstConnectedOutput( void ) {
	if( !outputList.empty() ) {
		ID_MAP< string, GateOutput >::iterator outP = outputList.begin();
		while(outP != outputList.end()) {
			if((outP->second).wireID != ID_NONE) {
				return outP->first;
			}
			outP++;
		}
	}
	
	// If there are none, then return ID_NONE.
	return "";
}


// Set a gate parameter:
bool Gate::setParameter( string paramName, string value ) {
	istringstream iss(value);
	if( paramName == "DEFAULT_DELAY" ) {
		iss >> defaultDelay;
		return false;
	} else {
		WARNING("Gate::setParameter() - Invalid parameter.");
		return false;
	}
}

bool Gate::setInputParameter( string inputID, string paramName, string value ) {
	istringstream iss(value);
	string temp;
	if( paramName == "INVERTED" ) {
		iss >> temp;
		// Set the input inverted state:
		setInputInverted( inputID, (temp == "TRUE"));
		return true;
	} else {
		WARNING("Gate::setInputParameter() - Invalid parameter.");
		return false;
	}
}

bool Gate::setOutputParameter( string outputID, string paramName, string value ) {
	istringstream iss(value);
	string temp;
	if( paramName == "INVERTED" ) {
		iss >> temp;
		// Set the input inverted state:
		setOutputInverted( outputID, (temp == "TRUE"));
		return true;
	} else if( paramName == "E_INPUT" ) {
		iss >> temp;
		// Set the input inverted state:
		setOutputEnablePin( outputID, temp );
		return true;
	} else {
		WARNING("Gate::setOutputParameter() - Invalid parameter.");
		return false;
	}
}


// Get the value of a gate parameter:
string Gate::getParameter( string paramName ) {
	ostringstream oss;
	if( paramName == "DEFAULT_DELAY" ) {
		oss << defaultDelay;
	} else {
		WARNING("Gate::getParameter() - Invalid parameter.");
	}
	return oss.str();
}


vector<ParamDescriptor> Gate::paramSchema() const {
	return { { "DEFAULT_DELAY", ParamKind::INT } };
}


// ******************** Gate Subclass Use Methods **********************************
// These are used by the subclassed gate types to define what interface and
// process each gate posesses.


// **** Gate "Entity" declaration methods:

// Register an input for this gate:
// Possibly declare the input as edge triggered, which will cause it
// to be tracked to be able to check rising and falling edges.
void Gate::declareInput( string inputID, bool edgeTriggered ) {
	// Touch the item in the list, to make sure that it is created:
	this->inputList[inputID].wireID = ID_NONE;

	if( edgeTriggered ) {
		edgeTriggeredInputs.insert( inputID );
		
		// NOTE: We don't set a last state here, because we don't want
		// the first event to come along to cause a rising or falling edge.
		// The first event to come along (i.e. there is no "last state" information)
		// will not register as either edge.
		// Not: edgeTriggeredLastState[name] = UNKNOWN;
	}
}

// Register an output for this gate:
void Gate::declareOutput( string name ) {
	outputList[ name ].wireID = ID_NONE;
	outputList[ name ].lastEventState = HI_Z; // The GUI assumes HI_Z for all wires to begin with.
	outputList[ name ].lastEventTime = TIME_NONE;
}

// **** Gate "Process" activity methods:
// Note: All of these depend on the ourCircuit pointer to be non-null, and a valid
// myID value, so they can only be called during a call to updateGate().

// Get the current time in the simulation:
TimeType Gate::getSimTime( void ) {
	assert(ourCircuit != NULL);
	
	return ourCircuit->getSystemTime();
}
	
// Check the state of the named input and return it.
StateType Gate::getInputState( string inputID ) {
	assert(ourCircuit != NULL);

	if(inputList.find(inputID) == inputList.end()) {
		WARNING("Gate::getInputState() - Invalid input name.");
		assert( false );
		return ZERO;
	}

	// If the input is connected, get the input value:
	if( inputList[inputID].wireID != ID_NONE ) {
		StateType theState = ourCircuit->getWireState( inputList[inputID].wireID );
		
		// Invert the input if it is set as inverted:
		if( inputList[inputID].inverted ) {
			if( theState == ZERO ) theState = ONE;
			else if( theState == ONE ) theState = ZERO;
		}
		
		return theState;
	} else {
		// If the input is not connected, then return
		// high-impedance as the "value" for the input.
		return HI_Z;
	}
}

// Get the input states of a bus of inputs named "busName_0" through
// "busName_x" and return their states as a vector.
vector< StateType > Gate::getInputBusState( string busName ) {
	unsigned long BUS_MAX_WIDTH = 10000;
	ostringstream pinName;
	vector< StateType > inStates;
	unsigned long i = 0;
	do {
		pinName.str("");
		pinName.clear();
		pinName << busName << "_" << i;
		if( inputExists( pinName.str() ) ) {
			inStates.push_back( getInputState( pinName.str() ) );
		}
		i++;
	} while( (inputExists( pinName.str() )) && (i < BUS_MAX_WIDTH) );
	return inStates;
}


// Get the types of inputs that are represented.
vector< bool > Gate::groupInputStates( void ) {
	assert(ourCircuit != NULL);

	vector< bool > groupedInputs(NUM_STATES, false);

	ID_MAP< string, GateInput >::iterator inputs = inputList.begin();
	while( inputs != inputList.end() ) {

		// Note: Only add the input into the tally if it is connected!
		if( (inputs->second).wireID != ID_NONE ) {
			StateType theState = ourCircuit->getWireState( (inputs->second).wireID );
			groupedInputs[theState] = true;
		}

		inputs++;
	}
	
	return groupedInputs;
}

	
// Compare the "this" state with the "last" state and say if this is a rising or falling edge. 
bool Gate::isRisingEdge( string name ) {
	assert(ourCircuit != NULL);
	
	if( edgeTriggeredLastState.find( name ) == edgeTriggeredLastState.end() ) {
		// There can be no rising edge on the first time that the gate is simulated!
		return false;
	}
	
	StateType last = edgeTriggeredLastState[ name ];
	StateType now = getInputState( name );

	if( ( now == ONE ) && (last != ONE) ) {
		return true;
	} else {
		return false;
	}
}


bool Gate::isFallingEdge( string name ) {
	assert(ourCircuit != NULL);
	
	if( edgeTriggeredLastState.find( name ) == edgeTriggeredLastState.end() ) {
		// There can be no rising edge on the first time that the gate is simulated!
		return false;
	}
	
	StateType last = edgeTriggeredLastState[ name ];
	StateType now = getInputState( name );

	if( ( now == ZERO ) && (last != ZERO) ) {
		return true;
	} else {
		return false;
	}
}

// Send an output event to one of the outputs of this gate. 
// Compare the last sent event with the newState and decide whether or not to 
// really send the event. Also, log the last sent event so that it can be 
// repeated later if necessary. 
void Gate::setOutputState( string outID, StateType newState, TimeType delay ) {
	
	assert( ourCircuit != NULL );

	if(outputList.find(outID) == outputList.end()) {
		WARNING("Gate::setOutputState() - Invalid output name.");
		assert( false );
		return;
	}
	
	if( delay == TIME_NONE ) {
		delay = defaultDelay;
	}

	// The event variables for the event to be thrown:
	TimeType eTime = getSimTime() + delay;
	IDType eWire = outputList[outID].wireID;

	// Set the output state (if the output is inverted, then invert it first):
	StateType eState;
	if( outputList[outID].inverted ) {
		if( newState == ONE ) {
			eState = ZERO;
		} else if( newState == ZERO ) {
			eState = ONE;
		} else {
			eState = newState;
		}
	} else {
		eState = newState;
	}

	if( outputList[outID].enableInput != "" ) {
		// If the enable pin is NOT set to 0, then it is enabled!
		// (Interprets HI_Z, CONFLICT, and UNKNOWN as 1.)
		if( getInputState( outputList[outID].enableInput ) == ZERO ) {
			eState = HI_Z;
		}
	}

	// If the state has changed, then we are interested in this event:
	if( eState != outputList[outID].lastEventState ) {

		// If we have a wire connected, then send the event:
		if( eWire != ID_NONE ) {
			ourCircuit->createEvent( eTime, eWire, myID, outID, eState );
		}
		
		// Store the last-state information to prevent duplicate events,
		// and in case a wire is connected to this output and the event
		// needs to be re-sent:
		outputList[outID].lastEventState = eState;
		outputList[outID].lastEventTime = eTime;
	}
}

	
// Set the output states of a bus of outputs named "busName_0" through
// "busName_x" using a vector of states:
void Gate::setOutputBusState( string outID, vector< StateType > newState, TimeType delay ) {
	ostringstream pinName;
	for( unsigned long i = 0; i < newState.size(); i++ ) {
		pinName.str("");
		pinName.clear();
		pinName << outID << "_" << i;
		setOutputState( pinName.str(), newState[i], delay);
	}
}


// List a parameter in the Circuit as having been changed:
void Gate::listChangedParam( string paramName ) {
	//*************************************
	//Edit by Joshua Lansford 4/22/07
	//I am changeing it so that instead of
	//asserting that ourCircuit != NULL,
	//we will just stach paramiters to pass
	//later instead of just forbidding them.
	//This way pop-ups can be updated imediatly
	//instead of waiting until next gate step
	
	if(ourCircuit != NULL){
	
		// Send the update param to the Circuit:
		ourCircuit->addUpdateParam( this->myID, paramName );
		
	}else{
		changedParamWaitingList.push_back( paramName );	
	}
	
}



// A helper function that allows you to convert a bus into a unsigned long:
// (HI_Z, etc. is interpreted as ZERO.)
unsigned long Gate::bus_to_ulong( vector< StateType > busStates ) {
	unsigned long theNumber = 0;

	// Loop from MSB to LSB:
	for( long i = (busStates.size() - 1); i >= 0; i-- ) {
		// Bit-shift to put the number in the right position:
		theNumber <<= 1;

		// Add a 1 into the number if the bus has one:
		if( busStates[i] == ONE ) {
			theNumber++;
		}
	}

	return theNumber;
}

// A helper function that allows you to convert an unsigned long number into a bus:
vector< StateType > Gate::ulong_to_bus( unsigned long number, unsigned long numBits ) {
	vector< StateType > theBus;
	unsigned long mask = 1;
	
	for( unsigned int i = 0; i < numBits; i++ ) {

		// If that bit of the number is turned on,
		// then output a ONE. Else ZERO:
		if( mask & number ) {
			theBus.push_back( ONE );
		} else {
			theBus.push_back( ZERO );
		}

		// Shift the mask to the next bit:
		mask <<= 1;
	}

	return theBus;
}


// ************************* END GENERIC GATE ***********************************



// ******************************** N-Input GATE ***********************************
// This gate type has no logic inside of it, but it will declare and manage
// the N inputs. This is good for gates of a similar shape.

// Initialize the gate's interface:
Gate_N_INPUT::Gate_N_INPUT() : Gate() {
	// Default of 0 inputs (Must be specified in library file, or no inputs will be made!):
	setParameter("INPUT_BITS", "0");
}


// Set the parameters:
bool Gate_N_INPUT::setParameter( string paramName, string value ) {
	istringstream iss(value);
	if( paramName == "INPUT_BITS" ) {
		iss >> inBits;

		// Declare the address pins!		
		if( inBits > 0 ) {
			declareInputBus( "IN", inBits );
		}

		//NOTE: Don't return "true" from this, because
		// you shouldn't be setting this param during simulation while
		// anything is connected anyhow!
	} else {
		return Gate::setParameter( paramName, value );
	}
	return false;
}


// Set the parameters:
string Gate_N_INPUT::getParameter( string paramName ) {
	ostringstream oss;
	if( paramName == "INPUT_BITS" ) {
		oss << inBits;
		return oss.str();
	} else {
		return Gate::getParameter( paramName );
	}
}


vector<ParamDescriptor> Gate_N_INPUT::paramSchema() const {
	auto schema = Gate::paramSchema();
	schema.push_back( { "INPUT_BITS", ParamKind::BITS } );
	return schema;
}

// **************************** END Gate_N_INPUT GATE ***********************************

// ******************************** PASS GATE ***********************************
// This gate simply takes all of the inputs and passes them to the outputs, like
// a buffer. It is useful for creating tri-state buffers and inverters.

// Initialize the gate's interface:
Gate_PASS::Gate_PASS() : Gate_N_INPUT() {
	// Inputs are declared in Gate_N_INPUT();

	// Outputs are declared in setParameter();
	// Default of 1 input (No need to set this in the library file for 1-input gates):
	setParameter("INPUT_BITS", "1");
};
	

// Handle gate events:
void Gate_PASS::gateProcess( void ) {
	// Get the status of all of the inputs:
	vector< StateType > inputStates = getInputBusState("IN");
	vector< StateType > outputStates(inBits, UNKNOWN);
	
	for( unsigned long i = 0; i < inBits; i++ ) {
		// If we have a ONE or ZERO, pass it through:
		if( ( inputStates[i] == ONE ) || ( inputStates[i] == ZERO ) ) {
			outputStates[i] = inputStates[i];
		}
	}

	setOutputBusState("OUT", outputStates);
};


// Set the parameters:
bool Gate_PASS::setParameter( string paramName, string value ) {
	istringstream iss(value);
	if( paramName == "INPUT_BITS" ) {
		iss >> inBits;

		// Declare the output pins:
		if( inBits > 0 ) {
			declareOutputBus( "OUT", inBits );
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


// **************************** END PASS GATE ***********************************


// ******************************** OR GATE ***********************************

// Initialize the gate's interface:
Gate_OR::Gate_OR() : Gate_N_INPUT() {
	//NOTE: Inputs are declared by Gate_N_INPUT()

	// Declare the output:
	declareOutput("OUT");
}

// Handle gate events:
void Gate_OR::gateProcess( void ) {
	// Get the status of all of the inputs:
	vector< StateType > inputStates = getInputBusState("IN");
	
	StateType outState = ZERO; // Assume that the output is ZERO first of all.
	for( unsigned long i = 0; i < inBits; i++ ) {
		if( inputStates[i] == ONE ) {
			outState = ONE;
			break; // A single ONE input will force the gate to ONE.
		} else if( inputStates[i] == ZERO ) {
			// A zero does nothing, since we assume zero first.
		} else { // HI_Z, CONFLICT, UNKNOWN
			outState = UNKNOWN;
		}
	}

	setOutputState("OUT", outState);
}

// **************************** END OR GATE ***********************************


// ******************************** AND GATE ***********************************

// Initialize the gate's interface:
Gate_AND::Gate_AND() : Gate_N_INPUT() {
	//NOTE: Inputs are declared by Gate_N_INPUT()

	// Declare the output:
	declareOutput("OUT");
}

// Handle gate events:
void Gate_AND::gateProcess( void ) {
	// Get the status of all of the inputs:
	vector< StateType > inputStates = getInputBusState("IN");
	
	StateType outState = ONE; // Assume that the output is ONE first of all.
	for( unsigned long i = 0; i < inBits; i++ ) {
		if( inputStates[i] == ZERO ) {
			outState = ZERO;
			break; // A single ZERO input will force the gate to ZERO.
		} else if( inputStates[i] == ONE ) {
			// A ONE does nothing, since we assume ONE first.
		} else { // HI_Z, CONFLICT, UNKNOWN
			outState = UNKNOWN;
		}
	}

	setOutputState("OUT", outState);
}

// **************************** END AND GATE ***********************************

// ******************************** AND GATE ***********************************

// Initialize the gate's interface:
Gate_EQUIVALENCE::Gate_EQUIVALENCE() : Gate_N_INPUT() {
	//NOTE: Inputs are declared by Gate_N_INPUT()

	// Declare the output:
	declareOutput("OUT");
}

// Handle gate events:
void Gate_EQUIVALENCE::gateProcess( void ) {
	// Get the status of all of the inputs:
	vector< StateType > inputStates = getInputBusState("IN");
	
	StateType outState;
	
	if( (inputStates[0] == ONE && inputStates[1] == ONE)
	  ||(inputStates[0] == ZERO && inputStates[1] == ZERO )){
		outState = ONE;
	}else if( (inputStates[0] == ZERO && inputStates[1] == ONE)
	  ||(inputStates[0] == ONE && inputStates[1] == ZERO )){
	  	outState = ZERO;
	}else{
		outState = UNKNOWN;
	}

	setOutputState("OUT", outState);
}

// **************************** END AND GATE ***********************************


// ******************************** XOR GATE ***********************************

// Initialize the gate's interface:
Gate_XOR::Gate_XOR() : Gate_N_INPUT() {
	//NOTE: Inputs are declared by Gate_N_INPUT()

	// Declare the output:
	declareOutput("OUT");
}

// Handle gate events:
void Gate_XOR::gateProcess( void ) {
	// Get the status of all of the inputs:
	vector< StateType > inputStates = getInputBusState("IN");

	// The XOR operation is basically a parity check.
	// XOR returns TRUE if there are an odd number of 1's.
	StateType outState = ZERO; // Assume ZERO for the output.
	unsigned long numOnes = 0;
	for( unsigned long i = 0; i < inBits; i++ ) {
		// Any unknown-type inputs will cause the output to be unknown:
		if( (inputStates[i] == HI_Z) || (inputStates[i] == CONFLICT) || (inputStates[i] == UNKNOWN) ) {
			outState = UNKNOWN;
			break;
		} else if( inputStates[i] == ONE ) {
			// Tally up the ones:
			numOnes++;
		}
	}
	
	if( outState != UNKNOWN ) {
		// If the number of ONES is odd, then return ONE:
		if( (numOnes % 2) != 0 ) {
			outState = ONE;
		}
	}

	setOutputState("OUT", outState);
}

// **************************** END XOR GATE ***********************************


