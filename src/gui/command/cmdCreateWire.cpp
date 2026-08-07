
#include "cmdCreateWire.h"
#include "../GUICanvas.h"
#include "../guiGate.h"
#include "cmdConnectWire.h"
#include "cmdSerialize.h"

cmdCreateWire::cmdCreateWire(GUICanvas* gCanvas, GUICircuit* gCircuit,
		const std::vector<IDType> &wireIds, cmdConnectWire* conn1,
		cmdConnectWire* conn2) :
			klsCommand(true, "Create Wire") {

	this->gCanvas = gCanvas;
	this->gCircuit = gCircuit;
	this->wireIds = wireIds;
	this->conn1 = conn1;
	this->conn2 = conn2;
}

cmdCreateWire::cmdCreateWire(const std::string &def) :
		klsCommand(true, "Create Wire") {

	cmdser::CreateWire cw;
	cmdser::parse(def, cw);
	wireIds = cw.wireIds;
	conn1 = new cmdConnectWire(cmdser::emit(cw.conn1));
	conn2 = new cmdConnectWire(cmdser::emit(cw.conn2));
}

cmdCreateWire::~cmdCreateWire() {
	delete conn1;
	delete conn2;
}

bool cmdCreateWire::Do() {

	guiWire *wire = gCircuit->createWire(wireIds);
	gCanvas->insertWire(wire);

	for (IDType wireId : wireIds) {
		gCircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_CREATE_WIRE,
			new klsMessage::Message_CREATE_WIRE(wireId)));
	}

	conn1->Do();
	conn2->Do();

	return true;
}

bool cmdCreateWire::Undo() {

	conn1->Undo();
	conn2->Undo();

	for (IDType wireId : wireIds) {
		gCircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_DELETE_WIRE,
			new klsMessage::Message_DELETE_WIRE(wireId)));
	}

	gCanvas->removeWire(wireIds[0]);
	gCircuit->deleteWire(wireIds[0]);

	return true;
}

bool cmdCreateWire::validateBusLines() const {

	IDType gate1Id = conn1->getGateId();
	IDType gate2Id = conn2->getGateId();

	guiGate *gate1 = (*gCircuit->getGates())[gate1Id];
	guiGate *gate2 = (*gCircuit->getGates())[gate2Id];

	std::string hotspot1 = conn1->getHotspot();
	std::string hotspot2 = conn2->getHotspot();

	int busLines1 = gate1->getHotspot(hotspot1)->getBusLines();
	int busLines2 = gate2->getHotspot(hotspot2)->getBusLines();

	return busLines1 == busLines2 && busLines1 == wireIds.size();
}

const vector<IDType> & cmdCreateWire::getWireIds() const {
	return wireIds;
}

std::string cmdCreateWire::toString() const {
	cmdser::CreateWire cw;
	cw.wireIds = wireIds;
	cw.conn1 = { conn1->getWireId(), conn1->getGateId(), conn1->getHotspot() };
	cw.conn2 = { conn2->getWireId(), conn2->getGateId(), conn2->getHotspot() };
	return cmdser::emit(cw);
}

void cmdCreateWire::setPointers(GUICircuit* gCircuit, GUICanvas* gCanvas,
	TranslationMap &gateids, TranslationMap &wireids) {

	// remap ids.
	// notice the difference between wireIds and wireids.
	for (IDType &id : wireIds) {
		if (wireids.find(id) != wireids.end()) {
			id = wireids[id];
		}
		else {
			wireids[id] = gCircuit->getNextAvailableWireID();
			id = wireids[id];
		}
	}

	conn1->setPointers(gCircuit, gCanvas, gateids, wireids);
	conn2->setPointers(gCircuit, gCanvas, gateids, wireids);
	this->gCircuit = gCircuit;
	this->gCanvas = gCanvas;
}