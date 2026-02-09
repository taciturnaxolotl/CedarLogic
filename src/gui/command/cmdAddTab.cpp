
#include "cmdAddTab.h"
#ifdef __WXOSX__
#include "wx/notebook.h"
#else
#include "wx/aui/auibook.h"
#endif
#include "../GUICanvas.h"

#ifdef __WXOSX__
cmdAddTab::cmdAddTab(GUICircuit* gCircuit, wxNotebook* book,
		std::vector<GUICanvas *> *canvases) :
			klsCommand(true, "Add Tab") {
#else
cmdAddTab::cmdAddTab(GUICircuit* gCircuit, wxAuiNotebook* book,
		std::vector<GUICanvas *> *canvases) :
			klsCommand(true, "Add Tab") {
#endif

	this->gCircuit = gCircuit;
	this->canvasBook = book;
	this->canvases = canvases;
}

bool cmdAddTab::Do() {
	canvases->push_back(new GUICanvas(canvasBook, gCircuit, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS));
	wxString oss;
	oss << "Page " << canvases->size();
	canvasBook->AddPage(canvases->at(canvases->size() - 1), oss, (false));
	return true;
}

bool cmdAddTab::Undo() {
	canvases->erase(canvases->end() - 1);
	canvasBook->DeletePage(canvasBook->GetPageCount() - 1);
	return true;
}