
#include "cmdDisconnectWire.h"
#include "../GUICircuit.h"
#include "../guiGate.h"
#include "cmdConnectWire.h"
#include "cmdSerialize.h"

cmdDisconnectWire::cmdDisconnectWire(GUICircuit* gCircuit, IDType wireId,
		IDType gateId, const std::string &hotspot, bool noCalcShape) :
			klsCommand(true, "Disconnection") {

	this->gCircuit = gCircuit;
	this->wireId = wireId;
	this->gateId = gateId;
	this->hotspot = hotspot;
	this->noCalcShape = noCalcShape;
}

bool cmdDisconnectWire::Do() {

	if (gCircuit->getWire(wireId) == nullptr) return false; // error: wire not found
	if (gCircuit->getGate(gateId) == nullptr) return false; // error: gate not found


	guiGate* gate = gCircuit->getGate(gateId);
	std::string hotspotPal = gate->getHotspotPal(hotspot);

	if (hotspotPal != "") {
		cmdConnectWire::sendMessagesToDisconnect(gCircuit, wireId, gateId, hotspotPal);
	}
	cmdConnectWire::sendMessagesToDisconnect(gCircuit, wireId, gateId, hotspot);

	return true;
}

bool cmdDisconnectWire::Undo() {

	if (gCircuit->getWire(wireId) == nullptr) return false; // error: wire not found
	if (gCircuit->getGate(gateId) == nullptr) return false; // error: gate not found

	guiGate* gate = gCircuit->getGate(gateId);
	std::string hotspotPal = gate->getHotspotPal(hotspot);

	if (hotspotPal != "") {
		cmdConnectWire::sendMessagesToConnect(gCircuit, wireId, gateId, hotspotPal, noCalcShape);
	}
	cmdConnectWire::sendMessagesToConnect(gCircuit, wireId, gateId, hotspot, noCalcShape);

	return true;
}

std::string cmdDisconnectWire::toString() const {
	return cmdser::emit(cmdser::DisconnectWire{ wireId, gateId, hotspot });
}