
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
	// Reuse the canvas from the first Do, so a redo restores the very object
	// the rest of the undo history is still pointing at.
	if (addedCanvas == nullptr) {
		addedCanvas = new GUICanvas(canvasBook, gCircuit, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS);
	}
	canvases->push_back(addedCanvas);
	wxString oss;
	oss << "Page " << canvases->size();
	addedCanvas->Show();
	canvasBook->AddPage(addedCanvas, oss, (false));
	return true;
}

bool cmdAddTab::Undo() {
	canvases->erase(canvases->end() - 1);
	// RemovePage, not DeletePage: the latter destroys the window, and commands
	// still in the undo history hold pointers to it.
	canvasBook->RemovePage(canvasBook->GetPageCount() - 1);
	if (addedCanvas != nullptr) addedCanvas->Hide();
	return true;
}

int cmdAddTab::pageToShow(bool /*isUndo*/) const {
	// Redo shows the newly added (now last) tab; undo shows the new last tab
	// after the added one is removed. Both are the last existing page.
	return (int)canvases->size() - 1;
}