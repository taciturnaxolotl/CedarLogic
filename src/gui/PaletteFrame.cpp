/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   PaletteFrame: Organizes PaletteCanvas objects
*****************************************************************************/

#include "PaletteFrame.h"
#include "wx/listbox.h"

using namespace std;

DECLARE_APP(MainApp)


BEGIN_EVENT_TABLE(PaletteFrame, wxPanel)
	EVT_LISTBOX(ID_LISTBOX, PaletteFrame::OnListSelect)
END_EVENT_TABLE()


PaletteFrame::PaletteFrame( wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size )
	: wxPanel( parent, id, pos, size, wxNO_BORDER ) {
	paletteSizer = new wxBoxSizer( wxVERTICAL );
	map < string, map < string, LibraryGate > >::iterator libWalk = wxGetApp().libraries.begin();
	while (libWalk != wxGetApp().libraries.end()) {
		strings.Add(libWalk->first);
		libWalk++;
	}
	// Calculate minimum width based on longest string, with padding for scrollbar
	int minWidth = 120; // Reasonable default minimum
	for (unsigned int i = 0; i < strings.GetCount(); i++) {
		int textWidth, textHeight;
		GetTextExtent(strings[i], &textWidth, &textHeight);
		if (textWidth + 30 > minWidth) { // Add padding for scrollbar and margins
			minWidth = textWidth + 30;
		}
	}
	// Calculate height based on item count and actual font metrics
	int itemHeight = GetCharHeight() + 4; // Add some padding
#ifdef __WXOSX__
	listBox = new wxListBox(this, ID_LISTBOX, wxDefaultPosition, wxSize(minWidth, strings.GetCount() * itemHeight + 8), strings, wxLB_SINGLE | wxLB_NO_SB);
#else
	listBox = new wxListBox(this, ID_LISTBOX, wxDefaultPosition, wxSize(minWidth, strings.GetCount() * itemHeight), strings, wxLB_SINGLE);
#endif
	paletteSizer->Add( listBox, wxSizerFlags(0).Expand().Border(wxALL, 0) );
	paletteSizer->Show( listBox );
	for (unsigned int i = 0; i < strings.GetCount(); i++) {
		PaletteCanvas* paletteCanvas = new PaletteCanvas( this, wxID_ANY, strings[i], wxDefaultPosition, wxDefaultSize );
		paletteSizer->Add( paletteCanvas, wxSizerFlags(1).Expand().Border(wxALL, 0) );
		paletteSizer->Hide( paletteCanvas );
		pcanvases[strings[i]] = paletteCanvas;
	}
	listBox->SetFirstItem(0);
	currentPalette = pcanvases.begin()->second;
	paletteSizer->Show( currentPalette );
	this->SetSizer( paletteSizer );
}

void PaletteFrame::OnListSelect( wxCommandEvent& evt ) {
	for (unsigned int i = 0; i < strings.GetCount(); i++) {
		if (listBox->IsSelected(i)) {
			paletteSizer->Hide( currentPalette );
			currentPalette = pcanvases[strings[i]];
			paletteSizer->Show( currentPalette );
			paletteSizer->Layout();
			currentPalette->Activate();
			break;
		}
	}
}

PaletteFrame::~PaletteFrame() {
	map < wxString, PaletteCanvas* >::iterator canvasWalk = pcanvases.begin();
	while (canvasWalk != pcanvases.end()) {
		delete canvasWalk->second;
		canvasWalk++;
	}
}
