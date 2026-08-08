
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
		if ((gCircuit->getWires())->find(preMoveWire[i].id) == (gCircuit->getWires())->end()) continue; // error, wire not found
		oldSegMaps[preMoveWire[i].id] = preMoveWire[i].oldWireTree;
		newSegMaps[preMoveWire[i].id] = (*(gCircuit->getWires()))[preMoveWire[i].id]->getSegmentMap();
	}

	this->gCircuit = gCircuit;
	this->startX = startX;
	this->startY = startY;
	this->endX = endX;
	this->endY = endY;
	wireMove = 1;
}

cmdMoveSelection::~cmdMoveSelection() {
	// Owns the proximity-connection sub-commands pushed via getConnections();
	// free them so they don't leak with the undo history.
	for (klsCommand *cmd : proxconnects) delete cmd;
}

// A cached segment map holds raw guiGate* pointers in its connections. When
// this move is part of a paste, undo/redo of the paste deletes and recreates
// the gates, so those pointers dangle. Re-resolve each connection's gate from
// its (stable) gid before the map is restored, or setSegmentMap dereferences
// freed memory. For an ordinary move the gate is unchanged, so this is a no-op.
void cmdMoveSelection::rebindSegMapGates(SegmentMap &segMap) {
	auto *gates = gCircuit->getGates();
	for (auto &seg : segMap) {
		for (wireConnection &conn : seg.second.connections) {
			auto it = gates->find(conn.gid);
			if (it != gates->end()) conn.cGate = it->second;
		}
	}
}

bool cmdMoveSelection::Do() {

	for (unsigned int i = 0; i < gateList.size(); i++) {
		if ((gCircuit->getGates())->find(gateList[i]) == (gCircuit->getGates())->end()) continue; // error, gate not found
		(*(gCircuit->getGates()))[gateList[i]]->translateGLcoords(endX - startX, endY - startY);
		(*(gCircuit->getGates()))[gateList[i]]->finalizeWirePlacements();
	}
	for (unsigned int i = 0; i < wireList.size(); i++) {
		if ((gCircuit->getWires())->find(wireList[i]) == (gCircuit->getWires())->end()) continue; // error, wire not found
		rebindSegMapGates(newSegMaps[wireList[i]]);
		(*(gCircuit->getWires()))[wireList[i]]->setSegmentMap(newSegMaps[wireList[i]]);
	}
	for (unsigned int i = 0; i < proxconnects.size(); i++) {
		proxconnects[i]->Do();
	}
	return true;
}

bool cmdMoveSelection::Undo() {
	for (unsigned int i = 0; i < gateList.size(); i++) {
		if ((gCircuit->getGates())->find(gateList[i]) == (gCircuit->getGates())->end()) continue; // error, gate not found
		(*(gCircuit->getGates()))[gateList[i]]->translateGLcoords(startX - endX, startY - endY);
		(*(gCircuit->getGates()))[gateList[i]]->finalizeWirePlacements();
	}
	for (unsigned int i = 0; i < wireList.size() && wireMove < 0; i++) {
		if ((gCircuit->getWires())->find(wireList[i]) == (gCircuit->getWires())->end()) continue; // error, wire not found
		rebindSegMapGates(oldSegMaps[wireList[i]]);
		(*(gCircuit->getWires()))[wireList[i]]->setSegmentMap(oldSegMaps[wireList[i]]);
	}
	wireMove = -1;
	for (unsigned int i = 0; i < proxconnects.size(); i++) {
		proxconnects[i]->Undo();
	}
	return true;
}

vector<klsCommand *> * cmdMoveSelection::getConnections() {
	return &proxconnects;
}