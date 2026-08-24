/*****************************************************************************
   Project: CEDAR Logic Simulator

   guiGateDialogs: the gate methods that open wxWidgets dialogs.
*****************************************************************************/

// These three methods are the only place the gate model reaches for a window.
// Keeping them out of guiGate.cpp is what lets that file -- and the whole gate
// model with it -- compile with no wxWidgets at all, which is how the core
// builds for WebAssembly. Only the desktop target compiles this file; the
// browser shell answers a parameter edit with its own UI and submits the same
// cmdSetParams.

#include "guiGate.h"
#include "GateLibrary.h"
#include "GUICircuit.h"
#include "paramDialog.h"
#include "RamPopupDialog.h"

void guiGate::doParamsDialog( void* gc, wxCommandProcessor* wxcmd ) {
	if (gateLibrary().libraries[libName][libGateName].dlgParams.size() == 0) return;
#ifdef __WXOSX__
	paramDialog* myDialog = new paramDialog("Parameters", gc, this, wxcmd);
	myDialog->Bind(wxEVT_WINDOW_MODAL_DIALOG_CLOSED, [myDialog](wxWindowModalDialogEvent&) {
		myDialog->Destroy();
	});
	myDialog->ShowWindowModal();
#else
	paramDialog myDialog("Parameters", gc, this, wxcmd);
	myDialog.SetFocus();
	myDialog.ShowModal();
#endif
}

guiGateRAM::~guiGateRAM(){	
	//Destroy is how you 'delete' wxwidget objects
	if( ramPopupDialog != NULL ){
		ramPopupDialog->Destroy();
		ramPopupDialog = NULL;
	}
}

void guiGateRAM::doParamsDialog( void* gc, wxCommandProcessor* wxcmd ){
	if( ramPopupDialog == NULL ){
		ramPopupDialog = new RamPopupDialog( this, addressBits, (GUICircuit*)gc );
		ramPopupDialog->updateGridDisplay();
	}
	ramPopupDialog->Show( true );
}

void guiGateRAM::notifyMemoryChanged( bool all ) {
	if( ramPopupDialog == NULL ) return;
	if( all ) ramPopupDialog->notifyAllChanged();
	else      ramPopupDialog->updateGridDisplay();
}
