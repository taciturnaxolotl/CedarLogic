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

class MainFrame;

using namespace std;

struct ApplicationSettings {
	string gateLibFile;
	string textFontFile;
	string helpFile;
	string lastDir;
	int mainFrameWidth;
	int mainFrameHeight;
	int mainFrameLeft;
	int mainFrameTop;
	int timePerStep;
	int refreshRate;
    float wireConnRadius;
    bool wireConnVisible;
    bool gridlineVisible;
    bool rightClickRotate;
};

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
    // ---- Threading model ------------------------------------------------
    // Two background threads run alongside the wx GUI thread: threadLogic (the
    // simulation) and autoSaveThread. They coordinate through the shared state
    // below. Two locks nest in a fixed order: take m_critsect first, then
    // mexMessages -- both message-drain sites (threadLogic::checkMessages and
    // MainFrame::OnIdle) do exactly that. Preserve that order; taking them the
    // other way round risks deadlock.
    //
    // Message-drain protocol: each drain site takes mexMessages only long
    // enough to swap the pending deque into a local, then processes the local
    // with the lock released (so parseMessage may freely re-take mexMessages to
    // send a reply). This replaced an older TryLock+wxYield busy-spin that held
    // mexMessages across parseMessage -- which pumped the GUI event loop while
    // waiting (reentrancy) and only worked because the mutex was recursive.
    // Anything that clears these queues (New/Open) must also hold mexMessages;
    // the logic thread drains dGUItoLOGIC independently of the GUI timers.

    // Coarse lock for the background-thread lifecycle: guards the logicThread /
    // saveThread pointers (install in MainFrame, teardown in threadLogic::OnExit)
    // and wraps the message-drain sections below (held while mexMessages is taken).
    wxCriticalSection m_critsect;

    // Posted by a background thread as it exits so MainFrame::OnQuit() can block
    // until teardown has finished.
    wxSemaphore m_semAllDone;
	// GUI <-> logic run/step signaling semaphores.
	wxSemaphore simulate;
	wxSemaphore readyToSend;

	// Guards the two message queues below (both directions of GUI <-> logic).
	// Always taken while already holding m_critsect (see the drain sites).
	wxMutex mexMessages;
	deque< klsMessage::Message > dGUItoLOGIC;
	deque< klsMessage::Message > dLOGICtoGUI;
	// Guards wireStateBuffer, the batch of wire-state updates the logic thread
	// produces for the GUI to drain.
	wxMutex wireStateMutex;
	unordered_map<IDType, StateType> wireStateBuffer;
	// Use a stopwatch for timing between step calls
	wxStopWatch appSystemTime;
	unsigned long timeStepMod;
	
	// The gate libraries moved to the GateLibrary service (Workstream C);
	// reach them via gateLibrary() (see GateLibrary.h).

    // the last exiting thread should post to m_semAllDone if this is true
    // (protected by the same m_critsect)
    bool m_waitingUntilAllDone;
    
    // Help system
#ifdef __APPLE__
    wxHtmlHelpController* helpController;
#else
    wxHelpController* helpController;
#endif
    
    bool showDragImage;
	string newGateToDrag;
	
	ApplicationSettings appSettings;
	
	threadLogic* logicThread;
	autoSaveThread* saveThread;
	
	ofstream logfile;
	
	//this pointer is added so that pop-ups can
	//resume simulation
	MainFrame* mainframe;
	
	//this string is necisary when the working directory
	//is not were the executeable is.
	string resourcesDir;

	// OK, honestly, this shouldn't be here
	//	Basically exporting bitmaps doesn't like GL display
	//	lists, so we flag them
	bool doingBitmapExport;

	// Headless --render mode: load a circuit, dump a PNG, exit. Suppresses the
	// modal load dialogs (version / migration / convert) so nothing blocks.
	bool headlessRender = false;

private:
	void loadSettings( void );

	// opengl context used for all rendering
	wxGLContext *glContext;
};

#endif /*MAINAPP_H_*/
