
#include "cmdMoveSelection.h"
#include "../GUICircuit.h"
#include "../guiWire.h"
#include "../guiGate.h"

cmdMoveSelection::cmdMoveSelection(GUICircuit* gCircuit,
		vector<GateState> &preMove, vector<WireState> &preMoveWire,
		float startX, float startY, float endX, float endY) :
			klsCommand(true, "Move Selection") {

	for (unsigned int i = 0; i < preMove.size(); i++) gateList.push_back(preMove[i].id);

	for (unsigned int i = 0; i < preMoveWire.size(); i++) {
		wireList.push_back(preMoveWire[i].id);
		if (gCircuit->getWire(preMoveWire[i].id) == nullptr) continue; // error, wire not found
		oldSegMaps[preMoveWire[i].id] = preMoveWire[i].oldWireTree;
		newSegMaps[preMoveWire[i].id] = gCircuit->getWire(preMoveWire[i].id)->getSegmentMap();
	}

	this->gCircuit = gCircuit;
	this->startX = startX;
	this->startY = startY;
	this->endX = endX;
	this->endY = endY;
	wireMove = 1;
}


bool cmdMoveSelection::Do() {

	for (unsigned int i = 0; i < gateList.size(); i++) {
		guiGate *gate = gCircuit->getGate(gateList[i]);
		if (gate == nullptr) continue; // error, gate not found
		gate->translateGLcoords(endX - startX, endY - startY);
		gate->finalizeWirePlacements();
	}
	for (unsigned int i = 0; i < wireList.size(); i++) {
		guiWire *wire = gCircuit->getWire(wireList[i]);
		if (wire == nullptr) continue; // error, wire not found
		wire->setSegmentMap(newSegMaps[wireList[i]]);
	}
	for (unsigned int i = 0; i < proxconnects.size(); i++) {
		proxconnects[i]->Do();
	}
	return true;
}

bool cmdMoveSelection::Undo() {
	for (unsigned int i = 0; i < gateList.size(); i++) {
		guiGate *gate = gCircuit->getGate(gateList[i]);
		if (gate == nullptr) continue; // error, gate not found
		gate->translateGLcoords(startX - endX, startY - endY);
		gate->finalizeWirePlacements();
	}
	for (unsigned int i = 0; i < wireList.size() && wireMove < 0; i++) {
		guiWire *wire = gCircuit->getWire(wireList[i]);
		if (wire == nullptr) continue; // error, wire not found
		wire->setSegmentMap(oldSegMaps[wireList[i]]);
	}
	wireMove = -1;
	for (unsigned int i = 0; i < proxconnects.size(); i++) {
		proxconnects[i]->Undo();
	}
	return true;
}

vector<std::unique_ptr<klsCommand>> * cmdMoveSelection::getConnections() {
	return &proxconnects;
}