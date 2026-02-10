/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                    Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.

   PaletteFrame: Organizes PaletteCanvas objects
*****************************************************************************/

#include "PaletteFrame.h"
#include "wx/choice.h"

using namespace std;

DECLARE_APP(MainApp)


BEGIN_EVENT_TABLE(PaletteFrame, wxPanel)
	EVT_CHOICE(ID_LISTBOX, PaletteFrame::OnListSelect)
END_EVENT_TABLE()


PaletteFrame::PaletteFrame( wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size )
	: wxPanel( parent, id, pos, size, wxNO_BORDER ) {
	paletteSizer = new wxBoxSizer( wxVERTICAL );
	map < string, map < string, LibraryGate > >::iterator libWalk = wxGetApp().libraries.begin();
	while (libWalk != wxGetApp().libraries.end()) {
		strings.Add(libWalk->first);
		libWalk++;
	}
	sectionChoice = new wxChoice(this, ID_LISTBOX, wxDefaultPosition, wxDefaultSize, strings);
	sectionChoice->SetSelection(0);
	paletteSizer->Add( sectionChoice, wxSizerFlags(0).Expand().Border(wxALL, 4) );
	for (unsigned int i = 0; i < strings.GetCount(); i++) {
		PaletteCanvas* paletteCanvas = new PaletteCanvas( this, wxID_ANY, strings[i], wxDefaultPosition, wxDefaultSize );
		paletteSizer->Add( paletteCanvas, wxSizerFlags(1).Expand().Border(wxALL, 0) );
		paletteSizer->Hide( paletteCanvas );
		pcanvases[strings[i]] = paletteCanvas;
	}
	currentPalette = pcanvases[strings[0]];
	paletteSizer->Show( currentPalette );
	this->SetSizer( paletteSizer );
}

void PaletteFrame::OnListSelect( wxCommandEvent& evt ) {
	int sel = sectionChoice->GetSelection();
	if (sel != wxNOT_FOUND) {
		paletteSizer->Hide( currentPalette );
		currentPalette = pcanvases[strings[sel]];
		paletteSizer->Show( currentPalette );
		paletteSizer->Layout();
		currentPalette->Activate();
	}
}

PaletteFrame::~PaletteFrame() {
	map < wxString, PaletteCanvas* >::iterator canvasWalk = pcanvases.begin();
	while (canvasWalk != pcanvases.end()) {
		delete canvasWalk->second;
		canvasWalk++;
	}
}
