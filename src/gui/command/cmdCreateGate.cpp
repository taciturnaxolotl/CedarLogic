
#include "cmdCreateGate.h"
#include "../GateLibrary.h"
#include "../GUICircuit.h"
#include "../GUICanvas.h"
#include "../guiGate.h"
#include "../MainApp.h"
#include "cmdSetParams.h"
#include "cmdSerialize.h"
#include "cmdRegistry.h"

DECLARE_APP(MainApp)

// Self-register so the paste dispatcher can rebuild a "creategate ..." line
// without naming this type. See cmdRegistry.h.
static const bool s_registered_cmdCreateGate =
	cmd::registerFactory("creategate", [](const std::string &def) -> klsCommand * {
		return new cmdCreateGate(def);
	});

cmdCreateGate::cmdCreateGate(GUICanvas* gCanvas, GUICircuit* gCircuit, unsigned long gid, string gateType, float x, float y) : klsCommand(true, "Create Gate") {
	this->gCanvas = gCanvas;
	this->gCircuit = gCircuit;
	this->gid = gid;
	this->gateType = gateType;
	this->x = x;
	this->y = y;
	this->fromString = false;
}

cmdCreateGate::cmdCreateGate(string def) : klsCommand(true, "Create Gate") {
	cmdser::CreateGate d;
	cmdser::parse(def, d);
	gid = d.gid;
	gateType = d.gateType;
	x = d.x;
	y = d.y;
	this->fromString = true;
}

cmdCreateGate::~cmdCreateGate() {
	// Owns the proximity-connection sub-commands pushed via getConnections();
	// free them so they don't leak with the undo history.
	for (klsCommand *cmd : proxconnects) delete cmd;
}

bool cmdCreateGate::Do() {
	if (gateLibrary().libraries.size() == 0) return false; // No library loaded, so can't create gate

	gCircuit->createGate(gateType, gid, fromString);
	gCanvas->insertGate(gid, (*(gCircuit->getGates()))[gid], x, y);

	string logicType = gateLibrary().libParser.getGateLogicType(gateType);
	if (logicType.size() > 0) {
		ostringstream oss;
		gCircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_CREATE_GATE, new klsMessage::Message_CREATE_GATE(logicType, gid)));
	} // if( logic type is non-null )

	cmdSetParams setgateparams(gCircuit, gid, paramSet((*(gCircuit->getGates()))[gid]->getAllGUIParams(), (*(gCircuit->getGates()))[gid]->getAllLogicParams()), fromString);
	setgateparams.Do();

	// Must set hotspot params after the gate's params, because sometimes logic_params will create
	// the hotspots!
	if (logicType.size() > 0) {
		// Loop through the hotspots and pass logic core hotspot settings:
		LibraryGate libGate;
		gateLibrary().libParser.getGate(gateType, libGate);
		for (unsigned int i = 0; i < libGate.hotspots.size(); i++) {

			// Send the isInverted message:
			if (libGate.hotspots[i].isInverted) {
				if (libGate.hotspots[i].isInput) {
					gCircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_INPUT_PARAM, new klsMessage::Message_SET_GATE_INPUT_PARAM(gid, libGate.hotspots[i].name, "INVERTED", "TRUE")));
				}
				else {
					gCircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_OUTPUT_PARAM, new klsMessage::Message_SET_GATE_OUTPUT_PARAM(gid, libGate.hotspots[i].name, "INVERTED", "TRUE")));
				}
			}

			// Send the logicEInput message:
			if (libGate.hotspots[i].logicEInput != "") {
				if (libGate.hotspots[i].isInput) {
					gCircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_INPUT_PARAM, new klsMessage::Message_SET_GATE_INPUT_PARAM(gid, libGate.hotspots[i].name, "E_INPUT", libGate.hotspots[i].logicEInput)));
				}
				else {
					gCircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_OUTPUT_PARAM, new klsMessage::Message_SET_GATE_OUTPUT_PARAM(gid, libGate.hotspots[i].name, "E_INPUT", libGate.hotspots[i].logicEInput)));
				}
			}
		} // for( loop through the hotspots )
	} // if( logic type is non-null )


	for (unsigned int i = 0; i < proxconnects.size(); i++) {
		proxconnects[i]->Do();
	}
	return true;
}

bool cmdCreateGate::Undo() {
	for (unsigned int i = 0; i < proxconnects.size(); i++) {
		proxconnects[i]->Undo();
	}
	gCanvas->removeGate(gid);
	gCircuit->deleteGate(gid);
	string logicType = gateLibrary().libParser.getGateLogicType(gateType);
	if (logicType.size() > 0) {
		gCircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_DELETE_GATE, new klsMessage::Message_DELETE_GATE(gid)));
	}
	return true;
}

std::string cmdCreateGate::toString() const {
	return cmdser::emit(cmdser::CreateGate{gid, gateType, x, y});
}

void cmdCreateGate::setPointers(GUICircuit* gCircuit, GUICanvas* gCanvas,
		TranslationMap &gateids, TranslationMap &wireids) {

	// Find myself an appropriate ID for a new environment
	if (gateids.find(gid) != gateids.end()) {
		gid = gateids[gid];
	}
	else {
		gateids[gid] = gCircuit->getNextAvailableGateID();
		gid = gateids[gid];
	}
	this->gCircuit = gCircuit;
	this->gCanvas = gCanvas;
}

std::vector<klsCommand *> * cmdCreateGate::getConnections() {
	return &proxconnects;
}
