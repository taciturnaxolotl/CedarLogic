
#include "cmdMoveGate.h"
#include "../GUICircuit.h"
#include "../guiGate.h"
#include "cmdSerialize.h"

cmdMoveGate::cmdMoveGate(GUICircuit* gCircuit, unsigned long gid,
		float startX, float startY, float endX, float endY, bool uW) :
			klsCommand(true, "Move Gate") {

	this->gCircuit = gCircuit;
	this->gid = gid;
	this->startX = startX;
	this->startY = startY;
	this->endX = endX;
	this->endY = endY;
	this->noUpdateWires = uW;
}

bool cmdMoveGate::Do() {

	guiGate *gate = gCircuit->getGate(gid);
	if (gate == nullptr) return false; // error, gate not found

	gate->setGLcoords(endX, endY, noUpdateWires);
	return true;
}

bool cmdMoveGate::Undo() {

	guiGate *gate = gCircuit->getGate(gid);
	if (gate == nullptr) return false; // error, gate not found

	gate->setGLcoords(startX, startY, noUpdateWires);
	return true;
}

std::string cmdMoveGate::toString() const {

	if (gCircuit->getGate(gid) == nullptr) return ""; // error, gate not found

	return cmdser::emit(cmdser::MoveGate{gid, startX, startY, endX, endY});
}