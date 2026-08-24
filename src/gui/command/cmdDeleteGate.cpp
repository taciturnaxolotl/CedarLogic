
#include "cmdDeleteGate.h"
#include "../GateLibrary.h"
#include <map>
#include "../gl_defs.h"
#include "../GUICircuit.h"
#include "../CircuitPage.h"
#include "../guiWire.h"
#include "../guiGate.h"
#include "../MainApp.h"
#include "cmdDisconnectWire.h"
#include "cmdDeleteWire.h"
#include "cmdMoveGate.h"
#include "cmdSetParams.h"

DECLARE_APP(MainApp);

cmdDeleteGate::cmdDeleteGate(GUICircuit* gCircuit, CircuitPage* gCanvas,
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
			//Joshua Lansford edit 11/02/06--Added so "buffer" ports on a gate don't contain
			//wire artifacts after the rest of the wire has been deleted.  A buffer is created
			//by haveing a input and output hotspot in the same location. 
			//if the number of things the wire has left to connect is only two, then delete the wire.

			//first thing we verify is that we only have two connections left.
			if (gWire->numConnections() == 2) {
				//now we get the gid from both those connections. The old code
				//re-fetched the wire by id here and wondered in a comment why;
				//the lookup returns the very pointer gWire already holds.
				std::vector < wireConnection > connections = gWire->getConnections();
				if (connections[0].gid == connections[1].gid) {

					//now we have to make sure that the connections are the same pin by comparing their positions
					guiGate* possibleBuffGate = gCircuit->getGate(connections[0].gid);
					std::string* hotspot1Name = &connections[0].connection;
					std::string* hotspot2Name = &connections[1].connection;

					float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
					possibleBuffGate->getHotspotCoords(*hotspot1Name, x1, y1);
					possibleBuffGate->getHotspotCoords(*hotspot2Name, x2, y2);

					if (x1 == x2 && y1 == y2) {
						//this wire has met the requierments for being cooked.
						//so we will scedual it for being delted.
						deleteWires.push_back(gWire->getID());
					}
				}
			}
			//coment on edit. I compiled and tested this edit.
			//It doesn't delete wires that connect two different pins
			//on one chip when a gate is deleted.  It does delete a wire
			//connecting and input and a output that are in the same location
			//when you delete another gate
			//end of edit---------------the else on the following if was added as well------------------
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
