
#include "cmdDeleteTab.h"
#ifdef __WXOSX__
#include "wx/notebook.h"
#else
#include "wx/aui/auibook.h"
#endif
#include "GUICanvas.h"

class guiGate;
class guiWire;

#ifdef __WXOSX__
cmdDeleteTab::cmdDeleteTab(GUICircuit* gCircuit, GUICanvas* gCanvas,
		wxNotebook* book, std::vector< GUICanvas* >* canvases,
		unsigned long ID) :
			klsCommand(true, "Delete Tab") {
#else
cmdDeleteTab::cmdDeleteTab(GUICircuit* gCircuit, GUICanvas* gCanvas,
		wxAuiNotebook* book, std::vector< GUICanvas* >* canvases,
		unsigned long ID) :
			klsCommand(true, "Delete Tab") {
#endif

	this->gCircuit = gCircuit;
	this->gCanvas = gCanvas;
	this->canvasBook = book;
	this->canvases = canvases;
	this->canvasID = ID;


	std::unordered_map< unsigned long, guiGate* >* gateList = gCanvas->getGateList();
	std::unordered_map< unsigned long, guiGate* >::iterator thisGate = gateList->begin();
	while (thisGate != gateList->end()) {
		this->gates.push_back(thisGate->first);
		thisGate++;
	}
	std::unordered_map< unsigned long, guiWire* >* wireList = gCanvas->getWireList();
	std::unordered_map< unsigned long, guiWire* >::iterator thisWire = wireList->begin();
	while (thisWire != wireList->end()) {
		this->wires.push_back(thisWire->first);
		thisWire++;
	}
}

cmdDeleteTab::~cmdDeleteTab() {
	while (!(cmdList.empty())) {
		cmdList.pop();
	}
}

bool cmdDeleteTab::Do() {
	cmdList.push(std::unique_ptr<klsCommand>(new cmdDeleteSelection(gCircuit, gCanvas, gates, wires)));
	cmdList.top()->Do();

	unsigned int canSize = canvases->size();
	//canvases->erase(canvases->begin() + canvasID);
	remove(canvases->begin(), canvases->end(), gCanvas);
	canvases->pop_back();
	if (canvasID < (canSize - 1)) {
		for (unsigned int i = canvasID; i < canSize; i++) {
			std::string text = "Page " + to_string(i);
			canvasBook->SetPageText(i, text);
		}
	}
	canvasBook->RemovePage(canvasID);
	//TODO fix canvases not refreshing
	gCanvas->Hide();
	return true;
}
bool cmdDeleteTab::Undo() {
	unsigned int canSize = canvases->size();
	canvases->insert(canvases->begin() + canvasID, gCanvas);
	wxString oss;
	oss << "Page " << canvasID + 1;
	canvasBook->InsertPage(canvasID, gCanvas, oss, false);
	if (canvasID < (canSize)) {
		for (unsigned int i = canvasID + 1; i < canSize + 1; i++) {
			std::string text = "Page " + to_string(i + 1);
			canvasBook->SetPageText(i, text);
		}
	}
	while (!(cmdList.empty())) {
		cmdList.top()->Undo();
		cmdList.pop();
	}
	return true;
}

int cmdDeleteTab::pageToShow(bool isUndo) const {
	// Undo re-inserts the tab at canvasID -- show the restored tab. Redo deletes
	// it -- show the tab that shifted into its slot, or the last if it was last.
	if (isUndo) return (int)canvasID;
	int last = (int)canvases->size() - 1;
	return (int)canvasID <= last ? (int)canvasID : last;
}