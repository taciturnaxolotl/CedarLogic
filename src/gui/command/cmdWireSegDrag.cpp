
#include "cmdWireSegDrag.h"
#include "../GUICircuit.h"
#include "../guiWire.h"

cmdWireSegDrag::cmdWireSegDrag(GUICircuit* gCircuit, CircuitPage* gCanvas,
		IDType wireID) :
			klsCommand(true, "Wire Shape") {

	this->gCircuit = gCircuit;
	this->gCanvas = gCanvas;
	this->wireID = wireID;

	guiWire *wire = gCircuit->getWire(wireID);
	if (wire == nullptr) return; // error: wire not found

	oldSegMap = wire->getOldSegmentMap();
	newSegMap = wire->getSegmentMap();
}

bool cmdWireSegDrag::Do() {

	guiWire *wire = gCircuit->getWire(wireID);
	if (wire == nullptr) return false; // error: wire not found

	wire->setSegmentMap(newSegMap);

	return true;
}

bool cmdWireSegDrag::Undo() {

	guiWire *wire = gCircuit->getWire(wireID);
	if (wire == nullptr) return false; // error: wire not found

	wire->setSegmentMap(oldSegMap);

	return true;
}