/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   klsClipboard: handles copy and paste of blocks
*****************************************************************************/

#include "klsClipboard.h"
#include "OscopeFrame.h"
#include <fstream>
#include <map>
#include <unordered_map>   // removed .h  KAS

#include "MainApp.h"
#include "commands.h"
#include "cmdSerialize.h"
#include "cmdRegistry.h"
#include "GUICanvas.h"
#include "GUICircuit.h"
#include "guiGate.h"
#include "guiWire.h"
#include "wx/clipbrd.h"
#include "wx/dataobj.h"

class clipboardCtx {
public:
	bool valid;

	clipboardCtx() {
		valid = wxTheClipboard->Open();
	}

	virtual ~clipboardCtx() {
		if (valid) wxTheClipboard->Close();
	}
};

// Paste-specific tweak (Colin Broberg, 10/6/16): when a single gate is pasted,
// auto-increment a trailing number on its JUNCTION_ID param so repeated pastes
// don't collide, and rewrite the clipboard so the next paste keeps counting.
// Skipped when the block holds more than one gate, or when Shift is held.
// Mutates `line` (the setparams command text) in place.
static void autoIncrementJunctionId(string &line, const string &pasteText) {
	// More than one creategate in the block, or Shift held -> leave it alone.
	if (pasteText.find("creategate", pasteText.find("creategate") + 1) != string::npos ||
		wxGetKeyState(WXK_SHIFT)) {
		return;
	}

	// Gather the run of digits at the end of the line. line always ends in a
	// tab, so start one character before it.
	string numEnd;
	for (int i = line.length() - 2; i > 0; i--) {
		if (isdigit(line[i])) numEnd = line[i] + numEnd;
		else break;
	}
	if (numEnd == "" || line.find("JUNCTION_ID") == string::npos) return;

	string newPasteText = pasteText; // rewritten to the clipboard so subsequent pastes keep incrementing
	line.erase(line.length() - 1 - numEnd.length(), numEnd.length() + 1);                  // drop the number and its trailing tab
	newPasteText.erase(newPasteText.length() - 2 - numEnd.length(), numEnd.length() + 2);  // same, plus the newline

	string s = to_string(stoi(numEnd) + 1) + "\t"; // the point of it all: bump the trailing number by 1
	line += s;
	newPasteText += s + "\n";
	wxTheClipboard->AddData(new wxTextDataObject(newPasteText));
}

cmdPasteBlock* klsClipboard::pasteBlock( GUICircuit* gCircuit, GUICanvas* gCanvas ) {
	clipboardCtx clipboard;
	if ( !clipboard.valid || !wxTheClipboard->IsSupported(wxDF_UNICODETEXT) ) {
		return NULL;
	}

    wxTextDataObject text;
    vector < klsCommand* > cmdList;
    if ( wxTheClipboard->GetData(text) ) {
    	string pasteText = text.GetText().ToStdString();
    	if (pasteText.find('\n',0) == string::npos) return NULL;
    	istringstream iss(pasteText);
    	string temp;
		
		TranslationMap gateids;
		TranslationMap wireids;
    	while (getline( iss, temp, '\n' )) {
    		// The setparams line for a lone pasted gate gets its JUNCTION_ID
    		// bumped before the command is rebuilt (this also rewrites the
    		// clipboard so the next paste keeps counting).
    		if (cmdser::keyword(temp) == "setparams")
    			autoIncrementJunctionId(temp, pasteText);

    		// The registry picks the concrete command from the line's keyword.
    		// An unrecognized keyword ends the block, as the old chain did.
    		klsCommand* cg = cmd::fromLine(temp);
    		if (cg == NULL) break;
    		cmdList.push_back( cg );
    		cg->setPointers( gCircuit, gCanvas, gateids, wireids );
    		cg->Do();
    	}
		gCanvas->unselectAllGates();
		gCanvas->unselectAllWires();
		TranslationMap::iterator gateWalk = gateids.begin();
		while (gateWalk != gateids.end()) {
			(*(gCircuit->getGates()))[gateWalk->second]->select();
			gateWalk++;
		}
		TranslationMap::iterator wireWalk = wireids.begin();
		while (wireWalk != wireids.end()) {
			guiWire *wire = (*(gCircuit->getWires()))[wireWalk->second];
			if (wire != nullptr) {
				wire->select();
			}
			wireWalk++;
		}
		gCircuit->getOscope()->UpdateMenu();
    }

	if (cmdList.size() > 0) return new cmdPasteBlock ( cmdList );
	return NULL;
}

void klsClipboard::copyBlock( GUICircuit* gCircuit, GUICanvas* gCanvas, vector < unsigned long > gates, vector < unsigned long > wires ) {
	if (gates.size() == 0) return;
	ostringstream oss;
	klsCommand* cmdTemp;
	map < unsigned long, unsigned long > connectWireList;
	// Write strings to copy gates
	for (unsigned int i = 0; i < gates.size(); i++) {
		// generate list of wire connections
		map < string, GLPoint2f > hotspotmap = (*(gCircuit->getGates()))[gates[i]]->getHotspotList();
		map < string, GLPoint2f >::iterator hsmapWalk = hotspotmap.begin();
		while (hsmapWalk != hotspotmap.end()) {
			if ( (*(gCircuit->getGates()))[gates[i]]->isConnected(hsmapWalk->first) )connectWireList[(*(gCircuit->getGates()))[gates[i]]->getConnection(hsmapWalk->first)->getID()]++;
			hsmapWalk++;
		}
		// Creation of a gate takes care of type, position, id; all other items are in params
		float x, y;
		(*(gCircuit->getGates()))[gates[i]]->getGLcoords(x,y);
		cmdTemp = new cmdCreateGate( gCanvas, gCircuit, gates[i], (*(gCircuit->getGates()))[gates[i]]->getLibraryGateName(), x, y);
		oss << cmdTemp->toString() << endl;
		delete cmdTemp;
		guiGate* gGate = (*(gCircuit->getGates()))[gates[i]];
		cmdTemp = new cmdSetParams( gCircuit, gates[i], paramSet(gGate->getAllGUIParams(), gGate->getAllLogicParams()) );
		oss << cmdTemp->toString() << endl;
		delete cmdTemp;
	}
	// For wires, only copy if more than one active connection, and trim shape
	vector < guiWire* > copyWires;
	map < unsigned long, unsigned long >::iterator wireWalk = connectWireList.begin();
	while (wireWalk != connectWireList.end()) {
		if ( wireWalk->second < 2 ) { wireWalk++; continue; }
		guiWire* wire = new guiWire();
		wire->setCircuit(gCircuit); // so removeConnection/setSegmentMap can resolve gids to gates
		// Set the IDs
		wire->setIDs( (*gCircuit->getWires())[wireWalk->first]->getIDs() );
		// Shove all the connections
		vector < wireConnection > wireConns = (*(gCircuit->getWires()))[wireWalk->first]->getConnections();
		for (unsigned int i = 0; i < wireConns.size(); i++) wire->addConnection( gCircuit->getGate(wireConns[i].gid), wireConns[i].connection, true );
		// Now get the segment map copy
		wire->setSegmentMap( (*(gCircuit->getWires()))[wireWalk->first]->getSegmentMap() );
		// Now that we have a good copy of the wire object, we can trim the connections that we don't want to carry over
		for (unsigned int i = 0; i < wireConns.size(); i++) {
			bool found = false;
			for (unsigned int j = 0; j < gates.size() && !found; j++) if (gates[j] == wireConns[i].gid) found = true;
			if (found) continue; // we found this connection; don't trim it
			// get rid of it
			wire->removeConnection( gCircuit->getGate(wireConns[i].gid), wireConns[i].connection );
		}
		// Wire should now have a completely valid shape to copy, shove it on the vector
		copyWires.push_back(wire);
		wireWalk++;
	}
	// Now actually generate copy of wire
	for (unsigned int i = 0; i < copyWires.size(); i++) {
		vector < wireConnection > wconns = copyWires[i]->getConnections();
		// now generate the connections - connections 1 and 2 must be passed to create the wire
		//	after which all connections may be done in succession.
		cmdConnectWire *conn1 = new cmdConnectWire(gCircuit, copyWires[i]->getID(), wconns[0].gid, wconns[0].connection);
		cmdConnectWire *conn2 = new cmdConnectWire(gCircuit, copyWires[i]->getID(), wconns[1].gid, wconns[1].connection);
		cmdTemp = new cmdCreateWire(gCanvas, gCircuit, gCircuit->getWires()->at(copyWires[i]->getID())->getIDs(), conn1, conn2);
		oss << cmdTemp->toString() << endl;
		delete cmdTemp;
		for (unsigned int j = 2; j < wconns.size(); j++) {
			cmdTemp = new cmdConnectWire(gCircuit, copyWires[i]->getID(), wconns[j].gid, wconns[j].connection);
			oss << cmdTemp->toString() << endl;
			delete cmdTemp;
		}
		// now track the wire's shape:
		cmdTemp = new cmdMoveWire(gCircuit, copyWires[i]->getID(), copyWires[i]->getSegmentMap(), copyWires[i]->getSegmentMap());
		oss << cmdTemp->toString() << endl;
		delete cmdTemp;
		delete copyWires[i];
	}
	if (!wxTheClipboard->Open()) return;
	wxTheClipboard->AddData(new wxTextDataObject(oss.str()));
	wxTheClipboard->Close();
}
