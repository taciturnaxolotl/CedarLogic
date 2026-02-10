/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                    Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.

   OscopeFrame: Docked panel for the Oscope
*****************************************************************************/

#ifndef OSCOPEFRAME_H_
#define OSCOPEFRAME_H_

class GUICircuit;
class OscopeFrame;

#include "MainApp.h"
#include "PaletteCanvas.h"
#include "wx/wxprec.h"
#include "wx/thread.h"
#include "wx/listbox.h"
#include "wx/toolbar.h"
#include "threadLogic.h"
#include "OscopeCanvas.h"
#include "wx/event.h"
#include <vector>

#define NONE_STR "[None]"

enum
{
	ID_OSCOPE_PAUSE = 6000,
	ID_OSCOPE_ADD,
	ID_OSCOPE_REMOVE,
	ID_OSCOPE_EXPORT,
	ID_OSCOPE_LOAD,
	ID_OSCOPE_SAVE,
	ID_OSCOPE_SIGNAL_MENU_BASE = 6100  // menu IDs for signal popup
};

using namespace std;

class GUICircuit;

class OscopeFrame : public wxPanel {
public:
    OscopeFrame(wxWindow *parent, GUICircuit* gCircuit);

	void UpdateData(void);
	void UpdateMenu(void);

	void OnPauseToggle( wxCommandEvent& event );
	void OnExport( wxCommandEvent& event );
	void OnLoad( wxCommandEvent& event );
	void OnSave( wxCommandEvent& event );
	void OnAddSignal( wxCommandEvent& event );
	void OnRemoveSignal( wxCommandEvent& event );
	void OnSignalMenuSelect( wxCommandEvent& event );

	void appendNewFeed( string newName );
	void setFeedName( int i, string newName );
	unsigned int numberOfFeeds();
	void removeFeed( int i );
	string getFeedName( int i );
	void cancelFeed( int i );
	int getFeedYPos( int i );
	void updatePossableFeeds( vector< string >* newPossabilities );

private:
	bool paused;
	vector<string> feedNames;
	vector<string> availableFeeds;

	GUICircuit* gCircuit;
	OscopeCanvas* theCanvas;
	wxBoxSizer* oSizer;
	wxListBox* signalList;
	wxToolBar* oscopeToolBar;
};

#endif /*OSCOPEFRAME_H_*/
