/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   PaletteCanvas: Renders the gateImage objects in a palette
*****************************************************************************/

#include "PaletteCanvas.h"
#include "GateLibrary.h"
#include "logic_values.h"
#include <wx/settings.h>

using namespace std;

DECLARE_APP(MainApp)


BEGIN_EVENT_TABLE(PaletteCanvas, wxScrolledWindow)
  EVT_PAINT(PaletteCanvas::OnPaint)
END_EVENT_TABLE()


PaletteCanvas::PaletteCanvas( wxWindow *parent, wxWindowID id, wxString &libName, const wxPoint &pos, const wxSize &size )
	: wxScrolledWindow( parent, id, pos, size, wxSUNKEN_BORDER|wxVSCROLL|wxFULL_REPAINT_ON_RESIZE ) {
    SetBackgroundColour(* wxWHITE);

    SetCursor(wxCursor(wxCURSOR_ARROW));
	SetBackgroundColour(* wxWHITE);

#ifdef __WXMSW__
	// On Windows the native vertical scrollbar is carved out of the client area,
	// and the palette is sized to exactly three gate columns -- so when the bar
	// appears it lands on top of the last column. Reserve its width in the
	// panel's minimum so the gates and the bar sit side by side. (macOS uses
	// overlay scrollbars that take no layout space and already render correctly,
	// so this is Windows-only.)
	int sbWidth = wxSystemSettings::GetMetric( wxSYS_VSCROLL_X );
	if ( sbWidth <= 0 ) sbWidth = 17;
	const int contentW = 3 * ( IMAGESIZE + 2 ) + 2;  // 3 bordered columns + row border
	SetMinClientSize( wxSize( contentW + sbWidth, -1 ) );
#endif

	libraryName = libName.ToStdString();
	gateSizer = NULL;
	
	init = false;
	activate = true;
}

PaletteCanvas::~PaletteCanvas() {
	for (unsigned int i = 0; i < gates.size(); i++) delete gates[i];
}

void PaletteCanvas::OnPaint( wxPaintEvent &event ) {
	wxPaintDC dc(this);
	if (!init) {
	   	map < string, LibraryGate >::iterator gateWalk = gateLibrary().libraries[libraryName].begin();
		int counter = 0;
		wxBoxSizer* lineSizer = NULL;
		gateSizer = new wxBoxSizer( wxVERTICAL );
		while (gateWalk != gateLibrary().libraries[libraryName].end()) {
			if (!(counter%3)) {
				lineSizer = new wxBoxSizer( wxHORIZONTAL );
				gateSizer->Add( lineSizer, wxSizerFlags(0).Border(wxALL, 1) );			
			}	
			gateImage* newGate = new gateImage((gateWalk->first), this, wxID_ANY, wxDefaultPosition, wxSize(IMAGESIZE, IMAGESIZE));
			gates.push_back(newGate);
			lineSizer->Add( newGate, wxSizerFlags(0).Border(wxALL, 1) );
			counter++;
			gateWalk++;
		}
		this->SetSizer( gateSizer );
		// Fine scroll rate: the scroll unit is the smallest step the wheel and
		// the scrollbar thumb move by. It used to be a whole gate row
		// (IMAGESIZE+1), which made scrolling lurch a row at a time; a small step
		// gives smooth wheel scrolling and a fine-grained drag.
		this->SetScrollRate(0, 16);
		gateSizer->Layout();
		init = true;
	}
	if (activate) {
		this->FitInside();
		this->Scroll(0,0); // reset position because the sizer is dumb
		gateSizer->Layout();
		activate = false;
	}
}
void PaletteCanvas::Activate() {
	activate = true;
}
