
#pragma once
#include "klsCommand.h"
#include <vector>

#ifdef __WXOSX__
class wxNotebook;
#else
class wxAuiNotebook;
#endif

//JV - cmdAddTab - add a new tab into canvasBook
class cmdAddTab : public klsCommand {
public:
#ifdef __WXOSX__
	cmdAddTab(GUICircuit* gCircuit, wxNotebook* book,
		std::vector<GUICanvas *> *canvases);
#else
	cmdAddTab(GUICircuit* gCircuit, wxAuiNotebook* book,
		std::vector<GUICanvas *> *canvases);
#endif

	bool Do();

	bool Undo();

private:
#ifdef __WXOSX__
	wxNotebook* canvasBook;
#else
	wxAuiNotebook* canvasBook;
#endif
	std::vector<GUICanvas *>* canvases;
};