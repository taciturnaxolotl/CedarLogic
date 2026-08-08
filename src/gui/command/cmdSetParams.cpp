
#include "cmdSetParams.h"
#include "../GUICircuit.h"
#include "../guiGate.h"
#include "../OscopeFrame.h"
#include "../MainApp.h"
#include "cmdSerialize.h"

DECLARE_APP(MainApp);

paramSet::paramSet(ParameterMap *g, ParameterMap* l) {
	gParams = g;
	lParams = l;
};

cmdSetParams::cmdSetParams(GUICircuit* gCircuit, unsigned long gid,
		paramSet pSet, bool setMode) :
			klsCommand(true, "Set Parameter") {

	if ((gCircuit->getGates())->find(gid) == (gCircuit->getGates())->end()) return; // error: gate not found
	this->gCircuit = gCircuit;
	this->gid = gid;
	this->fromString = setMode;
	// Save the original set of parameters
	map < string, string >::iterator paramWalk = (*(gCircuit->getGates()))[gid]->getAllGUIParams()->begin();
	while (paramWalk != (*(gCircuit->getGates()))[gid]->getAllGUIParams()->end()) {
		oldGUIParamList[paramWalk->first] = paramWalk->second;
		paramWalk++;
	}
	paramWalk = (*(gCircuit->getGates()))[gid]->getAllLogicParams()->begin();
	while (paramWalk != (*(gCircuit->getGates()))[gid]->getAllLogicParams()->end()) {
		oldLogicParamList[paramWalk->first] = paramWalk->second;
		paramWalk++;
	}
	// Now grab the new ones...
	if (pSet.gParams != NULL) {
		paramWalk = pSet.gParams->begin();
		while (paramWalk != pSet.gParams->end()) {
			newGUIParamList[paramWalk->first] = paramWalk->second;
			paramWalk++;
		}
	}
	if (pSet.lParams != NULL) {
		paramWalk = pSet.lParams->begin();
		while (paramWalk != pSet.lParams->end()) {
			newLogicParamList[paramWalk->first] = paramWalk->second;
			paramWalk++;
		}
	}
}

cmdSetParams::cmdSetParams(string def) : klsCommand(true, "Set Parameter") {

	this->fromString = true;
	cmdser::SetParams d;
	cmdser::parse(def, d);
	gid = d.gid;
	// The string form only carries the "new" values; old == new, as before.
	newGUIParamList = d.guiParams;
	oldGUIParamList = d.guiParams;
	newLogicParamList = d.logicParams;
	oldLogicParamList = d.logicParams;
}

bool cmdSetParams::Do() {

	if ((gCircuit->getGates())->find(gid) == (gCircuit->getGates())->end()) return false; // error: gate not found
	map < string, string >::iterator paramWalk = newLogicParamList.begin();
	vector < string > dontSendMessages;
	LibraryGate lg = wxGetApp().libraries[(*(gCircuit->getGates()))[gid]->getLibraryName()][(*(gCircuit->getGates()))[gid]->getLibraryGateName()];
	for (unsigned int i = 0; i < lg.dlgParams.size(); i++) {
		if (lg.dlgParams[i].isGui) continue;
		if (lg.dlgParams[i].type == "FILE_IN" || lg.dlgParams[i].type == "FILE_OUT") dontSendMessages.push_back(lg.dlgParams[i].name);
	}
	while (paramWalk != newLogicParamList.end()) {
		(*(gCircuit->getGates()))[gid]->setLogicParam(paramWalk->first, paramWalk->second);
		bool found = false;
		for (unsigned int i = 0; i < dontSendMessages.size() && !found; i++) {
			if (dontSendMessages[i] == paramWalk->first) found = true;
		}
		if (!found) gCircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_PARAM, new klsMessage::Message_SET_GATE_PARAM(gid, paramWalk->first, paramWalk->second)));
		paramWalk++;
	}
	paramWalk = newGUIParamList.begin();
	while (paramWalk != newGUIParamList.end()) {
		(*(gCircuit->getGates()))[gid]->setGUIParam(paramWalk->first, paramWalk->second);
		paramWalk++;
	}
	if (!fromString && (*(gCircuit->getGates()))[gid]->getGUIType() == "TO" && gCircuit->getOscope() != NULL) gCircuit->getOscope()->UpdateMenu();
	return true;
}

bool cmdSetParams::Undo() {

	if ((gCircuit->getGates())->find(gid) == (gCircuit->getGates())->end()) return false; // error: gate not found
	map < string, string >::iterator paramWalk = oldLogicParamList.begin();
	vector < string > dontSendMessages;
	LibraryGate lg = wxGetApp().libraries[(*(gCircuit->getGates()))[gid]->getLibraryName()][(*(gCircuit->getGates()))[gid]->getLibraryGateName()];
	for (unsigned int i = 0; i < lg.dlgParams.size(); i++) {
		if (lg.dlgParams[i].isGui) continue;
		if (lg.dlgParams[i].type == "FILE_IN" || lg.dlgParams[i].type == "FILE_OUT") dontSendMessages.push_back(lg.dlgParams[i].name);
	}
	while (paramWalk != oldLogicParamList.end()) {
		(*(gCircuit->getGates()))[gid]->setLogicParam(paramWalk->first, paramWalk->second);
		bool found = false;
		for (unsigned int i = 0; i < dontSendMessages.size() && !found; i++) {
			if (dontSendMessages[i] == paramWalk->first) found = true;
		}
		if (!found) gCircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_PARAM, new klsMessage::Message_SET_GATE_PARAM(gid, paramWalk->first, paramWalk->second)));
		paramWalk++;
	}
	paramWalk = oldGUIParamList.begin();
	while (paramWalk != oldGUIParamList.end()) {
		(*(gCircuit->getGates()))[gid]->setGUIParam(paramWalk->first, paramWalk->second);
		paramWalk++;
	}
	if (!fromString && (*(gCircuit->getGates()))[gid]->getGUIType() == "TO") gCircuit->getOscope()->UpdateMenu();
	return true;
}

string cmdSetParams::toString() const {

	return cmdser::emit(cmdser::SetParams{ gid, newGUIParamList, newLogicParamList });
}

void cmdSetParams::setPointers(GUICircuit* gCircuit, GUICanvas* gCanvas,
		TranslationMap &gateids, TranslationMap &wireids) {

	gid = gateids[gid];
	this->gCircuit = gCircuit;
	this->gCanvas = gCanvas;
}