/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   MainFrame: Main frame object
*****************************************************************************/

#include "MainApp.h"
#include "PaletteDrag.h"
#include "RenderMode.h"
#ifdef WITH_SKIA
#include "render/SkiaProbe.h"    // skiaRenderToPng (no Skia headers leak here)
#include "render/Scene.h"
#include "render/RenderStyle.h"
#endif
#include "SimBridge.h"
#include "Settings.h"
#include "GateLibrary.h"
#include "guiText.h"
#include "guiWire.h"
#include <fstream>
#include "MainFrame.h"
#include "guiGate.h"
#include "guiWire.h"
#include "GUICircuit.h"
#include <fstream>
#include <algorithm>
#ifdef WITH_AVOID
#include "avoid/RoutingService.h"      // headless --avoid-route (libavoid obstacle router)
#include "avoid/PolylineToSegments.h"  // route polyline -> segment tree (3.2c)
#endif
#include "wx/filedlg.h"
#include "wx/timer.h"
#include "wx/wfstream.h"
#include "wx/image.h"
#include "wx/thread.h"
#include "wx/toolbar.h"
#include "wx/artprov.h"
#include "wx/clipbrd.h"
#include "wx/dataobj.h"
#include "wx/config.h"
#include "wx/checkbox.h"
#include "wx/radiobox.h"
#include "wx/stattext.h"
#include "wx/sizer.h"
#include "wx/dialog.h"
#include "wx/button.h"
#include "wx/bmpbndl.h"
#include "wx/artprov.h"
#include "wx/settings.h"
#include "wx/file.h"
#include "CircuitParse.h"
#include "OscopeFrame.h"
#include "SettingsDialog.h"
#include "wx/docview.h"
#include "commands.h"
#include "autoSaveThread.h"
#include "svgExport.h"
#include "../version.h"
#ifdef __APPLE__
#include "SparkleUpdater.h"
#include "NativeIcons.h"
#endif
#ifdef _WIN32
#include "WinSparkleUpdater.h"
#endif

DECLARE_APP(MainApp)

// How often the sim/idle timers poll (ms). Kept at roughly half the refresh-rate
// target (appSettings.refreshRate, ~16 ms) so the render cadence can actually
// reach it: the cadence is a round-trip through two of these timers -- OnTimer
// sends a step, the sim thread replies, OnIdle drains the reply and repaints --
// so with two poll intervals per frame, ~8 ms gives a ~16 ms round-trip (~60
// fps). The old 20 ms poll was coarser than the target and capped it near 16
// fps. Simulation *speed* is unaffected: each step advances by
// elapsed-time/timeStepMod (with the remainder carried over), not per-tick, so
// a finer poll just means smaller, more frequent steps -- not a faster sim.
static const int TIMER_POLL_MS = 8;

BEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_MENU(wxID_EXIT,  MainFrame::OnQuit)
    EVT_MENU(wxID_ABOUT, MainFrame::OnAbout)
    EVT_MENU(wxID_HELP_CONTENTS, MainFrame::OnHelpContents)
    EVT_MENU(Help_KeyboardShortcuts, MainFrame::OnKeyboardShortcuts)
    EVT_MENU(wxID_NEW, MainFrame::OnNew)
    EVT_MENU(wxID_OPEN, MainFrame::OnOpen)
    EVT_MENU(wxID_SAVE, MainFrame::OnSave)
    EVT_MENU(wxID_SAVEAS, MainFrame::OnSaveAs)
	EVT_MENU(File_Export, MainFrame::OnExportBitmap)
	EVT_MENU(File_ExportLegacy, MainFrame::OnExportLegacy)
	EVT_MENU(File_ExportV2, MainFrame::OnExportV2)
	EVT_MENU(File_ClipCopy, MainFrame::OnCopyToClipboard)
	
	EVT_MENU(wxID_UNDO, MainFrame::OnUndo)
	EVT_MENU(wxID_REDO, MainFrame::OnRedo)
	EVT_MENU(wxID_COPY, MainFrame::OnCopy)
	EVT_MENU(wxID_PASTE, MainFrame::OnPaste)
	
    EVT_MENU(View_Oscope, MainFrame::OnOscope)
    EVT_MENU(View_Gridline, MainFrame::OnViewGridline)
    EVT_MENU(View_SkiaRenderer, MainFrame::OnViewSkiaRenderer)
    EVT_MENU(View_WireConn, MainFrame::OnViewWireConn)
    EVT_MENU(View_RightClickRotate, MainFrame::OnViewRightClickRotate)
    EVT_MENU(View_Preferences, MainFrame::OnPreferences)
    
	EVT_TOOL(Tool_Pause, MainFrame::OnPause)
	EVT_TOOL(Tool_Step, MainFrame::OnStep)
	EVT_TOOL(Tool_ZoomIn, MainFrame::OnZoomIn)
	EVT_TOOL(Tool_ZoomOut, MainFrame::OnZoomOut)
	EVT_SCROLL(MainFrame::OnTimeStepModSlider)
	EVT_TOOL(Tool_Lock, MainFrame::OnLock)
	EVT_TOOL(Tool_NewTab, MainFrame::OnNewTab)

	//EVT_MENU(Help_ReportABug, MainFrame::OnReportABug)
	//EVT_MENU(Help_RequestAFeature, MainFrame::OnRequestAFeature)
	EVT_MENU(Help_DownloadLatestVersion, MainFrame::OnDownloadLatestVersion)
	
    //EVT_SIZE(MainFrame::OnSize)
    //EVT_MAXIMIZE(MainFrame::OnMaximize)
    
	EVT_TIMER(TIMER_ID, MainFrame::OnTimer)
	EVT_TIMER(IDLETIMER_ID, MainFrame::OnIdle)

#ifndef __WXOSX__
	EVT_AUINOTEBOOK_PAGE_CHANGED(NOTEBOOK_ID, MainFrame::OnNotebookPage)
	EVT_AUINOTEBOOK_PAGE_CLOSE(NOTEBOOK_ID, MainFrame::OnDeleteTab)
#endif
	
	EVT_CLOSE(MainFrame::OnClose)
END_EVENT_TABLE()

#define ID_TEXTCTRL 5001

// Global print data object:
wxPrintData *g_printData = (wxPrintData*) NULL;


MainFrame::MainFrame(const wxString& title, string cmdFilename)
       : wxFrame(NULL, wxID_ANY, title, wxDefaultPosition, wxSize(1800,900))
{
    // set the frame icon
    //SetIcon(wxICON(sample));
	currentCanvas = nullptr;

	// Set default locations
	if (appConfig().appSettings.lastDir == "") lastDirectory = wxGetHomeDir();
	else lastDirectory = appConfig().appSettings.lastDir;  // added cast KAS

	//////////////////////////////////////////////////////////////////////////
    // create a menu bar
	//////////////////////////////////////////////////////////////////////////
    wxMenu *fileMenu = new wxMenu; // FILE MENU
	fileMenu->Append(wxID_NEW, "&New\tCtrl+N", "Create new circuit");
	fileMenu->Append(wxID_OPEN, "&Open\tCtrl+O", "Open circuit");
	fileMenu->Append(wxID_SAVE, "&Save\tCtrl+S", "Save circuit");
	fileMenu->Append(wxID_SAVEAS, "Save &As\tCtrl+Shift+S", "Save circuit");
	fileMenu->AppendSeparator();
	fileMenu->Append(File_Export, "Export as Image...\tCtrl+E", "Export or copy circuit image");
	fileMenu->Append(File_ExportV2, "Export as V2 (legacy XML)...", "Save a copy in the pre-V3 XML format");
	fileMenu->Append(File_ExportLegacy, "Export as V1.x Compatible...", "Save a copy in the oldest format");
	fileMenu->AppendSeparator();
	fileMenu->Append(wxID_EXIT, "E&xit\tAlt+X", "Quit this program");

    wxMenu *viewMenu = new wxMenu; // VIEW MENU
    viewMenu->Append(View_Oscope, "&Oscope\tCtrl+G", "Show the Oscope");
    wxMenu *settingsMenu = new wxMenu;
    settingsMenu->AppendCheckItem(View_Gridline, "Display Gridlines", "Toggle gridline display");
    settingsMenu->AppendCheckItem(View_WireConn, "Display Wire Connection Points", "Toggle wire connection points");
    settingsMenu->AppendCheckItem(View_RightClickRotate, "Right-Click Rotate", "Toggle right-click to rotate gates");
    settingsMenu->AppendCheckItem(View_SkiaRenderer, "Skia Renderer", "Render the canvas with the Skia engine");
    settingsMenu->AppendSeparator();
    settingsMenu->Append(View_Preferences, "Preferences...\tCtrl+,", "Open preferences dialog");
    viewMenu->AppendSeparator();
    viewMenu->AppendSubMenu(settingsMenu, "Settings");
    
    wxMenu *helpMenu = new wxMenu; // HELP MENU
    helpMenu->Append(wxID_HELP_CONTENTS, "&Contents...\tF1", "Show Help system");
	helpMenu->Append(Help_KeyboardShortcuts, "&Keyboard Shortcuts...", "Show keyboard shortcuts");
	helpMenu->AppendSeparator();
	//helpMenu->Append(Help_ReportABug, "Report a bug...");
	//helpMenu->Append(Help_RequestAFeature, "Request a feature...");
#if defined(__APPLE__) || defined(_WIN32)
	helpMenu->Append(Help_DownloadLatestVersion, "Check for Updates...");
#else
	helpMenu->Append(Help_DownloadLatestVersion, "Download latest version...");
#endif
	helpMenu->AppendSeparator();
    helpMenu->Append(wxID_ABOUT, "&About...", "Show about dialog");

	wxMenu *editMenu = new wxMenu; // EDIT MENU
	editMenu->Append(wxID_UNDO, "Undo\tCtrl+Z", "Undo last operation");
	editMenu->Append(wxID_REDO, "Redo", "Redo last operation");
	editMenu->AppendSeparator();
	editMenu->Append(Tool_NewTab, "New Tab\tCtrl+T", "New Tab");
#ifdef __WXOSX__
	editMenu->Append(Tool_CloseTab, "Close Tab\tCtrl+W", "Close current tab");
#endif
	editMenu->AppendSeparator();
	editMenu->Append(wxID_COPY, "Copy\tCtrl+C", "Copy selection to clipboard");
	editMenu->Append(wxID_PASTE, "Paste\tCtrl+V", "Paste selection from clipboard");
	
    // now append the freshly created menu to the menu bar...
    wxMenuBar *menuBar = new wxMenuBar();
    menuBar->Append(fileMenu, "&File");
    menuBar->Append(editMenu, "&Edit");
    menuBar->Append(viewMenu, "&View");
    menuBar->Append(helpMenu, "&Help");

    // set checkmarks on settings menu
    menuBar->Check(View_Gridline, appConfig().appSettings.gridlineVisible);
    menuBar->Check(View_WireConn, appConfig().appSettings.wireConnVisible);
#ifdef WITH_SKIA
    menuBar->Check(View_SkiaRenderer, appConfig().appSettings.useSkiaRenderer);
#else
    // No Skia in this build: reflect that and disable the toggle.
    appConfig().appSettings.useSkiaRenderer = false;
    menuBar->Check(View_SkiaRenderer, false);
    menuBar->Enable(View_SkiaRenderer, false);
#endif
    menuBar->Check(View_RightClickRotate, appConfig().appSettings.rightClickRotate);
    
    // ... and attach this menu bar to the frame
    SetMenuBar(menuBar);
    
	//////////////////////////////////////////////////////////////////////////
    // parse a gate library
	//////////////////////////////////////////////////////////////////////////
	string libPath = appConfig().appSettings.gateLibFile;
	LibraryParse newLib(libPath);
	gateLibrary().libParser = newLib;
	
	//////////////////////////////////////////////////////////////////////////
    // create a toolbar
	//////////////////////////////////////////////////////////////////////////
	toolBar = new wxToolBar(this, TOOLBAR_ID, wxPoint(0,0), wxDefaultSize, wxTB_HORIZONTAL|wxNO_BORDER| wxTB_FLAT);

#ifdef __WXOSX__
	// On macOS, use native SF Symbols for toolbar icons (requires macOS 11+)
	auto sfSymbol = [](const char* name) -> wxBitmap {
		wxBitmap bmp = NativeIcon_GetSFSymbol(name, 18);
		if (bmp.IsOk()) return bmp;
		return wxArtProvider::GetBitmap(wxART_QUESTION, wxART_TOOLBAR);
	};

	toolBar->AddTool(wxID_NEW, "New", sfSymbol("doc.badge.plus"), "New");
	toolBar->AddTool(wxID_OPEN, "Open", sfSymbol("folder"), "Open");
	toolBar->AddTool(wxID_SAVE, "Save", sfSymbol("square.and.arrow.down"), "Save");
	toolBar->AddSeparator();
	toolBar->AddTool(wxID_UNDO, "Undo", sfSymbol("arrow.uturn.backward"), "Undo");
	toolBar->AddTool(wxID_REDO, "Redo", sfSymbol("arrow.uturn.forward"), "Redo");
	toolBar->AddSeparator();
	toolBar->AddTool(wxID_COPY, "Copy", sfSymbol("doc.on.doc"), "Copy");
	toolBar->AddTool(wxID_PASTE, "Paste", sfSymbol("clipboard"), "Paste");
	toolBar->AddSeparator();
	toolBar->AddTool(Tool_ZoomIn, "Zoom In", sfSymbol("plus.magnifyingglass"), "Zoom In");
	toolBar->AddTool(Tool_ZoomOut, "Zoom Out", sfSymbol("minus.magnifyingglass"), "Zoom Out");
	toolBar->AddSeparator();
	pauseIcon = sfSymbol("pause.fill");
	playIcon = sfSymbol("play.fill");
	toolBar->AddTool(Tool_Pause, "Pause/Resume", pauseIcon, "Pause/Resume", wxITEM_CHECK);
	toolBar->AddTool(Tool_Step, "Step", sfSymbol("forward.frame.fill"), "Step");
	timeStepModSlider = new wxSlider(toolBar, wxID_ANY, appConfig().timeStepMod, 1, 500, wxDefaultPosition, wxSize(125,-1), wxSL_HORIZONTAL);
	wxString oss;
	oss << appConfig().timeStepMod << "ms";
	timeStepModVal = new wxStaticText(toolBar, wxID_ANY, oss, wxDefaultPosition, wxSize(45, -1), wxSUNKEN_BORDER | wxALIGN_RIGHT | wxST_NO_AUTORESIZE);
	// Label + tooltip so it's clear this sets the simulation step size / speed.
	wxStaticText* timeStepModLabel = new wxStaticText(toolBar, wxID_ANY, "Sim step ");
	const wxString stepTip = "Simulation time per step (ms). Lower = faster simulation, higher = slower.";
	timeStepModLabel->SetToolTip(stepTip);
	timeStepModSlider->SetToolTip(stepTip);
	timeStepModVal->SetToolTip(stepTip);
	toolBar->AddControl( timeStepModLabel );
	toolBar->AddControl( timeStepModSlider );
	toolBar->AddControl( timeStepModVal );
	toolBar->AddSeparator();
	lockedIcon = sfSymbol("lock.fill");
	unlockedIcon = sfSymbol("lock.open.fill");
	toolBar->AddTool(Tool_Lock, "Lock state", unlockedIcon, "Lock state", wxITEM_CHECK);
	toolBar->AddSeparator();
	toolBar->AddTool(wxID_ABOUT, "About", sfSymbol("info.circle"), "About");
	toolBar->AddSeparator();
	toolBar->AddTool(Tool_NewTab, "New Tab", sfSymbol("plus.square"), "New Tab");
	toolBar->AddStretchableSpace();
#else
	// On Windows/Linux, load modern SVG icons via wxBitmapBundle (crisp at any DPI).
	// The SVGs are authored with a #333333 stroke/fill; recolor at load time so
	// the icons stay legible on both a light and a dark toolbar.
	const wxSize iconSize(24, 24);
	const bool darkMode = wxSystemSettings::GetAppearance().IsDark();
	const wxString iconColor = darkMode ? "#E6E6E6" : "#333333";
	auto svgIcon = [&](const char* name) -> wxBitmapBundle {
		wxString path = appConfig().resourcesDir + "res/icons/" + name + ".svg";
		wxFile f(path);
		wxString svg;
		if (f.IsOpened() && f.ReadAll(&svg)) {
			svg.Replace("#333333", iconColor);
			// FromSVG takes a mutable buffer (nanosvg parses it in place).
			wxScopedCharBuffer buf = svg.utf8_str();
			wxBitmapBundle b = wxBitmapBundle::FromSVG(buf.data(), iconSize);
			if (b.IsOk()) return b;
		}
		wxBitmapBundle b = wxBitmapBundle::FromSVGFile(path, iconSize);
		if (b.IsOk()) return b;
		return wxBitmapBundle(wxArtProvider::GetBitmap(wxART_QUESTION, wxART_TOOLBAR));
	};

	toolBar->SetToolBitmapSize(iconSize);
	toolBar->AddTool(wxID_NEW, "New", svgIcon("new"), "New");
	toolBar->AddTool(wxID_OPEN, "Open", svgIcon("open"), "Open");
	toolBar->AddTool(wxID_SAVE, "Save", svgIcon("save"), "Save");
	toolBar->AddSeparator();
	toolBar->AddTool(wxID_UNDO, "Undo", svgIcon("undo"), "Undo");
	toolBar->AddTool(wxID_REDO, "Redo", svgIcon("redo"), "Redo");
	toolBar->AddSeparator();
	toolBar->AddTool(wxID_COPY, "Copy", svgIcon("copy"), "Copy");
	toolBar->AddTool(wxID_PASTE, "Paste", svgIcon("paste"), "Paste");
	toolBar->AddSeparator();
	toolBar->AddTool(Tool_ZoomIn, "Zoom In", svgIcon("zoomin"), "Zoom In");
	toolBar->AddTool(Tool_ZoomOut, "Zoom Out", svgIcon("zoomout"), "Zoom Out");
	toolBar->AddSeparator();
	pauseIcon = svgIcon("pause").GetBitmap(iconSize);
	playIcon = svgIcon("play").GetBitmap(iconSize);
	toolBar->AddTool(Tool_Pause, "Pause/Resume", pauseIcon, "Pause/Resume", wxITEM_CHECK);
	toolBar->AddTool(Tool_Step, "Step", svgIcon("step"), "Step");
	timeStepModSlider = new wxSlider(toolBar, wxID_ANY, appConfig().timeStepMod, 1, 500, wxDefaultPosition, wxSize(125,-1), wxSL_HORIZONTAL);
	wxString oss;
	oss << appConfig().timeStepMod << "ms";
	timeStepModVal = new wxStaticText(toolBar, wxID_ANY, oss, wxDefaultPosition, wxSize(45, -1), wxSUNKEN_BORDER | wxALIGN_RIGHT | wxST_NO_AUTORESIZE);
	// Label + tooltip so it's clear this sets the simulation step size / speed.
	wxStaticText* timeStepModLabel = new wxStaticText(toolBar, wxID_ANY, "Sim step ");
	const wxString stepTip = "Simulation time per step (ms). Lower = faster simulation, higher = slower.";
	timeStepModLabel->SetToolTip(stepTip);
	timeStepModSlider->SetToolTip(stepTip);
	timeStepModVal->SetToolTip(stepTip);
	toolBar->AddControl( timeStepModLabel );
	toolBar->AddControl( timeStepModSlider );
	toolBar->AddControl( timeStepModVal );
	toolBar->AddSeparator();
	lockedIcon = svgIcon("locked").GetBitmap(iconSize);
	unlockedIcon = svgIcon("unlocked").GetBitmap(iconSize);
	toolBar->AddTool(Tool_Lock, "Lock state", unlockedIcon, "Lock state", wxITEM_CHECK);
	toolBar->AddSeparator();
	toolBar->AddTool(wxID_ABOUT, "About", svgIcon("about"), "About");
	toolBar->AddSeparator();
	toolBar->AddTool(Tool_NewTab, "New Tab", svgIcon("newtab"), "New Tab");
#endif
	SetToolBar(toolBar);
	toolBar->Show(true);

    CreateStatusBar(2);
    SetStatusText("");

	mainSizer = new wxBoxSizer( wxHORIZONTAL );
	wxBoxSizer* leftPaneSizer = new wxBoxSizer( wxVERTICAL );
	wxSize sz = this->GetClientSize();
	
	// now a gate palette for the library
	gatePalette = new PaletteFrame(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
	leftPaneSizer->Add( gatePalette, wxSizerFlags(1).Expand().Border(wxALL, 0) );
	leftPaneSizer->Show( gatePalette );
	miniMap = new klsMiniMap(this, wxID_ANY, wxDefaultPosition, wxSize(130, 100));
	leftPaneSizer->Add( miniMap, wxSizerFlags(0).Expand().Border(wxALL, 0) );
	mainSizer->Add( leftPaneSizer, wxSizerFlags(0).Expand().Border(wxALL, 0) );
	
	// set up the panel and make canvases
	gCircuit = new GUICircuit();
	commandProcessor = new wxCommandProcessor();
	gCircuit->SetCommandProcessor(commandProcessor);
	gCircuit->GetCommandProcessor()->SetEditMenu(editMenu);
	gCircuit->GetCommandProcessor()->Initialize();

	// Create splitter for canvasBook (top) and oscope (bottom)
	rightSplitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_3D | wxSP_LIVE_UPDATE);
	rightSplitter->SetMinimumPaneSize(100);

#ifdef __WXOSX__
	canvasBook = new wxNotebook(rightSplitter, NOTEBOOK_ID, wxDefaultPosition, wxSize(400,400), wxNB_TOP);
#else
	canvasBook = new wxAuiNotebook(rightSplitter, NOTEBOOK_ID, wxDefaultPosition, wxSize(400,400), wxAUI_NB_CLOSE_ON_ACTIVE_TAB| wxAUI_NB_SCROLL_BUTTONS);
#endif

	//add 1 tab: Left loop to allow for different default
	for (int i = 0; i < 1; i++) {
		canvases.push_back(new GUICanvas(canvasBook, gCircuit, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS));
		wxString oss;
		oss << "Page " << (i+1);
		canvasBook->AddPage(canvases[i], oss);
	}

	currentCanvas = canvases[0];
	gCircuit->setCurrentCanvas(currentCanvas);
	currentCanvas->setMinimap(miniMap);
	currentCanvas->SetFocus();

	// Initialize splitter showing only canvasBook (oscope hidden)
	rightSplitter->Initialize(canvasBook);
	mainSizer->Add( rightSplitter, wxSizerFlags(1).Expand().Border(wxALL, 0) );

	SetSizer( mainSizer);
		
	threadLogic *thread = CreateThread();
	autoSaveThread *autoThread = CreateSaveThread();
	
    if ( thread->Run() != wxTHREAD_NO_ERROR )
    {
       wxLogError("Can't start thread!");
    }
	
	simTimer = new wxTimer(this, TIMER_ID);
	idleTimer = new wxTimer(this, IDLETIMER_ID);
	stopTimers();
	startTimers(TIMER_POLL_MS);

	// Setup the "Maximize Catch" flag:
	sizeChanged = false;
	
	oscopePanel = new OscopeFrame(rightSplitter, gCircuit);
	oscopePanel->Hide();
	gCircuit->setOscope(oscopePanel);
	
	toolBar->Realize();

	// Create the print data object:
	g_printData = new wxPrintData;
	g_printData->SetOrientation(wxLANDSCAPE);
	
	this->SetSize( appConfig().appSettings.mainFrameLeft, appConfig().appSettings.mainFrameTop, appConfig().appSettings.mainFrameWidth, appConfig().appSettings.mainFrameHeight );

#ifdef __WXOSX__
	canvasBook->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, &MainFrame::OnNotebookPage, this);
	Bind(wxEVT_MENU, &MainFrame::OnCloseTab, this, Tool_CloseTab);
#endif

	// Show the main window
	Show(true);

#ifdef __WXOSX__
	NativeWindow_ConfigureTitleBar(this);
#endif

	doOpenFile = (cmdFilename.size() > 0);
	this->openedFilename = cmdFilename;

	if (ifstream(CRASH_FILENAME)) {
		wxMessageDialog dialog(this, "Oops! It seems like there may have been a crash.\nWould you like to try to recover your work?", "Recover File", wxYES_DEFAULT | wxYES_NO | wxICON_QUESTION);
		if (dialog.ShowModal() == wxID_YES)
		{
			doOpenFile = false;
			openedFilename = "Recovered File";
			load(CRASH_FILENAME);
			this->SetTitle(VERSION_TITLE() + " - " + openedFilename);
		}
		removeTempFile();
	}

	if (autoThread->Run() != wxTHREAD_NO_ERROR)
	{
		wxLogError("Autosave thread not started!");
	}
	currentTempNum = 0;
	handlingEvent = false;
	wxInitAllImageHandlers(); //Julian: Added to allow saving all types of image files

	// Colin: for testing dynamic gates
	//DynamicGate* dg = new DynamicGate(currentCanvas, gCircuit, gCircuit->getNextAvailableGateID(), 3, 0, 0, "AND");
}

MainFrame::~MainFrame() {
	
	saveSettings();
	
	stopTimers();

	// Shut down the detached thread and wait for it to exit
	simBridge().logicThread->Delete();
	simBridge().saveThread->Delete();

	
	simBridge().m_semAllDone.Wait();
	
	
	
	// Delete the various objects
	delete wxGetApp().helpController;
	wxGetApp().helpController = NULL;
	
	
	
	//Edit by Joshua Lansford 10/18/2007.
	//Commented out the delete on the toolbar.
	//wxWidets auto deletes toolBars.  See the destructor for
	//wxFrame.
	//delete toolBar;
	
	
	delete gCircuit;
	gCircuit = NULL;
	
	//Joshua Lansford Edit 10/18/07
	//Removed the delete of systemTime because it was causeing a
	//crash on close.  In stead, I changed it from a pointer
	//to a local var so that it would not need to be deleted.

	delete simTimer;
	simTimer = NULL;
	delete idleTimer;
	idleTimer = NULL;
	delete g_printData;
	g_printData = NULL;
}

threadLogic *MainFrame::CreateThread()
{
	threadLogic *thread = new threadLogic();
    if ( thread->Create() != wxTHREAD_NO_ERROR )
    {
        wxLogError("Can't create thread!");
    }

    wxCriticalSectionLocker enter(simBridge().m_critsect);
	simBridge().logicThread = thread;
	
    return thread;
}

autoSaveThread *MainFrame::CreateSaveThread()
{
	autoSaveThread *thread = new autoSaveThread();
	if (thread->Create() != wxTHREAD_NO_ERROR)
	{
		wxLogError("Can't create autosave thread!");
	}

	wxCriticalSectionLocker enter(simBridge().m_critsect);
	simBridge().saveThread = thread;

	return thread;
}


// event handlers

void MainFrame::OnClose(wxCloseEvent& event) {
	//Edit by Joshua Lansford 10/18/07
	//Calling Destroy is not what was crashing the system.
	//Deleting simBridge().appSystemTime in MainFrame::~MainFrame
	//was crashing the system.  This problem was solved by
	//replaceing the pointer appSystemTime with the non pointer
	//appSystemTime.  Now it doesn't need to be deleted, and
	//the system doesn't crash on close.
	
	//If Destroy is replaced with Close, then you get in
	//an infinite loop because Close calls this method we
	//are in right now.

	//This synopsis is wrong... See comment above. ~JEL 10/18/07
	
	// The call to destroy the application window was causing abnormal
	// termination.  I'm not sure why, but I'm guessing that after the
	// window was destroyed, there was another reference to the window
	// object (or one of its children) as the application closed down.
	// I modified this event handler to note the destroy request with
	// the static boolean variable below, and then in the presence of
	// such a request close the window.  This more gentle manner of
	// termination seems to allow time for whatever needs to clean up
	// so that the application terminates normally.  KAS 4/26/07
	static bool destroy = false;
	handlingEvent = true;
	
	pauseTimers();

	// Allow the user to save the file, unless we are in the midst of terminating the app!!, KAS 4/26/07	
	if (commandProcessor->IsDirty() && !destroy) {
		wxMessageDialog dialog( this, "Circuit has not been saved.  Would you like to save it?", "Save Circuit", wxYES_DEFAULT|wxYES_NO|wxCANCEL|wxICON_QUESTION);
		switch (dialog.ShowModal()) {
		case wxID_YES:
			OnSave(*((wxCommandEvent*)(&event)));
			destroy = true;  // postpone destruction until wxWidgets cleans up, KAS 4/26/07
			break;
		case wxID_NO:
			destroy = true;  // postpone destruction until wxWidgets cleans up, KAS 4/26/07
			break;
		case wxID_CANCEL:
			if (event.CanVeto()) event.Veto(); else destroy = true;
			break;
		}			
	} else {
		destroy = true;      // postpone destruction until wxWidgets cleans up, KAS 4/26/07
	}
	
	resumeTimers(TIMER_POLL_MS);

	if (destroy)
	{
		removeTempFile();
	}
	else
	{
		handlingEvent = false;
	}

	//Edit by Joshua Lansford 10/18/07
	//KAS replaced the destroy method with a close method.
	//However the close method simply calls the method we
	//are currently in.  This causes an ininitue loop which
	//wxWidgets detects and terminates.
	//While this did remove the crash on close, it did so
	//by makeing the program simple give up before it ever got
	//to the crashing code.  The destructor of the MainFrame
	//never was being called and the save settings function
	//in it never was being called.
	//See 
	//http://www.wxwidgets.org/manuals/stable/wx_windowdeletionoverview.html
	if (destroy) this->Destroy();
}

void MainFrame::OnQuit(wxCommandEvent& WXUNUSED(event)) {
    // true is to force the frame to close, so pass false to allow OnClose to handle
    Close(false);
}

void MainFrame::OnAbout(wxCommandEvent& WXUNUSED(event)) {
    wxString msg;
    msg.Printf(VERSION_ABOUT_TEXT().c_str());

    wxMessageBox(msg, "About", wxOK | wxICON_INFORMATION, this);
}

void MainFrame::OnNew(wxCommandEvent& event) {
	handlingEvent = true;

	if (commandProcessor->IsDirty()) {
		wxMessageDialog dialog( this, "Circuit has not been saved.  Would you like to save it?", "Save Circuit", wxYES_DEFAULT|wxYES_NO|wxCANCEL|wxICON_QUESTION);
		switch (dialog.ShowModal()) {
		case wxID_YES:
			OnSave(event);
			break;
		case wxID_CANCEL:
			return;
		}			
	}

	pauseTimers();

	// Clear the message queues under the lock -- the logic thread drains
	// dGUItoLOGIC on its own (waking on msgForLogic) and pauseTimers() only stops
	// the GUI timers, not that thread, so an unlocked clear() races it.
	{
		wxMutexLocker lock(simBridge().mexMessages);
		simBridge().dGUItoLOGIC.clear();
		simBridge().dLOGICtoGUI.clear();
	}

	for (unsigned int i = 0; i < canvases.size(); i++) canvases[i]->clearCircuit();
	gCircuit->reInitializeLogicCircuit();
	commandProcessor->ClearCommands();
	commandProcessor->SetMenuStrings();
	//JV - Added so that new starts with one tab
	for (unsigned int j = canvases.size() - 1; j > 0; j--) {
		canvasBook->DeletePage(j);
		canvases.erase(canvases.end() - 1);
	}

	currentCanvas->Update(); // Render();
	this->SetTitle(VERSION_TITLE()); // KAS
	removeTempFile();
	currentTempNum++;
    openedFilename = "";
	loadedFileFormat = 3;  // a fresh circuit saves as v3
	saveFormatDecided = false;

	resumeTimers(TIMER_POLL_MS);

	handlingEvent = false;
}

void MainFrame::OnOpen(wxCommandEvent& event) {
	
	handlingEvent = true;

	currentCanvas->getCircuit()->setSimulate(false);
	if (commandProcessor->IsDirty()) {
		wxMessageDialog dialog( this, "Circuit has not been saved.  Would you like to save it?", "Save Circuit", wxYES_DEFAULT|wxYES_NO|wxCANCEL|wxICON_QUESTION);
		switch (dialog.ShowModal()) {
		case wxID_YES:
			OnSave(event);
			break;
		case wxID_CANCEL:
			currentCanvas->getCircuit()->setSimulate(true);
			handlingEvent = false;
			return;
		}			
	}
	
	pauseTimers();

	wxString caption = "Open a circuit";
	wxString wildcard = "Circuit files (*.cdl)|*.cdl";
	wxString defaultFilename = "";
	wxFileDialog dialog(this, caption, wxEmptyString, defaultFilename, wildcard, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	dialog.SetDirectory(lastDirectory);
	
	
	if (dialog.ShowModal() == wxID_OK) {
		lastDirectory = dialog.GetDirectory();
		loadCircuitFile(dialog.GetPath().ToStdString());
	}
    currentCanvas->Update(); // Render();
	currentCanvas->getCircuit()->setSimulate(true);

	resumeTimers(TIMER_POLL_MS);

	handlingEvent = false;
}
//Edit by Joshua Lansford 2/15/07
//Purpose of edit:  by obstracting the loading of
//circuit files out of the onOpen rutine,
//I can now make it so that if a circuit file
//is specified as an argument to cedarls when
//it starts, that cedarls can load that file
//by calling this method.
void MainFrame::loadCircuitFile( string fileName ){
	wxString path = fileName;
	
	openedFilename = path;
	this->SetTitle(VERSION_TITLE() + " - " + path );
	// Clear the queues under the lock -- the logic thread drains dGUItoLOGIC
	// concurrently, so an unlocked flush races it.
	{
		wxMutexLocker lock(simBridge().mexMessages);
		simBridge().dGUItoLOGIC.clear();
		simBridge().dLOGICtoGUI.clear();
	}
	for (unsigned int i = 0; i < canvases.size(); i++) canvases[i]->clearCircuit();
	gCircuit->reInitializeLogicCircuit();
	commandProcessor->ClearCommands();
	commandProcessor->SetMenuStrings();
	//JV - Delete all but the first tab
	for (unsigned int j = canvases.size() - 1; j > 0; j--) {
		canvasBook->DeletePage(j);
		canvases.erase(canvases.end()-1);
	}
	
    CircuitParse cirp(path.ToStdString(), canvases);
	canvases = cirp.parseFile();
	loadedFileFormat = cirp.getLoadedFormatCode();
	saveFormatDecided = false;  // a freshly opened file hasn't been answered yet

	//JV - Put pages back into canvas book
	for (unsigned int i = 1; i < canvases.size(); i++) 
	{
		ostringstream oss;
		oss << "Page " << (i + 1);
		canvasBook->AddPage(canvases[i], oss.str(), (i == 0 ? true : false));
	}
	currentCanvas = canvases[0];
	gCircuit->setCurrentCanvas(currentCanvas);
	currentCanvas->setMinimap(miniMap);
	mainSizer->Show(rightSplitter);
	currentCanvas->SetFocus();

	removeTempFile();

	// Offer to migrate an older file to the latest format up front. Declining
	// leaves it undecided, so the same choice is offered again when they save.
	// Skip the crash-recovery file.
	if ((loadedFileFormat == 1 || loadedFileFormat == 2) && fileName != CRASH_FILENAME
	    && !renderMode().headlessRender) {
		wxString v = (loadedFileFormat == 1) ? "V1" : "V2";
		wxMessageDialog dialog(this,
			"This circuit was saved in an older file format (" + v + ").\n\n"
			"Convert it to V3 now? If not, you can convert it later when you save.",
			"Older File Format", wxYES_NO | wxICON_QUESTION);
		dialog.SetYesNoLabels("Convert to V3", "Not Now");
		if (dialog.ShowModal() == wxID_YES) {
			CircuitParse saver(currentCanvas);
			if (saver.saveCircuitV3(fileName, canvases)) {
				loadedFileFormat = 3;
				saveFormatDecided = true;
				commandProcessor->MarkAsSaved();
			} else {
				wxMessageBox("Could not convert the file:\n\n" + saver.getLastError(),
					"Save Error", wxOK | wxICON_ERROR, this);
			}
		}
	}
}

void MainFrame::OnSave(wxCommandEvent& event) {
	if (openedFilename == "") OnSaveAs(event);
	else {
		int format = chooseSaveFormat();
		if (format == -1) return;  // user cancelled
		bool success = save((string)openedFilename, format);
		if (success) {
			commandProcessor->MarkAsSaved();
		} else if (lastSaveError.rfind("Warning:", 0) == 0) {
			// The file was written, but with a caveat (e.g. bus features can't be
			// represented in v1.x). Treat it as saved.
			wxMessageBox(lastSaveError, "Save Warning", wxOK | wxICON_WARNING, this);
			commandProcessor->MarkAsSaved();
		} else {
			wxString errorMsg = "Failed to save file:\n\n" + lastSaveError;
			wxMessageBox(errorMsg, "Save Error", wxOK | wxICON_ERROR, this);
		}
	}
}

// Ask which format to write an old-format file in. v3/new circuits save as v3
// with no prompt; v1/v2 files prompt so opening one never silently upgrades it.
int MainFrame::chooseSaveFormat() {
	if (loadedFileFormat != 1 && loadedFileFormat != 2) return 3;
	if (saveFormatDecided) return loadedFileFormat;  // already answered for this file

	wxString v = (loadedFileFormat == 1) ? "V1" : "V2";
	wxMessageDialog dialog(this,
		"This circuit was opened in an older file format (" + v + ").\n\n"
		"Convert it to V3, or keep " + v + "?\n\n"
		"V3 files cannot be opened by older versions of CedarLogic.",
		"Save Circuit", wxYES_NO | wxCANCEL | wxICON_QUESTION);
	dialog.SetYesNoCancelLabels("Convert to V3", "Keep " + v, "Cancel");

	int result = dialog.ShowModal();
	if (result == wxID_CANCEL) return -1;
	saveFormatDecided = true;  // don't ask again for this file
	if (result == wxID_YES) { loadedFileFormat = 3; return 3; }  // future saves stay v3
	return loadedFileFormat;  // keep the original format
}

void MainFrame::OnSaveAs(wxCommandEvent& WXUNUSED(event)) {
	handlingEvent = true;

	wxString caption = "Save circuit";
	wxString wildcard = "Circuit files (*.cdl)|*.cdl";
	wxString defaultFilename = "";
	wxFileDialog dialog(this, caption, wxEmptyString, defaultFilename, wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	dialog.SetDirectory(lastDirectory);
	if (dialog.ShowModal() == wxID_OK) {
		wxString path = dialog.GetPath();
		int format = chooseSaveFormat();
		if (format == -1) { handlingEvent = false; return; }  // user cancelled
		bool success = save((string)path, format);
		if (success || lastSaveError.rfind("Warning:", 0) == 0) {
			if (!success)
				wxMessageBox(lastSaveError, "Save Warning", wxOK | wxICON_WARNING, this);
			removeTempFile();
			openedFilename = path;
			this->SetTitle(VERSION_TITLE() + " - " + path );
			commandProcessor->MarkAsSaved();
		} else {
			wxString errorMsg = "Failed to save file:\n\n" + lastSaveError;
			wxMessageBox(errorMsg, "Save Error", wxOK | wxICON_ERROR, this);
		}
	}
	handlingEvent = false;
}

void MainFrame::OnOscope(wxCommandEvent& WXUNUSED(event)) {
	if (rightSplitter->IsSplit()) {
		rightSplitter->Unsplit(oscopePanel);
	} else {
		oscopePanel->Show();
		rightSplitter->SplitHorizontally(canvasBook, oscopePanel, -250);
	}
}

void MainFrame::OnViewGridline(wxCommandEvent& event) {
	appConfig().appSettings.gridlineVisible = event.IsChecked();
	if (currentCanvas != NULL) currentCanvas->Update();
}

void MainFrame::OnViewWireConn(wxCommandEvent& event) {
	appConfig().appSettings.wireConnVisible = event.IsChecked();
	if (currentCanvas != NULL) currentCanvas->Update();
}

void MainFrame::OnViewSkiaRenderer(wxCommandEvent& event) {
	appConfig().appSettings.useSkiaRenderer = event.IsChecked();
	// Repaint the visible canvas + minimap with the newly-chosen engine; other
	// pages and the oscope repaint on their next paint.
	if (currentCanvas != NULL) { currentCanvas->Refresh(); currentCanvas->Update(); }
	if (miniMap != NULL) miniMap->Refresh();
}

void MainFrame::OnViewRightClickRotate(wxCommandEvent& event) {
	appConfig().appSettings.rightClickRotate = event.IsChecked();
}

void MainFrame::OnPreferences(wxCommandEvent& event) {
	SettingsDialog dlg(this);
	if (dlg.ShowModal() == wxID_OK) {
		appConfig().appSettings.wireConnVisible = dlg.getWireConnVisible();
		appConfig().appSettings.wireConnRadius = (float)dlg.getWireConnRadius();
		appConfig().appSettings.gridlineVisible = dlg.getGridlineVisible();
		appConfig().appSettings.refreshRate = dlg.getRefreshRate();

		// Sync menu checkmarks
		GetMenuBar()->Check(View_Gridline, appConfig().appSettings.gridlineVisible);
		GetMenuBar()->Check(View_WireConn, appConfig().appSettings.wireConnVisible);
		GetMenuBar()->Check(View_RightClickRotate, appConfig().appSettings.rightClickRotate);

		if (currentCanvas != NULL) currentCanvas->Update();
	}
}

void MainFrame::OnTimer(wxTimerEvent& event) {
	ostringstream oss;
	if (!(currentCanvas->getCircuit()->getSimulate())) {
		return;
	}
	if (simBridge().appSystemTime.Time() < appConfig().appSettings.refreshRate) return;
	simBridge().appSystemTime.Pause();
	if (gCircuit->panic) return;
	// Do function of number of milliseconds that passed since last step
	gCircuit->lastTime = simBridge().appSystemTime.Time();
	gCircuit->lastTimeMod = appConfig().timeStepMod;
	gCircuit->lastNumSteps = simBridge().appSystemTime.Time() / appConfig().timeStepMod;
	gCircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_STEPSIM, new klsMessage::Message_STEPSIM(simBridge().appSystemTime.Time() / appConfig().timeStepMod)));
	currentCanvas->getCircuit()->setSimulate(false);
	simBridge().appSystemTime.Start(simBridge().appSystemTime.Time() % appConfig().timeStepMod);
}

void MainFrame::OnIdle(wxTimerEvent& event) {
	wxCriticalSectionLocker locker(simBridge().m_critsect);
	// Take the whole pending batch under the lock, then process it with the lock
	// released. The old TryLock+wxYield spin pumped the GUI event loop while
	// waiting on the logic thread, letting a menu action (undo/redo/paste)
	// re-enter mid-drain -- the crash class #32 worked around. Holding
	// mexMessages only for the O(1) swap removes the spin and the reentrancy.
	deque< klsMessage::Message > batch;
	{
		wxMutexLocker lock(simBridge().mexMessages);
		batch.swap(simBridge().dLOGICtoGUI);
	}
	while (!batch.empty()) {
		gCircuit->parseMessage(batch.front());
		batch.pop_front();
	}

	if (mainSizer == NULL) return;
	
	if ( doOpenFile ) {
		doOpenFile = false;
		load((string)openedFilename);
		this->SetTitle(VERSION_TITLE() + " - " + openedFilename );
	}
	
	if ( gCircuit->panic ) {
		gCircuit->panic = false;
		toolBar->ToggleTool( Tool_Pause, true );
#ifdef __WXOSX__
		NativeIcon_SetToolbarSFSymbol(toolBar, Tool_Pause, "play.fill", 18);
#else
		toolBar->SetToolNormalBitmap(Tool_Pause, playIcon);
#endif
		simTimer->Stop();
		simBridge().appSystemTime.Start(0);
		simBridge().appSystemTime.Pause();
		//Edit by Joshua Lansford 11/24/06
		//I have overloaded the meaning of panic
		//panic is now also used to pause the system.
		//thus we don't want to shout if we are just pausing
		//This edit was made so that the Z_80LogicGate
		//can 'step' through instructions.
		//see the location were pausing is set to true
		//for further explination in GUICircuit::parseMessage
		if( !gCircuit->pausing ){
			wxMessageBox("Overloading simulator: please increase time per step and then resume simulation.", "Error - overload", wxOK | wxICON_ERROR, NULL);
		}
		gCircuit->pausing = false;
	}

	if( sizeChanged ) {	
		sizeChanged = false;
		wxSizeEvent temp;
	}
}
void MainFrame::OnSize(wxSizeEvent& event) {
	if (currentCanvas != NULL) currentCanvas->Update();
	if (mainSizer != NULL) mainSizer->Layout();
}

void MainFrame::OnMaximize(wxMaximizeEvent& event) {
	// Setup the "Maximize Catch" flag:
	sizeChanged = true;
}

#ifdef __WXOSX__
void MainFrame::OnNotebookPage(wxBookCtrlEvent& event) {
#else
void MainFrame::OnNotebookPage(wxAuiNotebookEvent& event) {
#endif
	long canvasID = event.GetSelection();
	if (currentCanvas == NULL || canvases[canvasID] == currentCanvas) return;
	//**********************************
	//Edit by Joshua Lansford 4/9/07
	//This edit is to make the minimap
	//only be controled by the current
	//Canvase.
	//This will avoid the minimap
	//spazing out when the mainFrame is
	//resized
	currentCanvas->setMinimap( NULL );
	//End of Edit*********************
	currentCanvas = canvases[canvasID];
	gCircuit->setCurrentCanvas(currentCanvas);
	currentCanvas->setMinimap(miniMap);
	currentCanvas->SetFocus();
	currentCanvas->Update();
}

void MainFrame::OnUndo(wxCommandEvent& event) {
	// Quiesce the sim timers while the command re-runs its structural edits -- the
	// same guard the New/Open/Close handlers use. Undo/redo mutate the gate/wire
	// lists and fire core messages; with a running simulation the step timer is
	// concurrently syncing wire state and repainting (MT_DONESTEP), and the two
	// race and crash. Pausing the sim by hand avoids it, and so does this.
	handlingEvent = true;
	pauseTimers();
	// Switch to the page this command affects, so an undo on another tab is shown
	// where it happens instead of silently changing an off-screen page.
	klsCommand *cmd = (klsCommand *)commandProcessor->GetCurrentCommand();
	if (cmd != NULL) switchToCanvas(cmd->getCanvas());
	commandProcessor->Undo();
	// Tab commands can't be followed by pointer; they name a page to show after.
	if (cmd != NULL) { int p = cmd->pageToShow(true); if (p >= 0) showCanvasIndex(p); }
	resumeTimers(TIMER_POLL_MS);
	handlingEvent = false;
	currentCanvas->Update();
}

void MainFrame::OnRedo(wxCommandEvent& event) {
	handlingEvent = true;
	pauseTimers();
	// The redo target is the command just after the current position; switch to
	// its page before re-doing it (see OnUndo).
	wxList &cmds = commandProcessor->GetCommands();
	wxCommand *current = commandProcessor->GetCurrentCommand();
	klsCommand *cmd = NULL;
	if (current == NULL) {
		if (!cmds.IsEmpty()) cmd = (klsCommand *)cmds.GetFirst()->GetData();
	} else {
		wxList::compatibility_iterator node = cmds.Find(current);
		if (node && node->GetNext()) cmd = (klsCommand *)node->GetNext()->GetData();
	}
	if (cmd != NULL) switchToCanvas(cmd->getCanvas());
	commandProcessor->Redo();
	if (cmd != NULL) { int p = cmd->pageToShow(false); if (p >= 0) showCanvasIndex(p); }
	resumeTimers(TIMER_POLL_MS);
	handlingEvent = false;
	currentCanvas->Update();
}

void MainFrame::showCanvasIndex(int idx) {
	if (idx < 0 || idx >= (int)canvases.size()) return;
	GUICanvas *target = canvases[idx];
	// ChangeSelection switches without firing a page-changed event (which would
	// re-enter mid-undo); mirror the state OnNotebookPage would set, by hand.
	// currentCanvas may be a just-removed (hidden) page here, so guard it.
	canvasBook->ChangeSelection(idx);
	if (currentCanvas != NULL && currentCanvas != target) currentCanvas->setMinimap(NULL);
	currentCanvas = target;
	gCircuit->setCurrentCanvas(currentCanvas);
	currentCanvas->setMinimap(miniMap);
}

void MainFrame::switchToCanvas(GUICanvas *canvas) {
	if (canvas == NULL || canvas == currentCanvas) return;
	for (size_t i = 0; i < canvases.size(); i++) {
		if (canvases[i] == canvas) {
			// ChangeSelection switches the tab WITHOUT firing a page-changed
			// event -- SetSelection would, re-entering the GUI mid-undo. Mirror
			// the parts of OnNotebookPage we actually need, by hand.
			canvasBook->ChangeSelection(i);
			currentCanvas->setMinimap(NULL);
			currentCanvas = canvas;
			gCircuit->setCurrentCanvas(currentCanvas);
			currentCanvas->setMinimap(miniMap);
			break;
		}
	}
}

void MainFrame::OnCopy(wxCommandEvent& event) {
	currentCanvas->copyBlockToClipboard();
}

void MainFrame::OnPaste(wxCommandEvent& event) {
	currentCanvas->pasteBlockFromClipboard();
}

void MainFrame::OnExportBitmap(wxCommandEvent& event) {
	// Create unified export dialog with horizontal layout
	wxDialog exportDialog(this, wxID_ANY, "Export as Image", wxDefaultPosition, wxDefaultSize);
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	// Preview panel — sized dynamically on first render
	const int previewMaxW = 560, previewMaxH = 220;
	wxStaticBitmap* previewBitmap = new wxStaticBitmap(&exportDialog, wxID_ANY, wxNullBitmap,
		wxDefaultPosition, wxDefaultSize);
	mainSizer->Add(previewBitmap, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 15);

	// Grid option
	wxCheckBox* gridCheck = new wxCheckBox(&exportDialog, wxID_ANY, "Include grid lines");
	gridCheck->SetValue(false);
	mainSizer->Add(gridCheck, 0, wxLEFT | wxRIGHT, 15);

	// Horizontal sizer for output style and resolution side-by-side
	mainSizer->AddSpacer(10);
	wxBoxSizer* optionsSizer = new wxBoxSizer(wxHORIZONTAL);

	// Output style box with better spacing
	wxStaticBoxSizer* styleBox = new wxStaticBoxSizer(wxVERTICAL, &exportDialog, "Output style");
	wxRadioButton* colorRadio = new wxRadioButton(&exportDialog, wxID_ANY, "Color", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	wxRadioButton* bwRadio = new wxRadioButton(&exportDialog, wxID_ANY, "Black && White");
	colorRadio->SetValue(true);
	styleBox->Add(colorRadio, 0, wxALL, 5);
	styleBox->Add(bwRadio, 0, wxALL, 5);
	optionsSizer->Add(styleBox, 1, wxRIGHT | wxEXPAND, 10);

	// Resolution box with better spacing
	wxStaticBoxSizer* resBox = new wxStaticBoxSizer(wxVERTICAL, &exportDialog, "Resolution");
	wxString resolutions[] = {"Screen (2×)", "Print (4×)", "High Quality (6×)"};
	wxRadioButton* screen2x = new wxRadioButton(&exportDialog, wxID_ANY, "Screen (2×)", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	wxRadioButton* print4x = new wxRadioButton(&exportDialog, wxID_ANY, "Print (4×)");
	wxRadioButton* high6x = new wxRadioButton(&exportDialog, wxID_ANY, "High Quality (6×)");
	print4x->SetValue(true); // Default to Print
	resBox->Add(screen2x, 0, wxALL, 5);
	resBox->Add(print4x, 0, wxALL, 5);
	resBox->Add(high6x, 0, wxALL, 5);
	optionsSizer->Add(resBox, 1, wxLEFT | wxEXPAND, 10);

	mainSizer->Add(optionsSizer, 0, wxLEFT | wxRIGHT | wxEXPAND, 15);

	// Buttons with proper spacing and styling
	mainSizer->AddSpacer(25);
	wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);

	wxButton* saveBtn = new wxButton(&exportDialog, wxID_OK, "Export to File...");
	wxButton* copyBtn = new wxButton(&exportDialog, wxID_APPLY, "Copy to Clipboard");
	wxButton* cancelBtn = new wxButton(&exportDialog, wxID_CANCEL, "Cancel");

	copyBtn->Bind(wxEVT_BUTTON, [&exportDialog](wxCommandEvent&) {
		exportDialog.EndModal(wxID_APPLY);
	});

	saveBtn->SetDefault(); // Make it blue (default button)

	buttonSizer->Add(0, 0, 1); // Stretchable space
	buttonSizer->Add(saveBtn, 0, wxRIGHT, 10);
	buttonSizer->Add(copyBtn, 0, wxRIGHT, 10);
	buttonSizer->Add(cancelBtn, 0, 0, 0);
	buttonSizer->Add(0, 0, 1); // Stretchable space

	mainSizer->Add(buttonSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 20);

	exportDialog.SetSizer(mainSizer);

	// Preview update helper — renders at full canvas resolution and downscales for crisp preview
	double contentScaleFactor = exportDialog.GetContentScaleFactor();
	auto updatePreview = [&]() {
		bool showGrid = gridCheck->GetValue();
		bool noColor = bwRadio->GetValue();

		// Render at native canvas size (1:1 with what the user sees)
		wxBitmap bmp = getBitmap(showGrid, noColor, 1);
		wxImage img = bmp.ConvertToImage();

		// Compute logical thumbnail size preserving aspect ratio
		int srcW = img.GetWidth(), srcH = img.GetHeight();
		double scale = std::min((double)previewMaxW / srcW, (double)previewMaxH / srcH);
		if (scale > 1.0) scale = 1.0;
		int logicalW = std::max(1, (int)(srcW * scale));
		int logicalH = std::max(1, (int)(srcH * scale));

		// Scale to Retina physical pixels for sharp rendering
		int physW = (int)(logicalW * contentScaleFactor);
		int physH = (int)(logicalH * contentScaleFactor);
		img.Rescale(physW, physH, wxIMAGE_QUALITY_HIGH);

		// Create a Retina-aware bitmap at the logical size
		wxBitmap retinaThumb(img);
		retinaThumb.SetScaleFactor(contentScaleFactor);

		previewBitmap->SetBitmap(retinaThumb);
		previewBitmap->SetMinSize(wxSize(logicalW, logicalH));
		exportDialog.GetSizer()->Layout();
	};

	// Bind option changes to refresh preview
	auto onOptionChange = [&](wxCommandEvent&) { updatePreview(); };
	gridCheck->Bind(wxEVT_CHECKBOX, onOptionChange);
	colorRadio->Bind(wxEVT_RADIOBUTTON, onOptionChange);
	bwRadio->Bind(wxEVT_RADIOBUTTON, onOptionChange);

	// Generate initial preview and size dialog to fit
	updatePreview();
	exportDialog.Fit();
	exportDialog.Centre();

	int result = exportDialog.ShowModal();
	if (result == wxID_CANCEL) return;

	// Get user choices
	bool showGrid = gridCheck->GetValue();
	bool useNoColor = bwRadio->GetValue();
	int multiplier = screen2x->GetValue() ? 2 : (print4x->GetValue() ? 4 : 6);

	// Generate bitmap
	wxBitmap bitmap = getBitmap(showGrid, useNoColor, multiplier);

	// Handle action
	if (result == wxID_APPLY) {
		// Copy to clipboard
		if (wxTheClipboard->Open()) {
			wxTheClipboard->SetData(new wxBitmapDataObject(bitmap));
			wxTheClipboard->Flush();
			wxTheClipboard->Close();
		}
	} else if (result == wxID_OK) {
		// Save to file
		wxString caption = "Export Circuit";
		wxString wildcard = "SVG (*.svg)|*.svg|PNG (*.png)|*.png|Bitmap (*.bmp)|*.bmp";
		wxFileDialog saveDialog(this, caption, wxEmptyString, "", wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
		saveDialog.SetDirectory(lastDirectory);

		if (saveDialog.ShowModal() == wxID_OK) {
			wxString path = saveDialog.GetPath();
			wxString ext = path.SubString(path.find_last_of(".") + 1, path.length());

			if (ext == "svg") {
				bool success = false;
#ifdef WITH_SKIA
				// Vector SVG through the Scene seam (Skia) -- same renderer that
				// matches the GL golden, so the export is faithful. The page is
				// the canvas size x the chosen multiplier; SVG stays crisp anyway.
				wxSize sz = currentCanvas->GetClientSize();
				success = renderToSvgSkia(path, sz.GetWidth() * multiplier,
				                          sz.GetHeight() * multiplier,
				                          showGrid, useNoColor);
#else
				// Fallback: the legacy hand-rolled exporter.
				float scale = multiplier / 2.0f;
				success = SVGExporter::exportToSVG(currentCanvas, std::string(path.mb_str()),
				                                   showGrid, useNoColor, scale);
#endif
				if (!success) {
					wxMessageBox("Failed to export SVG file.", "Export Error", wxOK | wxICON_ERROR);
				}
			} else {
				// Export as bitmap (PNG default; BMP when explicitly chosen).
				wxBitmapType fileType = (ext == "bmp") ? wxBITMAP_TYPE_BMP
				                                       : wxBITMAP_TYPE_PNG;
				bitmap.SaveFile(path, fileType);
			}
		}
	}
}

void MainFrame::OnExportLegacy(wxCommandEvent& event) {
	handlingEvent = true;

	wxString caption = "Export v1.x Compatible Circuit";
	wxString wildcard = "Circuit files (*.cdl)|*.cdl";
	wxString defaultFilename = "";
	wxFileDialog dialog(this, caption, wxEmptyString, defaultFilename, wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	dialog.SetDirectory(lastDirectory);
	if (dialog.ShowModal() == wxID_OK) {
		wxString path = dialog.GetPath();

		// Pause system during save
		lock();
		gCircuit->setSimulate(false);

		// Save in legacy format
		CircuitParse cirp(currentCanvas);
		bool success = cirp.saveCircuitLegacy((string)path, canvases);

		// Resume system
		gCircuit->setSimulate(true);
		if (!(toolBar->GetToolState(Tool_Lock))) {
			unlock();
		}

		if (!success) {
			// Check the error message to distinguish between I/O error and bus features
			CircuitParse cirpCheck(currentCanvas);
			string errorMsg = cirp.getLastError();

			if (errorMsg.find("Warning:") == 0) {
				// This is a bus features warning, file was saved successfully
				wxMessageBox(errorMsg, "Export Warning", wxOK | wxICON_WARNING);
			} else {
				// This is an I/O error
				wxString fullMsg = "Failed to export file:\n\n" + errorMsg;
				wxMessageBox(fullMsg, "Export Error", wxOK | wxICON_ERROR);
			}
		}
	}
	handlingEvent = false;
}

// Export a copy in the v2 (pre-v3 XML) format without changing the open file.
void MainFrame::OnExportV2(wxCommandEvent& event) {
	handlingEvent = true;

	wxString wildcard = "Circuit files (*.cdl)|*.cdl";
	wxFileDialog dialog(this, "Export v2 (legacy XML) Circuit", wxEmptyString, "",
	                    wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	dialog.SetDirectory(lastDirectory);
	if (dialog.ShowModal() == wxID_OK) {
		wxString path = dialog.GetPath();

		lock();
		gCircuit->setSimulate(false);

		CircuitParse cirp(currentCanvas);
		bool success = cirp.saveCircuit((string)path, canvases);

		gCircuit->setSimulate(true);
		if (!(toolBar->GetToolState(Tool_Lock))) unlock();

		if (!success) {
			wxMessageBox("Failed to export file:\n\n" + cirp.getLastError(),
			             "Export Error", wxOK | wxICON_ERROR);
		}
	}
	handlingEvent = false;
}

void MainFrame::OnCopyToClipboard(wxCommandEvent& event) {
	// Redirect to unified export dialog
	OnExportBitmap(event);
}

wxBitmap MainFrame::getBitmap(bool withGrid, bool noColor, int multiplier) {
	bool gridlineVisible = appConfig().appSettings.gridlineVisible;
	appConfig().appSettings.gridlineVisible = withGrid;
	renderMode().doingBitmapExport = true;

	// render the image
	// When noColor is true, it renders gates/wires as black line drawings (perfect for printing)
	wxSize imageSize = currentCanvas->GetClientSize();
	wxImage circuitImage = currentCanvas->renderToImage(imageSize.GetWidth() * multiplier, imageSize.GetHeight() * multiplier, 32, noColor);
	wxBitmap circuitBitmap(circuitImage);

	// restore grid display setting
	appConfig().appSettings.gridlineVisible = gridlineVisible;
	renderMode().doingBitmapExport = false;

	return circuitBitmap;
}

void MainFrame::OnPause(wxCommandEvent& event) {
	PauseSim();
}

void MainFrame::OnStep(wxCommandEvent& event) {
	if (!(currentCanvas->getCircuit()->getSimulate())) {
		return;
	}
	gCircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_STEPSIM, new klsMessage::Message_STEPSIM(1)));
	currentCanvas->getCircuit()->setSimulate(false);
}

void MainFrame::OnLock(wxCommandEvent& event) {
	if (toolBar->GetToolState(Tool_Lock)) {
		lock();
#ifdef __WXOSX__
		NativeIcon_SetToolbarSFSymbol(toolBar, Tool_Lock, "lock.fill", 18);
#else
		toolBar->SetToolNormalBitmap(Tool_Lock, lockedIcon);
#endif
	} else {
		unlock();
#ifdef __WXOSX__
		NativeIcon_SetToolbarSFSymbol(toolBar, Tool_Lock, "lock.open.fill", 18);
#else
		toolBar->SetToolNormalBitmap(Tool_Lock, unlockedIcon);
#endif
	}
}

void MainFrame::OnZoomIn(wxCommandEvent& event) {
	//TODO: There is no way to check if currentCanvas is valid first!!!
	currentCanvas->zoomIn();
}

void MainFrame::OnZoomOut(wxCommandEvent& event) {
	//TODO: There is no way to check if currentCanvas is valid first!!!
	currentCanvas->zoomOut();
}

void MainFrame::OnHelpContents(wxCommandEvent& event) {
	wxGetApp().helpController->DisplayContents();
}

void MainFrame::OnTimeStepModSlider(wxScrollEvent& event) {
	// Update the value first, then rebuild the label from it -- otherwise the
	// readout lags one change behind the slider.
	appConfig().timeStepMod = timeStepModSlider->GetValue();
	wxString oss;
	oss << appConfig().timeStepMod << "ms";
	timeStepModVal->SetLabel(oss);
}


void MainFrame::saveSettings() {
	//Edit by Joshua Lansford 2/15/07
	//making the execution of cedarls indipendent of were
	//it was executed from.  However the settings.ini file still
	//needs to be relative.
	//adding substring on the end of the relative paths to knock
	//of the part I put on.
	int numCharAbsolute = appConfig().resourcesDir.length();
	wxConfigBase *conf = wxConfigBase::Get();
	auto settings = appConfig().appSettings;
	
	wxString str = settings.gateLibFile.substr(numCharAbsolute);
	conf->Write("GateLib", str);

	str = settings.helpFile.substr(numCharAbsolute);
	conf->Write("HelpFile", str);

	str = settings.textFontFile.substr(numCharAbsolute);
	conf->Write("TextFont", str);

	conf->Write("FrameWidth", GetSize().GetWidth());
	conf->Write("FrameHeight", GetSize().GetHeight());
	conf->Write("FrameLeft", GetPosition().x);
	conf->Write("FrameTop", GetPosition().y);
	conf->Write("TimeStep", appConfig().timeStepMod);
	conf->Write("RefreshRate", settings.refreshRate);
	conf->Write("LastDirectory", lastDirectory);
	conf->Write("WireConnRadius", settings.wireConnRadius);
	conf->Write("WireConnVisible", settings.wireConnVisible);
	conf->Write("GridlineVisible", settings.gridlineVisible);
	conf->Write("RightClickRotate", settings.rightClickRotate);
	conf->Write("UseSkiaRenderer", settings.useSkiaRenderer);
}

void MainFrame::ResumeExecution() {
	if (toolBar->GetToolState(Tool_Pause)) {
		toolBar->ToggleTool(Tool_Pause, false);
		PauseSim();
	}
	else {
		//do nothing
	}
}

void MainFrame::PauseSim() {
	if (toolBar->GetToolState(Tool_Pause)) {
		simTimer->Stop();
		simBridge().appSystemTime.Start(0);
		simBridge().appSystemTime.Pause();
#ifdef __WXOSX__
		NativeIcon_SetToolbarSFSymbol(toolBar, Tool_Pause, "play.fill", 18);
#else
		toolBar->SetToolNormalBitmap(Tool_Pause, playIcon);
#endif
	}
	else {
		simBridge().appSystemTime.Start(0);
		simTimer->Start(TIMER_POLL_MS);
#ifdef __WXOSX__
		NativeIcon_SetToolbarSFSymbol(toolBar, Tool_Pause, "pause.fill", 18);
#else
		toolBar->SetToolNormalBitmap(Tool_Pause, pauseIcon);
#endif
	}
}

//Julian: Added to simplify timer use.
void MainFrame::stopTimers() {
	simTimer->Stop();
	idleTimer->Stop();
}

void MainFrame::startTimers(int at) {
	if (!(toolBar->GetToolState(Tool_Pause)))
	{
		simTimer->Start(at);
	}
	idleTimer->Start(at);
}

void MainFrame::pauseTimers() {
	simBridge().appSystemTime.Pause();
	stopTimers();
}
void MainFrame::resumeTimers(int at) {
	if (!(toolBar->GetToolState(Tool_Pause)))
	{
		simBridge().appSystemTime.Start(0);
		simTimer->Start(at);
	}
	idleTimer->Start(at);
}

//Julian: All of the following functions were added to support autosave functionality.

void MainFrame::autosave() {
	// Attempt to autosave - if it fails, the user can still manually save
	save(CRASH_FILENAME);
}

bool MainFrame::save(string filename, int format) {
	//Pause system so that user can't modify during save
	lock();
	gCircuit->setSimulate(false);

	// Disabling timers from autosave thread caused an assertion fail.
	//pauseTimers();

	//Save file in the requested format (v3 by default).
	CircuitParse cirp(currentCanvas);
	bool success;
	if (format == 1) success = cirp.saveCircuitLegacy(filename, canvases);
	else if (format == 2) success = cirp.saveCircuit(filename, canvases);
	else success = cirp.saveCircuitV3(filename, canvases);

	// Store the error message for the caller
	if (!success) {
		lastSaveError = cirp.getLastError();
	}

	// Disabling timers from autosave thread caused an assertion fail.
	//Resume system
	//resumeTimers(20);

	gCircuit->setSimulate(true);
	if (!(toolBar->GetToolState(Tool_Lock))) {
		unlock();
	}

	return success;
}

bool MainFrame::fileIsDirty() {
	return commandProcessor->IsDirty();
}

void MainFrame::removeTempFile() {
	remove(CRASH_FILENAME.c_str());
}

bool MainFrame::isHandlingEvent() {
	return handlingEvent;
}

void MainFrame::lock() {
	for (unsigned int i = 0; i < canvases.size(); i++) {
		canvases[i]->lockCanvas();
	}
}

void MainFrame::unlock() {
	for (unsigned int i = 0; i < canvases.size(); i++) {
		canvases[i]->unlockCanvas();
	}
}

void MainFrame::load(string filename) {
	loadCircuitFile(filename);
}

bool MainFrame::renderToPng(const wxString &path) {
	if (currentCanvas == NULL) return false;
	// Go through the canonical export path. getBitmap brackets the render with
	// doingBitmapExport, which is load-bearing: the offscreen GL context is
	// unshared on Windows, so without it the connection-point display list is a
	// no-op and wires lose their dots. It also handles the grid-visibility state.
	wxBitmap bmp = getBitmap(appConfig().appSettings.gridlineVisible, false, 1);
	if (!bmp.IsOk()) return false;
	return bmp.ConvertToImage().SaveFile(path, wxBITMAP_TYPE_PNG);
}

bool MainFrame::renderToPngSkia(const wxString &path, int width, int height) {
#ifdef WITH_SKIA
	if (currentCanvas == NULL) return false;
	GUICanvas *canvas = currentCanvas;
	// Draw through the engine-neutral Scene into a Skia raster surface. The
	// callback keeps Skia headers out of this TU (see SkiaProbe.h).
	cl::render::RenderStyle style = cl::render::RenderStyle::screen();
	return cl::render::skiaRenderToPng(
		path.ToStdString().c_str(), width, height,
		[canvas, &style, width, height](cl::render::Scene &scene) {
			canvas->renderToScene(scene, style, width, height);
		});
#else
	(void)path; (void)width; (void)height;
	return false;
#endif
}

bool MainFrame::renderSingleGate(const std::string &gateName, const std::string &angle,
                                 const wxString &path, int width, int height, bool skia) {
	if (currentCanvas == NULL || gCircuit == NULL) return false;

	// Ensure the GL text font's glyph metrics are loaded before any gate is
	// created: createGate -> calcBBox -> guiText::getBoundingBox reads them, and
	// in this windowless one-shot the canvas may not have painted (which is what
	// normally loads the font) yet. Create() reads the metrics from the file
	// before touching GL, so this is safe without a current context; the texture
	// reloads at real render time.
	guiText::loadFont(appConfig().appSettings.textFontFile);

	// Build one gate of this type through the real creation path, so its shape,
	// hotspots and params are exactly what the app would produce. createGate
	// registers it in the circuit and stamps its id; we still add it to the
	// canvas list that the renderer iterates.
	guiGate *g = gCircuit->createGate(gateName, -1);
	if (g == NULL) return false;

	// Rotate before placing: insertGate -> setGLcoords -> updateBBoxes reads the
	// "angle" gparam to build the gate's model matrix.
	g->setGUIParam("angle", angle);
	currentCanvas->insertGate(g->getID(), g, 0.0f, 0.0f);
	currentCanvas->Update();

	if (skia) return renderToPngSkia(path, width, height);
	return renderToPng(path);
}

// Shared scene for the wire-router test hooks.
struct ProbeScene {
	guiWire *wire = nullptr;
	guiGate *A = nullptr;
	guiGate *B = nullptr;
	std::string outName, inName;
};

// Two gates a fixed distance apart, then a wire from A's first output to B's
// first input -- this drives guiWire::addConnection -> calcShape, so a dump
// captures exactly what the router produced. Deterministic: gate/hotspot choice
// and positions are fixed, so before/after dumps diff cleanly.
static ProbeScene buildProbeWire(GUICircuit *gCircuit, GUICanvas *canvas,
                                 const std::string &gateA, const std::string &gateB,
                                 const std::string &angleA, const std::string &angleB) {
	ProbeScene sc;
	guiText::loadFont(appConfig().appSettings.textFontFile);
	sc.A = gCircuit->createGate(gateA, -1);
	sc.B = gCircuit->createGate(gateB, -1);
	if (sc.A == NULL || sc.B == NULL) { sc.A = sc.B = nullptr; return sc; }
	sc.A->setGUIParam("angle", angleA);
	sc.B->setGUIParam("angle", angleB);
	canvas->insertGate(sc.A->getID(), sc.A, -8.0f, 0.0f);
	canvas->insertGate(sc.B->getID(), sc.B, 8.0f, 0.0f);
	canvas->Update();

	for (const auto &kv : sc.A->getHotspotList())
		if (!sc.A->isConnectionInput(kv.first)) { sc.outName = kv.first; break; }
	for (const auto &kv : sc.B->getHotspotList())
		if (sc.B->isConnectionInput(kv.first)) { sc.inName = kv.first; break; }
	if (sc.outName.empty() || sc.inName.empty()) return sc;

	std::vector<IDType> wireIds = { gCircuit->getNextAvailableWireID() };
	gCircuit->setWireConnection(wireIds, sc.A->getID(), sc.outName);
	sc.wire = gCircuit->setWireConnection(wireIds, sc.B->getID(), sc.inName);
	return sc;
}

// Dump a wire's segment map as deterministic text under `label`.
static void dumpSegMapTo(std::ofstream &f, guiWire *wire, const char *label) {
	f << "-- " << label << " --\n";
	std::map<long, wireSegment> sm = wire->getSegmentMap();
	for (const auto &kv : sm) {
		const wireSegment &s = kv.second;
		f << "seg " << s.id << (s.verticalSeg ? " V " : " H ")
		  << "(" << s.begin.x << "," << s.begin.y << ")-("
		  << s.end.x << "," << s.end.y << ") conn=[";
		for (const auto &c : s.connections) f << c.connection << ":" << c.gid << " ";
		f << "] xs={";
		for (const auto &ix : s.intersects) {
			f << ix.first << ":";
			for (long id : ix.second) f << id << ",";
			f << " ";
		}
		f << "}\n";
	}
}

bool MainFrame::dumpWireShape(const std::string &gateA, const std::string &gateB,
                              const std::string &angleA, const std::string &angleB,
                              const wxString &path) {
	if (currentCanvas == NULL || gCircuit == NULL) return false;
	ProbeScene sc = buildProbeWire(gCircuit, currentCanvas, gateA, gateB, angleA, angleB);
	if (sc.wire == NULL) return false;

	std::ofstream f(path.ToStdString().c_str());
	if (!f) return false;
	f << "wire " << gateA << "@" << angleA << "." << sc.outName
	  << " -> " << gateB << "@" << angleB << "." << sc.inName << "\n";

	dumpSegMapTo(f, sc.wire, "create"); // guiWire::calcShape output
	sc.B->setGLcoords(11.0f, 2.0f);     // move B -> guiGate::updateBBoxes notifies the
	currentCanvas->Update();            // wire, driving updateConnectionPos/updateSegDrag
	dumpSegMapTo(f, sc.wire, "after move B");
	return true;
}

bool MainFrame::dumpWireDrag(const std::string &gateA, const std::string &gateB,
                             const std::string &angleA, const std::string &angleB,
                             const wxString &path) {
	if (currentCanvas == NULL || gCircuit == NULL) return false;
	ProbeScene sc = buildProbeWire(gCircuit, currentCanvas, gateA, gateB, angleA, angleB);
	if (sc.wire == NULL) return false;

	std::ofstream f(path.ToStdString().c_str());
	if (!f) return false;
	f << "drag " << gateA << "@" << angleA << "." << sc.outName
	  << " -> " << gateB << "@" << angleB << "." << sc.inName << "\n";
	dumpSegMapTo(f, sc.wire, "create");

	// Pick the longest segment (begin <= end always, so no abs needed) and grab
	// its midpoint. A zero-size mouse box exactly on that segment selects it, the
	// same point-box the canvas passes to startSegDrag/updateSegDrag (snapMouse).
	std::map<long, wireSegment> sm = sc.wire->getSegmentMap();
	const wireSegment *pick = nullptr; float bestLen = -1.0f;
	for (const auto &kv : sm) {
		const wireSegment &s = kv.second;
		float len = (s.end.x - s.begin.x) + (s.end.y - s.begin.y);
		if (len > bestLen) { bestLen = len; pick = &kv.second; }
	}
	if (pick == nullptr) { f << "-- no segment to drag --\n"; return true; }
	GLPoint2f mid((pick->begin.x + pick->end.x) * 0.5f,
	              (pick->begin.y + pick->end.y) * 0.5f);
	bool vertical = pick->verticalSeg;

	klsCollisionObject mouse(COLL_MOUSEBOX);
	klsBBox startBox; startBox.addPoint(mid); mouse.setBBox(startBox);
	if (!sc.wire->startSegDrag(&mouse)) { f << "-- drag skipped (no segment under mouse) --\n"; return true; }

	// Drag perpendicular by a fixed grid delta (x for a vertical seg, y for a
	// horizontal one) so the reshape is real and reproducible.
	GLPoint2f target = vertical ? GLPoint2f(mid.x + 2.0f, mid.y)
	                            : GLPoint2f(mid.x, mid.y + 2.0f);
	klsBBox endBox; endBox.addPoint(target); mouse.setBBox(endBox);
	sc.wire->updateSegDrag(&mouse);
	sc.wire->endSegDrag();
	dumpSegMapTo(f, sc.wire, "after drag");
	return true;
}

#ifdef WITH_AVOID
bool MainFrame::dumpAvoidRoutes(const wxString &path) {
	if (currentCanvas == NULL || gCircuit == NULL) return false;

	cl::avoid::RoutingService svc;
	auto *gates = gCircuit->getGates();

	// Every gate is an obstacle. Inset the bbox a hair so a wire endpoint sitting
	// on a gate edge (hotspots live on the boundary) lands just outside its own
	// obstacle rather than inside it.
	const float inset = 0.05f;
	for (auto &gp : *gates) {
		guiGate *g = gp.second;
		if (g == NULL) continue;
		klsBBox b = g->getBBox();
		svc.addObstacle(gp.first, b.getLeft() + inset, b.getBottom() + inset,
		                b.getRight() - inset, b.getTop() - inset);
	}

	// Each 2-terminal wire becomes one connector between its endpoint hotspots.
	// Multi-terminal nets (shared trunks) wait for 3.2d's hyperedge support.
	auto *wires = gCircuit->getWires();
	std::vector<unsigned long> routed;
	for (auto &wp : *wires) {
		guiWire *w = wp.second;
		if (w == NULL) continue;
		std::vector<wireConnection> conns = w->getConnections();
		if (conns.size() != 2) continue;
		auto g0 = gates->find(conns[0].gid);
		auto g1 = gates->find(conns[1].gid);
		if (g0 == gates->end() || g1 == gates->end()) continue;
		GLPoint2f p0, p1;
		g0->second->getHotspotCoords(conns[0].connection, p0.x, p0.y);
		g1->second->getHotspotCoords(conns[1].connection, p1.x, p1.y);
		svc.addConnector(wp.first, p0.x, p0.y, p1.x, p1.y);
		routed.push_back(wp.first);
	}

	svc.run();

	std::sort(routed.begin(), routed.end()); // deterministic dump order
	std::ofstream f(path.ToStdString().c_str());
	if (!f) return false;
	f << "avoid-routes: " << routed.size() << " two-terminal wires, "
	  << gates->size() << " obstacles\n";

	// Feed every real route through the 3.2c translator and tally validity: a
	// non-orthogonal emitted segment would mean the translator mishandled some
	// libavoid output. This exercises the translator on real data alongside its
	// unit suite.
	long totalSegs = 0, totalJunctions = 0, nonOrthogonal = 0;
	for (unsigned long wid : routed) {
		std::vector<std::pair<float, float>> pts = svc.routeOf(wid);
		std::vector<cl::avoid::RoutePoint> rp;
		for (const auto &pt : pts) rp.push_back({pt.first, pt.second});
		cl::avoid::ShapeOut shape = cl::avoid::polylineToSegments(rp);
		for (const cl::avoid::SegmentOut &sg : shape.segments) {
			totalSegs++;
			totalJunctions += (long)sg.crossings.size();
			bool ortho = sg.vertical ? (sg.bx == sg.ex) : (sg.by == sg.ey);
			if (!ortho) nonOrthogonal++;
		}
		f << "wire " << wid << " : " << pts.size() << " pts -> "
		  << shape.segments.size() << " segs";
		for (const auto &pt : pts) f << " (" << pt.first << "," << pt.second << ")";
		f << "\n";
	}
	f << "translated: " << totalSegs << " segments, " << totalJunctions / 2
	  << " junctions, " << nonOrthogonal << " non-orthogonal\n";
	return true;
}
#else
bool MainFrame::dumpAvoidRoutes(const wxString &) { return false; }
#endif

// Build the export RenderStyle from the dialog's choices: black-on-white with
// no live signal colors when "Black & White" is picked (print intent), full
// color otherwise. Selection overlays never belong in an exported file.
#ifdef WITH_SKIA
static cl::render::RenderStyle exportStyle(bool showGrid, bool noColor) {
	cl::render::RenderStyle style;
	style.colorOutput = !noColor;
	style.showLiveState = !noColor;
	style.showGrid = showGrid;
	style.showSelection = false;
	return style;
}
#endif

bool MainFrame::renderToSvgSkia(const wxString &path, int width, int height,
                                bool showGrid, bool noColor) {
#ifdef WITH_SKIA
	if (currentCanvas == NULL) return false;
	GUICanvas *canvas = currentCanvas;
	cl::render::RenderStyle style = exportStyle(showGrid, noColor);
	return cl::render::skiaRenderToSvg(
		path.ToStdString().c_str(), width, height,
		[canvas, &style, width, height](cl::render::Scene &scene) {
			canvas->renderToScene(scene, style, width, height);
		});
#else
	(void)path; (void)width; (void)height; (void)showGrid; (void)noColor;
	return false;
#endif
}

bool MainFrame::renderToPdfSkia(const wxString &path, int width, int height,
                                bool showGrid, bool noColor) {
#ifdef WITH_SKIA
	if (currentCanvas == NULL) return false;
	GUICanvas *canvas = currentCanvas;
	cl::render::RenderStyle style = exportStyle(showGrid, noColor);
	return cl::render::skiaRenderToPdf(
		path.ToStdString().c_str(), width, height,
		[canvas, &style, width, height](cl::render::Scene &scene) {
			canvas->renderToScene(scene, style, width, height);
		});
#else
	(void)path; (void)width; (void)height; (void)showGrid; (void)noColor;
	return false;
#endif
}

void MainFrame::openFileFromFinder(const wxString& fileName) {
	doOpenFile = true;
	openedFilename = fileName;
}

void MainFrame::PreGateDrag() {
	currentCanvas->CaptureMouse();
}

//JV - Make new canvas and add it to canvases and canvasBook
//TODO - Find a way to put a tab button in correct place
void MainFrame::OnNewTab(wxCommandEvent& event) {
	int canSize = canvases.size();

	if (canSize < 42) {
		gCircuit->GetCommandProcessor()->Submit((wxCommand*)new cmdAddTab(gCircuit, canvasBook, &canvases));
	}
	else {
		wxMessageBox("You have reached the maximum number of tabs.", "Close", wxOK);
	}
	 
/*	canvases.push_back(new GUICanvas(canvasBook, gCircuit, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS));
	ostringstream oss;
	oss << "Page " << canvases.size();
	canvasBook->AddPage(canvases[canvases.size()-1], oss.str(), false);*/

}

#ifdef __WXOSX__
//macOS: Close current tab via Edit > Close Tab (Cmd+W)
void MainFrame::OnCloseTab(wxCommandEvent& event) {
	int canvasID = canvasBook->GetSelection();
	int canSize = canvases.size();

	if (canSize > 1) {
		if (!canvases[canvasID]->getGateList()->empty()) {
			wxMessageDialog dialog(this, "All work on this tab will be lost. Would you like to close it?", "Close Tab", wxYES_DEFAULT | wxYES_NO | wxICON_QUESTION);
			switch (dialog.ShowModal()) {
				case wxID_YES:
					break;
				case wxID_NO:
					return;
			}
		}
		gCircuit->GetCommandProcessor()->Submit((wxCommand*)(new cmdDeleteTab(gCircuit, canvases[canvasID], canvasBook, &canvases, canvasID)));
	}
	else {
		wxBell();
	}
}
#else
//JV - Handle deletetab event. Remove tab and decrement all following tabs numbers
void MainFrame::OnDeleteTab(wxAuiNotebookEvent& event) {
	int canvasID = event.GetSelection();
	int canSize = canvases.size();


	if (canSize > 1) {
		if (!canvases[canvasID]->getGateList()->empty()) {
			wxMessageDialog dialog(this, "All work on this tab will be lost. Would you like to close it?", "Close Tab", wxYES_DEFAULT | wxYES_NO | wxICON_QUESTION);
			switch (dialog.ShowModal()) {
				case wxID_YES:
					break;
				case wxID_NO:
					event.Veto();
					return;
			}
		}
		gCircuit->GetCommandProcessor()->Submit((wxCommand*)(new cmdDeleteTab(gCircuit, currentCanvas, canvasBook, &canvases, canvasID)));
		event.Veto();
	}
	else {
		wxMessageBox("Tab cannot be closed", "Close", wxOK);
		event.Veto();
	}
}
#endif

void MainFrame::OnReportABug(wxCommandEvent& event) {
	// Tyler Drake can remap the url using cedar.to/create
	// Don't change the url here!
	//wxLaunchDefaultBrowser("https://cedar.to/XoQJpX", 0);
	wxMessageBox("Feature temporarily unavailable!");
}

void MainFrame::OnRequestAFeature(wxCommandEvent& event) {
	// Tyler Drake can remap the url using cedar.to/create
	// Don't change the url here!
	//wxLaunchDefaultBrowser("https://cedar.to/6IlP8c", 0);
	wxMessageBox("Feature temporarily unavailable!");
}

void MainFrame::OnDownloadLatestVersion(wxCommandEvent& event) {
#ifdef __APPLE__
	SparkleUpdater_CheckForUpdates();
#elif defined(_WIN32)
	WinSparkleUpdater_CheckForUpdates();
#else
	// Tyler Drake can remap the url using cedar.to/create
	// Don't change the url here!
	wxLaunchDefaultBrowser("https://cedar.to/vjyQw7", 0);
#endif
}

void MainFrame::OnKeyboardShortcuts(wxCommandEvent& event) {
#ifdef __WXOSX__
	wxString mod = "Cmd";
#else
	wxString mod = "Ctrl";
#endif

	wxDialog dlg(this, wxID_ANY, "Keyboard Shortcuts", wxDefaultPosition, wxDefaultSize,
				 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

	wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

	// Helper to add a section header
	auto addHeader = [&](wxFlexGridSizer* grid, const wxString& title) {
		wxStaticText* header = new wxStaticText(&dlg, wxID_ANY, title);
		wxFont headerFont = header->GetFont();
		headerFont.SetWeight(wxFONTWEIGHT_BOLD);
		header->SetFont(headerFont);
		grid->Add(header, 0, wxTOP | wxBOTTOM, 4);
		grid->Add(new wxStaticText(&dlg, wxID_ANY, ""), 0); // empty cell
	};

	// Helper to add a shortcut row
	auto addRow = [&](wxFlexGridSizer* grid, const wxString& key, const wxString& desc) {
		wxStaticText* keyText = new wxStaticText(&dlg, wxID_ANY, key);
		wxFont keyFont = keyText->GetFont();
		keyFont.SetFamily(wxFONTFAMILY_TELETYPE);
		keyText->SetFont(keyFont);
		grid->Add(keyText, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
		grid->Add(new wxStaticText(&dlg, wxID_ANY, desc), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
	};

	wxFlexGridSizer* grid = new wxFlexGridSizer(2, 6, 4);
	grid->AddGrowableCol(1, 1);

	addHeader(grid, "File Operations");
	addRow(grid, mod + "+N", "New Circuit");
	addRow(grid, mod + "+O", "Open Circuit");
	addRow(grid, mod + "+S", "Save Circuit");
	addRow(grid, mod + "+Shift+S", "Save As");
	addRow(grid, mod + "+E", "Export as Image");

	addHeader(grid, "Edit");
	addRow(grid, mod + "+Z", "Undo");
	addRow(grid, mod + "+Shift+Z", "Redo");
	addRow(grid, mod + "+C", "Copy");
	addRow(grid, mod + "+V", "Paste");
	addRow(grid, "Delete", "Delete Selection");
	addRow(grid, "Escape", "Clear Selection");

	addHeader(grid, "View");
	addRow(grid, "+/=", "Zoom In");
	addRow(grid, "-", "Zoom Out");
	addRow(grid, "Scroll", "Zoom In/Out");
	addRow(grid, "Space", "Zoom to Fit");
	addRow(grid, "Arrow Keys", "Pan View");
#ifdef __WXOSX__
	addRow(grid, "Cmd+Scroll/Swipe", "Pan View");
#else
	addRow(grid, "Trackpad Swipe", "Pan Horizontally (natural)");
#endif
	addRow(grid, "Shift+Scroll", "Pan Horizontally");
	addRow(grid, "Ctrl+Scroll", "Pan Vertically");
	addRow(grid, mod + "+G", "Show Oscilloscope");

	addHeader(grid, "Gates");
	addRow(grid, "A", "Quick Add Gate");
	addRow(grid, "R", "Rotate Selection");

	addHeader(grid, "Tabs");
	addRow(grid, mod + "+T", "New Tab");
#ifdef __WXOSX__
	addRow(grid, mod + "+W", "Close Tab");
#endif

	topSizer->Add(grid, 1, wxALL | wxEXPAND, 16);
	topSizer->Add(dlg.CreateButtonSizer(wxOK), 0, wxALIGN_CENTER | wxBOTTOM, 12);

	dlg.SetSizerAndFit(topSizer);
	dlg.CentreOnParent();
	dlg.ShowModal();
}
