
#pragma once
#include "klsCommand.h"
#include <stack>
#include <vector>

#ifdef __WXOSX__
class wxNotebook;
#else
class wxAuiNotebook;
#endif

//JV - cmdDeleteTab - delete a tab from canvasBook
class cmdDeleteTab : public klsCommand {
public:
#ifdef __WXOSX__
	cmdDeleteTab(GUICircuit* gCircuit, GUICanvas* gCanvas, wxNotebook* book,
		std::vector<GUICanvas *> *canvases, unsigned long ID);
#else
	cmdDeleteTab(GUICircuit* gCircuit, GUICanvas* gCanvas, wxAuiNotebook* book,
		std::vector<GUICanvas *> *canvases, unsigned long ID);
#endif

	virtual ~cmdDeleteTab();

	bool Do();

	bool Undo();

protected:
	std::vector < unsigned long > gates;
	std::vector < unsigned long > wires;
	std::stack < klsCommand* > cmdList;
#ifdef __WXOSX__
	wxNotebook* canvasBook;
#else
	wxAuiNotebook* canvasBook;
#endif
	std::vector< GUICanvas* >* canvases;
	unsigned long canvasID;

};