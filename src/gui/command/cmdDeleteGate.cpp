
#include "cmdDeleteGate.h"
#include "../GateLibrary.h"
#include <map>
#include "../gl_defs.h"
#include "../GUICircuit.h"
#include "../GUICanvas.h"
#include "../guiWire.h"
#include "../guiGate.h"
#include "../MainApp.h"
#include "cmdDisconnectWire.h"
#include "cmdDeleteWire.h"
#include "cmdMoveGate.h"
#include "cmdSetParams.h"

DECLARE_APP(MainApp);

namespace {

// A wire looping from a gate's input back to its output at the same point. Once
// the rest of the wire is gone such a wire is left as an artifact, so deleting
// the gate takes it too. (Joshua Lansford, 11/02/06.)
bool isBufferLoop(GUICircuit *gCircuit, guiWire *wire) {
	if (wire->numConnections() != 2) return false;
	std::vector< wireConnection > conns = wire->getConnections();
	if (conns[0].gid != conns[1].gid) return false;
	guiGate *gate = gCircuit->getGate(conns[0].gid);
	if (gate == nullptr) return false; // a wire naming a gate the circuit lost
	float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
	gate->getHotspotCoords(conns[0].connection, x1, y1);
	gate->getHotspotCoords(conns[1].connection, x2, y2);
	return x1 == x2 && y1 == y2;
}

}  // namespace

cmdDeleteGate::cmdDeleteGate(GUICircuit* gCircuit, GUICanvas* gCanvas,
		IDType gateId) :
			klsCommand(true, "Delete Gate") {

	this->gCircuit = gCircuit;
	this->gCanvas = gCanvas;
	this->gateId = gateId;
}

cmdDeleteGate::~cmdDeleteGate() {

	while (!(cmdList.empty())) {
		cmdList.pop();
	}
}

bool cmdDeleteGate::Do() {

	//make sure the gate exists
	guiGate* gGate = gCircuit->getGate(gateId);
	if (gGate == nullptr) return false; //error: gate not found
	std::map<std::string, GLPoint2f> gateConns = gGate->getHotspotList();
	std::map<std::string, GLPoint2f>::iterator connWalk = gateConns.begin();
	std::vector < int > deleteWires;
	//we will need to disconect all wires that connect to that gate from that gate
	//we iterate over the connections
	while (connWalk != gateConns.end()) {
		//if the connection is actually connected...
		if (gGate->isConnected(connWalk->first)) {
			//grab the wire on that connection
			guiWire* gWire = gGate->getConnection(connWalk->first);
			//create a disconnect command and do it
			cmdDisconnectWire* disconn = new cmdDisconnectWire(gCircuit, gWire->getID(), gateId, connWalk->first);
			cmdList.push(std::unique_ptr<klsCommand>(disconn));
			disconn->Do();

			//----------------------------------------------------------------------------------------
			if (isBufferLoop(gCircuit, gWire)) deleteWires.push_back(gWire->getID());
			else if (gWire->numConnections() < 2) deleteWires.push_back(gWire->getID());
		}
		connWalk++;
	}

	for (unsigned int i = 0; i < deleteWires.size(); i++) {
		cmdDeleteWire* delwire = new cmdDeleteWire(gCircuit, gCanvas, deleteWires[i]);
		cmdList.push(std::unique_ptr<klsCommand>(delwire));
		delwire->Do();
	}

	float x, y;
	gGate->getGLcoords(x, y);
	cmdList.push(std::unique_ptr<klsCommand>(new cmdMoveGate(gCircuit, gateId, x, y, x, y)));
	cmdList.push(std::unique_ptr<klsCommand>(new cmdSetParams(gCircuit, gateId, paramSet(gGate->getAllGUIParams(), gGate->getAllLogicParams()), true)));

	gateType = gGate->getLibraryGateName();

	gCanvas->removeGate(gateId);
	gCircuit->deleteGate(gateId, true);
	std::string logicType = gateLibrary().libParser.getGateLogicType(gateType);
	if (logicType.size() > 0) {
		gCircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_DELETE_GATE, new klsMessage::Message_DELETE_GATE(gateId)));
	}
	return true;
}

bool cmdDeleteGate::Undo() {
	gCircuit->createGate(gateType, gateId, true);

	std::string logicType = gateLibrary().libParser.getGateLogicType(gateType);
	if (logicType.size() > 0) {
		gCircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_CREATE_GATE, new klsMessage::Message_CREATE_GATE(logicType, gateId)));
	}
	gCanvas->insertGate(gateId, gCircuit->getGate(gateId), 0, 0);

	while (!(cmdList.empty())) {
		cmdList.top()->Undo();
		cmdList.pop();
	}
	return true;
}
