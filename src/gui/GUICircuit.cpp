/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   GUICircuit: Contains GUI circuit manipulation functions
*****************************************************************************/

#include "GUICircuit.h"
#include "SimBridge.h"
#include "GateLibrary.h"
#include "MainApp.h"
#include "GUICanvas.h"
#include "OscopeFrame.h"
#include "guiWire.h"

DECLARE_APP(MainApp)
IMPLEMENT_DYNAMIC_CLASS(GUICircuit, wxDocument)

GUICircuit::GUICircuit() {
	nextGateID = nextWireID = 0;
	simulate = true;
	waitToSendMessage = true;
	panic = false;
	pausing = false;
	return;
}

GUICircuit::~GUICircuit() {

}

void GUICircuit::reInitializeLogicCircuit() {
	// do not wait to send messages to the core for reinit
	bool iswaiting = waitToSendMessage;
	waitToSendMessage = false;
	sendMessageToCore(klsMessage::Message(klsMessage::MT_REINITIALIZE));
	waitToSendMessage = iswaiting;
	// Indexes first, then the owners. buslineToWire only borrows, and leaving it
	// populated past the wires it points at is exactly the SIGSEGV in issue #100:
	// syncWireStates() walked it on the next step after a file open and wrote
	// through freed pointers. The pending states go too, since they describe the
	// old circuit and its ids get reused from zero.
	buslineToWire.clear();
	{
		wxMutexLocker lock(simBridge().wireStateMutex);
		simBridge().wireStateBuffer.clear();
	}

	// Clearing the maps destroys everything in them.
	gateList.clear();
	gateListVersion++;
	wireList.clear();
	nextGateID = nextWireID = 0;
	waitToSendMessage = false;
	simulate = true;
}

guiGate* GUICircuit::createGate(string gateName, long id, bool noOscope) {
	// Look the name up without inserting it. operator[] on these maps used to add
	// an entry for every unknown gate a file named, so after one bad file the app
	// treated that name as a real, empty gate type for the rest of the session.
	string libName;
	LibraryGate gateDef;
	auto owner = gateLibrary().gateNameToLibrary.find(gateName);
	if (owner != gateLibrary().gateNameToLibrary.end()) {
		libName = owner->second;
		auto lib = gateLibrary().libraries.find(libName);
		if (lib != gateLibrary().libraries.end()) {
			auto def = lib->second.find(gateName);
			if (def != lib->second.end()) gateDef = def->second;
		}
	}

	if (id == -1) id = getNextAvailableGateID();

	// Refuse an id that is already taken. Storing over the entry would destroy
	// the gate living there, and the canvas, the collision checker and every
	// wire connected to it all keep raw pointers that would be left dangling.
	// A file naming the same gate id twice is enough to reach this. Every
	// caller already handles a null return as "could not make that gate".
	if (gateList.find(id) != gateList.end()) return nullptr;

	guiGate* newGate = NULL;
	

	string ggt = gateDef.guiType;
	
	if (ggt == "REGISTER")
		newGate = (guiGate*)(new guiGateREGISTER());
	else if (ggt == "TO" || ggt == "FROM")
		newGate = (guiGate*)(new guiTO_FROM());
	else if (ggt == "LABEL")
		newGate = (guiGate*)(new guiLabel());
	else if (ggt == "LED")
		newGate = (guiGate*)(new guiGateLED());
	else if (ggt == "TOGGLE")
		newGate = (guiGate*)(new guiGateTOGGLE());
	else if (ggt == "KEYPAD")
		newGate = (guiGate*)(new guiGateKEYPAD());
	else if (ggt == "PULSE")
		newGate = (guiGate*)(new guiGatePULSE());
	else if (ggt == "RAM"){
		newGate = (guiGate*)(new guiGateRAM());
	}
	else
		newGate = new guiGate();

	newGate->setLibraryName( libName, gateName );

	for (unsigned int i = 0; i < gateDef.shape.size(); i++) {
		lgLine tempLine = gateDef.shape[i];
		newGate->insertLine(tempLine.x1, tempLine.y1, tempLine.x2, tempLine.y2, tempLine.labelGroup);
	}
	for (unsigned int i = 0; i < gateDef.arcs.size(); i++) {
		lgArc a = gateDef.arcs[i];
		newGate->insertArc(a.cx, a.cy, a.r, a.startDeg, a.sweepDeg, a.isLabel);
	}
	for (unsigned int i = 0; i < gateDef.circles.size(); i++) {
		lgCircle c = gateDef.circles[i];
		newGate->insertCircle(c.cx, c.cy, c.r, c.segs, c.isLabel);
	}
	for (unsigned int i = 0; i < gateDef.hotspots.size(); i++) {
		lgHotspot tempHS = gateDef.hotspots[i];
		newGate->insertHotspot(tempHS.x, tempHS.y, tempHS.name, tempHS.busLines);
		if (tempHS.isInput) newGate->declareInput(tempHS.name);
		else newGate->declareOutput(tempHS.name);
	}
	map < string, string >::iterator paramWalk = gateDef.guiParams.begin();
	while (paramWalk != gateDef.guiParams.end()) {
		newGate->setGUIParam(paramWalk->first, paramWalk->second);
		paramWalk++;
	}
	paramWalk = gateDef.logicParams.begin();
	while (paramWalk != gateDef.logicParams.end()) {
		newGate->setLogicParam(paramWalk->first, paramWalk->second);
		paramWalk++;
	}
	newGate->calcBBox();
	newGate->setID(id);
	gateList[id] = std::unique_ptr<guiGate>(newGate);
	gateListVersion++;
	
	// Update the OScope with the new info:
	if(ggt == "TO" && !noOscope) {
		myOscope->UpdateMenu();
	}
	
	return newGate;
}

void GUICircuit::deleteGate(unsigned long gid, bool waitToUpdate) {
	
	//Declaration Of Variables
	bool updateMenu = false;
	
	guiGate *gate = getGate(gid);
	if (gate == nullptr) return;

	//Update Oscope
	if(!waitToUpdate && gate->getGUIType() == "TO") {
		updateMenu = true;
	}

	// Take this gate out of every wire that names it before it stops existing.
	// Commands normally disconnect first and this does nothing; it is here so
	// that "no wire outlives a gate it is connected to" is a property of the
	// circuit rather than a habit of its callers, which is what lets guiWire
	// dereference gateOf() without a null check at thirteen call sites.
	for (const auto &connection : gate->getConnections()) {
		if (connection.second != nullptr) connection.second->removeConnection(gid, connection.first);
	}

	gateList.erase(gid);
	gateListVersion++;

	//Call Update Oscope
	if(updateMenu)
	{
		myOscope->UpdateMenu();
	}		
}

guiWire* GUICircuit::createWire(const std::vector<IDType> &wireIds) {
	if (guiWire *existing = getWire(wireIds[0])) return existing;

	auto wire = std::make_unique<guiWire>();
	wire->setCircuit(this); // so the wire can resolve connection gids to live gates
	wire->setIDs(wireIds);

	// buslineToWire claims every id the wire owns, which is what marks them as
	// used. wireList holds the wire once, under its head id: it used to also
	// hold a nullptr for each remaining bus line, so iterating it meant
	// remembering to skip holes and indexing it could hand back a null.
	guiWire *borrowed = wire.get();
	for (IDType id : wireIds) {
		buslineToWire[id] = borrowed;
	}
	wireList[wireIds[0]] = std::move(wire);
	return borrowed;
}

void GUICircuit::deleteWire(unsigned long wireId) {

	auto it = wireList.find(wireId);
	if (it == wireList.end()) return;

	// Drop the index entries first: they borrow the wire we are about to destroy.
	for (int busLineId : it->second->getIDs()) {
		buslineToWire.erase(busLineId);
	}

	wireList.erase(it);
}

std::unique_ptr<guiGate> GUICircuit::releaseGate(unsigned long gid) {
	auto it = gateList.find(gid);
	if (it == gateList.end()) return nullptr;

	std::unique_ptr<guiGate> gate = std::move(it->second);
	gateList.erase(it);
	gateListVersion++;
	return gate;
}

guiWire* GUICircuit::setWireConnection(const vector<IDType> &wireIds, long gid, string connection, bool openMode) {
	if (getGate(gid) == nullptr) return NULL; // error: gate not found
	guiWire *wire = createWire(wireIds); // do we need to init the wire first? if not then no effect.
	wire->addConnection(getGate(gid), connection, openMode);
	getGate(gid)->addConnection(connection, wire);
	return wire;
}

void GUICircuit::Render() {
	return;
}

void GUICircuit::syncWireStates() {
	wxMutexLocker lock(simBridge().wireStateMutex);
	for (auto& entry : simBridge().wireStateBuffer) {
		if (buslineToWire.find(entry.first) != buslineToWire.end()) {
			buslineToWire[entry.first]->setSubState(entry.first, entry.second);
		}
	}
}

void GUICircuit::parseMessage(klsMessage::Message message) {
	string temp, type;
	switch (message.mType) {
		case klsMessage::MT_SET_GATE_PARAM: {
			// SET GATE id PARAMETER name val
			const klsMessage::Message_SET_GATE_PARAM& msg = message.as<klsMessage::Message_SET_GATE_PARAM>();
			if (guiGate *gate = getGate(msg.gateId)) gate->setLogicParam(msg.paramName, msg.paramValue);
			if( msg.paramName == "PAUSE_SIM" ){
				pausing = true;
				panic = true;
			}
			break;
		}
		case klsMessage::MT_DONESTEP: { // DONESTEP
			simulate = true;
			int logicTime = message.as<klsMessage::Message_DONESTEP>().logicTime;
			// Did the core take longer than the wall time it was catching up on?
			// Keep a 3ms buffer. A step that was making up for a stall is exempt:
			// it was handed a pile of work on purpose and being slow is the point.
			lastLogicTime = logicTime;
			const bool late = (logicTime > lastTime + 3) && !catchingUp;
			lateSteps = late ? lateSteps + 1 : 0;
			catchingUp = false;
			// Only call it an overload once the core has been late repeatedly.
			// A single slow step is noise, and the old check fired on the first
			// one, which is why waking a sleeping laptop raised an alert.
			if (lateSteps >= kLateStepsBeforePanic) {
				panic = true;
				lateSteps = 0;
			}
			// Now we can send the waiting messages
			for (unsigned int i = 0; i < messageQueue.size(); i++) sendMessageToCore(messageQueue[i]);
			messageQueue.clear();
			// Sync wire states and always refresh
			syncWireStates();
			gCanvas->Refresh();
			break;
		}
		case klsMessage::MT_COMPLETE_INTERIM_STEP: {// COMPLETE INTERIM STEP - UPDATE OSCOPE
			syncWireStates();
			myOscope->UpdateData();
			break;
		}
		default:
			break;
	}
}

void GUICircuit::sendMessageToCore(klsMessage::Message message) {
	wxMutexLocker lock(simBridge().mexMessages);

	bool queuedForLogic = false;
	if (waitToSendMessage) {

		if (simulate) {
			simBridge().dGUItoLOGIC.push_back(message);
			queuedForLogic = true;
		} else{
			messageQueue.push_back(message);
		}
	} else{
		simBridge().dGUItoLOGIC.push_back(message);
		queuedForLogic = true;
	}
	// Wake the logic thread if we actually gave it work (it blocks on this
	// condition rather than polling). Signaled under mexMessages, held here.
	if (queuedForLogic) simBridge().msgForLogic.Signal();
}


void GUICircuit::printState() {
	wxGetApp().logfile << "print state" << endl << flush;
	for (const auto &entry : wireList) {
		wxGetApp().logfile << "wire " << entry.first << endl << flush;
	}
	for (const auto &entry : gateList) {
		wxGetApp().logfile << "gate " << entry.first << endl << flush;
	}
	
}
