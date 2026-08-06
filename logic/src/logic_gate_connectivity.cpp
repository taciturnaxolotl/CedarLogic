#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cassert>
#include <cmath>
using namespace std;
#include "logic_gate.h"
#include "logic_circuit.h"

// ******************************** Junction GATE ***********************************
// This class uses the circuit's Junctioning capabilities
// to enable and disable a junction and splice the inputs
// into the junction, to allow true to/from nodes.


// This is the mapping of junction states, and how often each is used (# of gates):
//ID_MAP< string, IDType > Gate_JUNCTION::junctionIDs;
//ID_MAP< string, unsigned long > Gate_JUNCTION::junctionUseCounter;


// Initialize the starting state and the output:
Gate_JUNCTION::Gate_JUNCTION( Circuit *newCircuit ) : Gate() {

	// Keep the circuit pointer, to use to access the Junctions
	myCircuit = newCircuit;

	// The only attributes that this gate has is what wires
	// are hooked up, and what its name is. Set the name
	// using "setParameter" so that the new Junction will be
	// created if needed:
	this->setParameter( "JUNCTION_ID", "NONE" );

/*	declareInput( "IN1", 0 );
	declareOutput( "OUT1", 0 );
	
	this->isFrom = isFrom;
*/
}


// Remove this junction's claim on the junction ID:
Gate_JUNCTION::~Gate_JUNCTION() {
	if( !((*(myCircuit->getJunctionIDs())).empty() ) && ((*(myCircuit->getJunctionIDs())).find( myID ) != (*(myCircuit->getJunctionIDs())).end()) ) {
		(*(myCircuit->getJunctionUseCounter()))[myID] -= 1;
		// Unhook this gate's wires from the old junction:
		ID_SET< IDType >::iterator thisWire = myWires.begin();
		while( thisWire != myWires.end() ) {
			myCircuit->disconnectJunction( (*(myCircuit->getJunctionIDs()))[myID], *thisWire );
			thisWire++;
		}

		// If that junction is no longer used, then erase it:
		if( (*(myCircuit->getJunctionUseCounter()))[myID] == 0 ) {
			// Erase the junction from the Circuit:
			myCircuit->deleteJunction( (*(myCircuit->getJunctionIDs()))[myID] );

			// Erase the junction from the junction maps:				
			(*(myCircuit->getJunctionIDs())).erase( myID );
			(*(myCircuit->getJunctionUseCounter())).erase( myID );
		}

	}
}


// Handle gate events:
void Gate_JUNCTION::gateProcess( void ) {
	// Do nothing, 'cause the Junction object does all the work for us!

/*
	if( isFrom ) {
		// All a "From" does is throw events as soon
		// as the junction has changed state:
		// NOTE: A "From" is a polled gate, but a "To" is not.
		StateType theState = UNKNOWN;
		if( junctionStates.find( myID ) != junctionStates.end() ) {
			theState = junctionStates[myID];
		}
		setOutputState( "OUT1", theState, 0 );

	} else {
		// All a "To" does is update the state of the junction:
		junctionStates[myID] = getInputState("IN1");
	}
*/
}


// Set the junction's ID:
bool Gate_JUNCTION::setParameter( string paramName, string value ) {
	istringstream iss(value);
	if( paramName == "JUNCTION_ID" ) {
		string myOldID = myID;

		// Read in the new ID:
		myID = value; // (We want to include whitespace in them, too.)
//		iss >> myID;

		// (Note that the first time that this is called is from the
		// constructor, which calls it with the string "NONE" as the new
		// id, and myID is uninitialized, which means it contains "" already.)

		// If we didn't change the junction ID, then we're done:
		if( myOldID == myID ) return false;
		// Decrement the old junction id counter:
		if( !((*(myCircuit->getJunctionIDs())).empty() ) && ((*(myCircuit->getJunctionIDs())).find( myOldID ) != (*(myCircuit->getJunctionIDs())).end()) ) {
			(*(myCircuit->getJunctionUseCounter()))[myOldID] -= 1;
			// Unhook this gate's wires from the old junction:
			ID_SET< IDType >::iterator thisWire = myWires.begin();
			while( thisWire != myWires.end() ) {
				myCircuit->disconnectJunction( (*(myCircuit->getJunctionIDs()))[myOldID], *thisWire );
				thisWire++;
			}

			// If that junction is no longer used, then erase it:
			if( (*(myCircuit->getJunctionUseCounter()))[myOldID] == 0 ) {
				// Erase the junction from the Circuit:
				myCircuit->deleteJunction( (*(myCircuit->getJunctionIDs()))[myOldID] );

				// Erase the junction from the junction maps:				
				(*(myCircuit->getJunctionIDs())).erase( myOldID );
				(*(myCircuit->getJunctionUseCounter())).erase( myOldID );
			}

		}

		// If the junction does not already exist, then create it. Otherwise,
		// simply increment the "use counter":
		if( (*(myCircuit->getJunctionIDs())).find( myID ) == (*(myCircuit->getJunctionIDs())).end() ) {
			// Create the new junction in the circuit:
			(*(myCircuit->getJunctionIDs()))[myID] = myCircuit->newJunction();

			// Add the use counter:
			(*(myCircuit->getJunctionUseCounter()))[myID] = 1;
		} else {
			(*(myCircuit->getJunctionUseCounter()))[myID] += 1;
		}

		// Add this gate's wires to the newly assigned junction:
		ID_SET< IDType >::iterator thisWire = myWires.begin();
		while( thisWire != myWires.end() ) {
			myCircuit->connectJunction( (*(myCircuit->getJunctionIDs()))[myID], *thisWire );
			thisWire++;
		}

		return false; // gateProcess() doesn't do anything anyway!
	} else {
		return Gate::setParameter( paramName, value );
	}
}


// Set the junction's ID:
string Gate_JUNCTION::getParameter( string paramName ) {
	ostringstream oss;
	if( paramName == "JUNCTION_ID" ) {
		oss << myID;
		return oss.str();
	} else {
		return Gate::getParameter( paramName );
	}
}


// Connect a wire to the input of this gate:
void Gate_JUNCTION::connectInput( string inputID, IDType wireID ) {
	Gate::connectInput( inputID, wireID );

	// Connect the wire to the junction in the Circuit:	
	myCircuit->connectJunction( (*(myCircuit->getJunctionIDs()))[myID], wireID );

	// Track this wire, so that it can move to a new junction if
	// our name changes:
	myWires.insert( wireID );
	if( myWireCounts.find( wireID ) == myWireCounts.end() ) {
		myWireCounts[wireID] = 1;
	} else {
		myWireCounts[wireID] += 1;
	}

//TODO: Decide whether or not it is a good idea to allow inputs
// to be connected to the gate, or if they should all be outputs, to avoid
// the wire having a dangling wireInput object.
}


// Disconnect a wire from the input of this gate:
// (Returns the wireID of the wire that was connected.)
IDType Gate_JUNCTION::disconnectInput( string inputID ) {
	IDType wireID = ID_NONE;
	
	// Call the gate's method:
	wireID = Gate::disconnectInput( inputID );

	if( wireID != ID_NONE ) {
		// Unhook the wire from the Junction in the Circuit:
		myCircuit->disconnectJunction( (*(myCircuit->getJunctionIDs()))[myID], wireID );
	}

	// Erase the wire from our tracking list, so that we won't keep it anymore:
	if( myWireCounts[wireID] == 1 ) {
		myWires.erase( wireID );
		myWireCounts.erase( wireID );
	}

	return wireID;
}


// **************************** END Junction GATE ***********************************



// ******************************** T GATE ***********************************
// This class uses the circuit's Junctioning capabilities
// to enable and disable a junction and splice the inputs
// into the junction, to allow T-gates.
// Note: All of the connections are INPUTS! (That way, they default to HI_Z.)
// Input 0 = T-Gate input
// Input 1 = T-Gate input2/output
// Input 2 = Control input


// Initialize the starting state and the output:
Gate_T::Gate_T( Circuit *newCircuit ) : Gate() {

	// Keep the circuit pointer, to use to access the Junctions
	myCircuit = newCircuit;

	// Create the Junction object in the Circuit:
	junctionID = myCircuit->newJunction();

	// The Junction starts out disconnected:
	juncLastState = false;
	myCircuit->setJunctionState( junctionID, juncLastState );

	// Declare the gate inputs and output:
	declareInput( "T_in" );
	declareInput( "T_in2" );
	declareInput( "T_ctrl" );
}


// Destroy the gate, and remove the Junction object from the
// Circuit:
Gate_T::~Gate_T() {
//NOTE: This doesn't crash the system when the Circuit object is destroyed,
//      because Circuit::~Circuit() always explicitly destroys all gates.
	myCircuit->deleteJunction( junctionID );
}


// Handle gate events:
void Gate_T::gateProcess( void ) {
	// The new state to set the junction to:
	// (The junction is set to FALSE unless
	// the control input is 1.)
	bool juncNewState = false;

	// Check the control input to determine the output:
	StateType ctrlValue = getInputState("T_ctrl");
	if( ctrlValue == ONE ) {
		juncNewState = true;
	}

	// If the junction state changed, then update it:
	if( juncNewState != juncLastState ) {
		juncLastState = juncNewState;

		// Use the default delay if the delay is not specified.
		TimeType delay = defaultDelay;

		// The event variables for the event to be thrown:
		TimeType eTime = getSimTime() + delay;
		myCircuit->createJunctionEvent( eTime, junctionID, juncNewState );
	}
}


// Connect a wire to the input of this gate:
void Gate_T::connectInput( string inputID, IDType wireID ) {
	Gate::connectInput( inputID, wireID );

	// If it's the T-gate input, then hook it to the junction also:
	if( inputID != "T_ctrl" ) {
		// Connect the wire to the junction in the Circuit:
		myCircuit->connectJunction( junctionID, wireID );
	}
}


// Disconnect a wire from the input of this gate:
// (Returns the wireID of the wire that was connected.)
IDType Gate_T::disconnectInput( string inputID ) {
	IDType wireID = ID_NONE;
	
	// Call the gate's method:
	wireID = Gate::disconnectInput( inputID );

	if( (wireID != ID_NONE) && (inputID != "T_ctrl") ) {
		// Unhook the wire from the Junction in the Circuit:
		myCircuit->disconnectJunction( junctionID, wireID );
	}

	return wireID;
}


// **************************** END T GATE ***********************************


// **************************** NODE Gate ***********************************
// This class uses the circuit's Junctioning capabilities
// to splice the inputs into the junction, to allow nodes.
// Note: All of the connections are INPUTS! (That way, they default to HI_Z.)
// Input 0-7 = NODE gate input/outputs


// Initialize the starting state and the output:
Gate_NODE::Gate_NODE( Circuit *newCircuit ) : Gate() {

	// Keep the circuit pointer, to use to access the Junctions
	myCircuit = newCircuit;

	// Create the Junction object in the Circuit:
	junctionID = myCircuit->newJunction();

	// The Junction is always connected:
	myCircuit->setJunctionState( junctionID, true );

	// Declare the gate inputs and output:
	declareInput( "N_in0" );
	declareInput( "N_in1" );
	declareInput( "N_in2" );
	declareInput( "N_in3" );

	declareInput( "N_in4" );
	declareInput( "N_in5" );
	declareInput( "N_in6" );
	declareInput( "N_in7" );
}


// Destroy the gate, and remove the Junction object from the
// Circuit:
Gate_NODE::~Gate_NODE() {
//NOTE: This doesn't crash the system when the Circuit object is destroyed,
//      because Circuit::~Circuit() always explicitly destroys all gates.
	myCircuit->deleteJunction( junctionID );
}


// Handle gate events:
void Gate_NODE::gateProcess( void ) {
	// The Junction handles all of the processing for the Gate_NODE.
}


// Connect a wire to the input of this gate:
void Gate_NODE::connectInput( string inputID, IDType wireID ) {
	Gate::connectInput( inputID, wireID );

	// Connect the wire to the junction in the Circuit:
	myCircuit->connectJunction( junctionID, wireID );
}


// Disconnect a wire from the input of this gate:
// (Returns the wireID of the wire that was connected.)
IDType Gate_NODE::disconnectInput( string inputID ) {
	IDType wireID = ID_NONE;
	
	// Call the gate's method:
	wireID = Gate::disconnectInput( inputID );

	if( wireID != ID_NONE ) {
		// Unhook the wire from the Junction in the Circuit:
		myCircuit->disconnectJunction( junctionID, wireID );
	}

	return wireID;
}


// **************************** END NODE GATE ***********************************





// **************************** BUS_END Gate ***********************************


// Initialize the starting state and the output:
Gate_BUS_END::Gate_BUS_END(Circuit *newCircuit) : Gate() {

	// Keep the circuit pointer, to use to access the Junctions
	myCircuit = newCircuit;
	busWidth = 0;
}

Gate_BUS_END::~Gate_BUS_END() {
	for (IDType id : junctionIDs) {
		myCircuit->deleteJunction(id);
	}
}

void Gate_BUS_END::gateProcess() { }

void Gate_BUS_END::connectInput(string inputID, IDType wireID) {
	Gate::connectInput(inputID, wireID);
	
	// Grab the bus line from the back of the inputID.
	int id = atoi(inputID.substr(inputID.find('_') + 1).c_str());

	// Connect the wire to the junction in the Circuit:
	myCircuit->connectJunction(junctionIDs[id], wireID);
}

IDType Gate_BUS_END::disconnectInput(string inputID) {
	IDType wireID = ID_NONE;

	// Call the gate's method:
	wireID = Gate::disconnectInput(inputID);

	// Grab the bus line from the back of the inputID.
	int id = atoi(inputID.substr(inputID.find('_') + 1).c_str());

	if (wireID != ID_NONE) {
		// Unhook the wire from the Junction in the Circuit:
		myCircuit->disconnectJunction(junctionIDs[id], wireID);
	}

	return wireID;
}

bool Gate_BUS_END::setParameter(string paramName, string value) {

	istringstream iss(value);
	if (paramName == "INPUT_BITS") {

		if (busWidth != 0) {
			return false;
		}

		iss >> busWidth;

		// Declare the address pins!		
		if (busWidth > 0) {

			// Declare inputs and outputs for connect/disconnect routines.
			declareInputBus("IN", busWidth);
			declareInputBus("OUT", busWidth);

			// Create internal junctions.
			for (int i = 0; i < busWidth; i++) {
				junctionIDs.push_back(myCircuit->newJunction());
				myCircuit->setJunctionState(junctionIDs[i], true);
			}
		}

		//NOTE: Don't return "true" from this, because
		// you shouldn't be setting this param during simulation while
		// anything is connected anyhow!
	}
	else {
		return Gate::setParameter(paramName, value);
	}
	return false;
}

string Gate_BUS_END::getParameter(string paramName) {

	ostringstream oss;
	if (paramName == "INPUT_BITS") {
		oss << busWidth;
		return oss.str();
	}
	else {
		return Gate::getParameter(paramName);
	}
}


// **************************** END BUS_END GATE ***********************************


//***************************************************************
//Edit by Joshua Lansford 6/5/2007
//This edit is to create a new gate called the pauseulator.
//This gate has one input and no outputs.  When the input of this
//gate goes high, then it will pause the simulation.  This takes
//avantage of the pauseing hooks that I had to create for the Z80.
Gate_pauseulator::Gate_pauseulator() : Gate(){
	declareInput( "signal", true );	
}

void Gate_pauseulator::gateProcess( void ) {
	if( isRisingEdge( "signal" ) ){
		listChangedParam( "PAUSE_SIM" );
	}
}

bool Gate_pauseulator::setParameter( string paramName, string value ) {
	//this is here to catch PAUSE_SIM so that when we load
	//and PAUSE_SIM gets thrown at us from the file,
	//we will pretend to do something with it.
	return false;
}

string Gate_pauseulator::getParameter( string paramName ) {
	//the only param that the system might we wanting is
	//PAUSE_SIM, so we will return "TRUE" because we only
	//flag it when it is true.
	return "TRUE";
}


//End of edit****************************************************








