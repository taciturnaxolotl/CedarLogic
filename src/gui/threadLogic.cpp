/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   threadLogic: Main execution point of logic thread
*****************************************************************************/

#include "threadLogic.h"
#include "SimBridge.h"
#include "MainApp.h"
#include <sstream>
#include "wx/timer.h"

#include "logic_defaults.h"
#include "logic_wire.h"
#include "logic_gate.h"
#include "logic_circuit.h"
#include "logic_event.h"
#include <string>


DECLARE_APP(MainApp)

threadLogic::threadLogic() : wxThread() {
	return;
}

void *threadLogic::Entry() {
	// This is the main function of the thread, so now we can init
#ifndef _PRODUCTION_
	logfile.open("logiclog.log");
#endif
	logicIDs = new map < IDType, IDType >;
	
	cir = new Circuit();
	while (!TestDestroy()) {
		// Block until the GUI queues a message (or we're asked to stop) rather
		// than spinning on a 1ms sleep. WaitTimeout bounds how long a pending
		// TestDestroy() goes unnoticed, so shutdown stays prompt without the
		// teardown path having to signal us.
		{
			wxMutexLocker lock(simBridge().mexMessages);
			while (simBridge().dGUItoLOGIC.empty() && !TestDestroy()) {
				simBridge().msgForLogic.WaitTimeout(100);
			}
		}
		checkMessages();
	}

	return NULL;
}

void threadLogic::checkMessages() {
	wxCriticalSectionLocker locker(simBridge().m_critsect);
	// Take the whole pending batch under the lock, then process it with the lock
	// released. Holding mexMessages only for the O(1) swap avoids the old
	// TryLock+wxYield busy-spin, and lets parseMessage's STEPSIM path re-take
	// mexMessages (via sendMessage) without relying on the mutex being recursive.
	deque< klsMessage::Message > batch;
	{
		wxMutexLocker lock(simBridge().mexMessages);
		batch.swap(simBridge().dGUItoLOGIC);
	}
	while (!batch.empty()) {
		parseMessage(batch.front());
		batch.pop_front();
	}
}

void threadLogic::OnExit() {
	wxCriticalSectionLocker locker(simBridge().m_critsect);
	delete cir;
	delete logicIDs;
	// Tell the main thread we can exit now
	simBridge().m_semAllDone.Post();
}

bool threadLogic::parseMessage(klsMessage::Message input) {
	string temp, type, pinID;
	long id, wireID;
	switch (input.mType) {
	case klsMessage::MT_REINITIALIZE: {
		// REINITIALIZE LOGIC CIRCUIT
		delete cir;
		cir = new Circuit();
		logicIDs->clear();
		break;
	}
	case klsMessage::MT_CREATE_GATE: {
		// CREATE GATE TYPE type ID id
		const klsMessage::Message_CREATE_GATE& msg = input.as<klsMessage::Message_CREATE_GATE>();

		// tell logic core to create a gate id of type OR
		cir->newGate( msg.gateType, msg.gateId );
		break;
	}
	case klsMessage::MT_CREATE_WIRE: {
		// CREATE WIRE ID id
		id = input.as<klsMessage::Message_CREATE_WIRE>().wireId;
		// tell logic core to create wire id
		(*logicIDs)[id] = cir->newWire( id );
		break;
	}
	case klsMessage::MT_DELETE_GATE: {
		// DELETE GATE id
		id = input.as<klsMessage::Message_DELETE_GATE>().gateId;
		cir->deleteGate(id);
		break;
	}
	case klsMessage::MT_DELETE_WIRE: {
		// DELETE WIRE id
		id = input.as<klsMessage::Message_DELETE_WIRE>().wireId;
		cir->deleteWire((*logicIDs)[id]);
		break;
	}
	case klsMessage::MT_SET_GATE_INPUT: {
		// SET GATE ID id INPUT ID id TO DISCONNECT/wid
		const klsMessage::Message_SET_GATE_INPUT& msg = input.as<klsMessage::Message_SET_GATE_INPUT>();
		id = msg.gateId;
		pinID = msg.inputId;
		// tell logic core to set gate id's input id to connect with wireID
		if (msg.disconnect) {
			cir->disconnectGateInput( id, pinID );
		} else {
			wireID = msg.wireId;
			if (logicIDs->find(wireID) == logicIDs->end()) {
				(*logicIDs)[wireID] = cir->connectGateInput( id, pinID, wireID );
			} else {
				cir->connectGateInput( id, pinID, (*logicIDs)[wireID] );
			}
		}
		break;
	}
	case klsMessage::MT_SET_GATE_INPUT_PARAM: {
		// SET GATE ID id INPUT ID id PARAM name value
		const klsMessage::Message_SET_GATE_INPUT_PARAM& msg = input.as<klsMessage::Message_SET_GATE_INPUT_PARAM>();
		// Send name "pName" and value "input" to gate for input pin settings
		cir->setGateInputParameter( msg.gateId, msg.inputId, msg.paramName, msg.paramValue );
		break;
	}
	case klsMessage::MT_SET_GATE_OUTPUT: {
		// SET GATE ID id OUTPUT ID id TO DISCONNECT/wid
		const klsMessage::Message_SET_GATE_OUTPUT& msg = input.as<klsMessage::Message_SET_GATE_OUTPUT>();
		id = msg.gateId;
		pinID = msg.outputId;
		// tell logic core to set gate id's output id to connect with wireID
		if (msg.disconnect) {
			cir->disconnectGateOutput( id, pinID );
		} else {
			wireID = msg.wireId;
			if (logicIDs->find(wireID) == logicIDs->end()) {
				(*logicIDs)[wireID] = cir->connectGateOutput( id, pinID, wireID );
			} else {
				cir->connectGateOutput( id, pinID, (*logicIDs)[wireID] );
			}
		}
		break;
	}

	case klsMessage::MT_SET_GATE_OUTPUT_PARAM: {
		// SET GATE ID id OUTPUT ID id PARAM name value
		const klsMessage::Message_SET_GATE_OUTPUT_PARAM& msg = input.as<klsMessage::Message_SET_GATE_OUTPUT_PARAM>();
		// Send name "pName" and value "input" to gate for output pin settings
		cir->setGateOutputParameter( msg.gateId, msg.outputId, msg.paramName, msg.paramValue );
		break;
	}
	case klsMessage::MT_SET_GATE_PARAM: {
		// SET GATE ID id PARAMETER paramname paramval
		const klsMessage::Message_SET_GATE_PARAM& msg = input.as<klsMessage::Message_SET_GATE_PARAM>();
		cir->setGateParameter(msg.gateId, msg.paramName, msg.paramValue);
		break;
	}
	case klsMessage::MT_STEPSIM: {
		// STEPSIM numSteps
		wxStopWatch simTime;
		int numSteps = input.as<klsMessage::Message_STEPSIM>().numSteps;
		bool pauseingSim = false;
		// Do that many steps and then notify GUI that we're done
		for (int i = 0; i < numSteps && !pauseingSim; i++) {
			ID_SET< IDType > changedWires;
			
			cir->step(&changedWires);
			{
				wxMutexLocker lock(simBridge().wireStateMutex);
				ID_SET< IDType >::iterator cw = changedWires.begin();
				while (cw != changedWires.end()) {
					simBridge().wireStateBuffer[*cw] = (StateType)cir->getWireState(*cw);
					cw++;
				}
			}
			
			// Update the possibly changed parameters:
			vector < changedParam > changedParams = cir->getParamUpdateList(); // Get the parameters that changed during this time step.
			cir->clearParamUpdateList(); // Let the circuit know that we are handling the updates!
			string paramVal;
			for( unsigned int i = 0; i < changedParams.size(); i++ ) {
				paramVal = cir->getGateParameter( changedParams[i].gateID, changedParams[i].paramName );
				if( paramVal.size() > 0 ) {
					sendMessage(klsMessage::Message(klsMessage::MT_SET_GATE_PARAM, new klsMessage::Message_SET_GATE_PARAM(changedParams[i].gateID, changedParams[i].paramName, paramVal)));
				}
				
				//************************************************************
				//Edit by Joshua Lansford 11/24/06
				//the perpose of this edit is to allow logic gates to be able
				//to pause the simulation.  This is so that the 
				//Z_80LogicGate can 'single step' through T states and
				//instruction states by pauseing the simulation when it
				//compleates eather.
				//
				//The way that this is acomplished is that when ever any gate
				//signals that a property has changed, and the name of that
				//property is "PAUSE_SIM", then the core should bail out
				//and not finnish the requested number of steps.
				//The GUI will also see this property fly by and will toggle
				//the pause button.
				//
				//This spacific edit is so that the core will see this property
				//and will bail out.
				if( changedParams[i].paramName == "PAUSE_SIM" ){
					pauseingSim = true;
				}
				//End of Edit************************************************
			}
			// send interim done step message
			sendMessage(klsMessage::Message(klsMessage::MT_COMPLETE_INTERIM_STEP));
		}
		sendMessage(klsMessage::Message(klsMessage::MT_DONESTEP, new klsMessage::Message_DONESTEP(simTime.Time())));
		break;
	}
	case klsMessage::MT_UPDATE_GATES: {
		//*********************************************
		//Edit by Joshua Lansford 3/27/07
		//Purpose of edit:
		//  This is a new command that the gui can
		//  send the core. "UPDATE GATES".  It makes
		//  it so that gates can respond with paramiter
		//  changes without steping the simulation
		//  forward by a step
		
		// UPDATE GATE PARAMS		
		cir->stepOnlyGates();

		// Update the possibly changed parameters:
		vector < changedParam > changedParams = cir->getParamUpdateList(); // Get the parameters that changed
		cir->clearParamUpdateList(); // Let the circuit know that we are handling the updates!
		string paramVal;
		for( unsigned int i = 0; i < changedParams.size(); i++ ) {
			paramVal = cir->getGateParameter( changedParams[i].gateID, changedParams[i].paramName );
			if( paramVal.size() > 0 ) {
				sendMessage(klsMessage::Message(klsMessage::MT_SET_GATE_PARAM, new klsMessage::Message_SET_GATE_PARAM(changedParams[i].gateID, changedParams[i].paramName, paramVal)));
			}
	
		}
		break;
	}
	default:
		break;
	}
	//End of edit**********************************
	
	return false;
}

void threadLogic::sendMessage(klsMessage::Message message) {
	wxMutexLocker lock(simBridge().mexMessages);
	simBridge().dLOGICtoGUI.push_back(message);
}
