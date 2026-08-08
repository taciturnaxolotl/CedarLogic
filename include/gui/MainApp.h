/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   MainApp: Main application object
*****************************************************************************/

#ifndef MAINAPP_H_
#define MAINAPP_H_

#include "wx/wxprec.h"
#include "wx/wx.h"
#include "wx/thread.h"
#include "wx/image.h"
#include "wx/docview.h"
#include "wx/help.h"
#include "wx/html/helpctrl.h"
#include "wx/fs_zip.h"
#include "wx/glcanvas.h"
#include "threadLogic.h"
#include "autoSaveThread.h"
#include "logic_values.h"
#include "LibraryParse.h"
#include "gl_defs.h"
#include "klsMessage.h"
#include <deque>
#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <cmath>

#include "Settings.h"
#include "SimBridge.h"
#include "PaletteDrag.h"
#include "RenderMode.h"

class MainFrame;

using namespace std;

// ApplicationSettings moved to Settings.h with the Settings service (WS C).

class MainApp : public wxApp {
public:
	MainApp();
	virtual bool OnInit();
	virtual int OnExit();
	void SetCurrentCanvas(wxGLCanvas *canvas);
#ifdef __WXOSX__
	virtual void MacOpenFile(const wxString& fileName);
	string pendingOpenFile;
#endif

public:
    // The GUI<->logic threading state (locks, message queues, semaphores,
    // sim timer, thread pointers) moved to the SimBridge service (Workstream C);
    // reach it via simBridge() (see SimBridge.h). The gate libraries and app
    // settings likewise moved to gateLibrary() and appConfig().

    // Help system
#ifdef __APPLE__
    wxHtmlHelpController* helpController;
#else
    wxHelpController* helpController;
#endif
    
	// Palette drag state (newGateToDrag / showDragImage) moved to the PaletteDrag
	// service, and the render-mode flags (doingBitmapExport / headlessRender) to
	// the RenderMode service (Workstream C); via paletteDrag()/renderMode().

	ofstream logfile;

	//this pointer is added so that pop-ups can
	//resume simulation
	MainFrame* mainframe;

private:
	void loadSettings( void );

	// opengl context used for all rendering
	wxGLContext *glContext;
};

#endif /*MAINAPP_H_*/
