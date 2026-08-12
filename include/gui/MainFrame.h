/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   MainFrame: Main frame object
*****************************************************************************/

#ifndef MAINFRAME_H_
#define MAINFRAME_H_

#include "MainApp.h"
#include "PaletteFrame.h"
#include "wx/wxprec.h"
#include "wx/thread.h"
#include "wx/toolbar.h"
#include "wx/gbsizer.h"
#ifdef __WXOSX__
#include "wx/notebook.h"
#else
#include "wx/aui/auibook.h"
#endif
#include "wx/slider.h"
#include "wx/splitter.h"
#include "threadLogic.h"
#include "GUICanvas.h"
#include "GUICircuit.h"
//#include "OscopeFrame.h"
class OscopeFrame;
#include "klsMiniMap.h"
#include "autoSaveThread.h"

enum
{
	File_Export = 5901, // out of range of wxWidgets constants
	File_ClipCopy,
	File_ExportLegacy,
	File_ExportV2,
	
	View_Oscope,
	View_Gridline,
	View_WireConn,
	View_SkiaRenderer,
	View_RightClickRotate,
	View_Preferences,
	
    TIMER_ID,
    IDLETIMER_ID,
    TOOLBAR_ID,
    NOTEBOOK_ID,
    
    Tool_Pause,
    Tool_Step,
    Tool_ZoomIn,
    Tool_ZoomOut,
    Tool_Lock,
	Tool_NewTab,
	Tool_DeleteTab,
	Tool_CloseTab,

	Help_ReportABug,
	Help_RequestAFeature,
	Help_DownloadLatestVersion,
	Help_KeyboardShortcuts
};

class MainFrame : public wxFrame {
public:
    // ctor(s)
    MainFrame(const wxString& title, string cmdFilename = "");
	virtual ~MainFrame();
	
    // event handlers (these functions should _not_ be virtual)
    void OnClose(wxCloseEvent& event);
    void OnQuit(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void OnHelpContents(wxCommandEvent& event);
    void OnNew(wxCommandEvent& event);
    void OnOpen(wxCommandEvent& event);
    void OnSave(wxCommandEvent& event);
    void OnSaveAs(wxCommandEvent& event);
	void OnExportBitmap(wxCommandEvent& event);
	void OnExportLegacy(wxCommandEvent& event);
	void OnExportV2(wxCommandEvent& event);
	void OnCopyToClipboard(wxCommandEvent& event);
	void OnTimer(wxTimerEvent& event);
	void OnIdle(wxTimerEvent& event);
	void OnSize(wxSizeEvent& event);
#ifdef __WXOSX__
	void OnNotebookPage(wxBookCtrlEvent& event);
#else
	void OnNotebookPage(wxAuiNotebookEvent& event);
#endif
	void OnMaximize(wxMaximizeEvent& event);
	void OnUndo(wxCommandEvent& event);
	void OnRedo(wxCommandEvent& event);
	void OnCut(wxCommandEvent& event);
	void OnCopy(wxCommandEvent& event);
	void OnPaste(wxCommandEvent& event);
	void OnOscope(wxCommandEvent& event);
	void OnViewGridline(wxCommandEvent& event);
	void OnViewWireConn(wxCommandEvent& event);
	void OnViewSkiaRenderer(wxCommandEvent& event);
	void OnViewRightClickRotate(wxCommandEvent& event);
	void OnPreferences(wxCommandEvent& event);
	void OnPause(wxCommandEvent& event);
	void OnStep(wxCommandEvent& event);
	void OnZoomIn(wxCommandEvent& event);
	void OnZoomOut(wxCommandEvent& event);
	void OnTimeStepModSlider(wxScrollEvent& event);
	void OnLock(wxCommandEvent& event);
	void OnNewTab(wxCommandEvent& event);
#ifdef __WXOSX__
	void OnCloseTab(wxCommandEvent& event);
#else
	void OnDeleteTab(wxAuiNotebookEvent& event);
#endif
	void OnReportABug(wxCommandEvent& event);
	void OnRequestAFeature(wxCommandEvent& event);
	void OnDownloadLatestVersion(wxCommandEvent& event);
	void OnKeyboardShortcuts(wxCommandEvent& event);
	
	void saveSettings( void );
	
	void ResumeExecution ( void );
	
	void PauseSim( void );
	
	void loadCircuitFile( string fileName );
	void openFileFromFinder( const wxString& fileName );

	//Julian: Added to simplify timer use
	void stopTimers();
	void startTimers(int at);
	void pauseTimers();
	void resumeTimers(int at);

	//Julian: Added functions to help with auto save functionality
	void autosave();
	bool fileIsDirty();
	void removeTempFile();
	bool isHandlingEvent();
	void lock();
	void unlock();
	// format: 1 = v1 XML, 2 = v2 XML, 3 = v3 S-expression (the default).
	bool save(string filename, int format = 3);
	void load(string filename);
	// Make `canvas` the active page (selecting its tab) so an undo/redo that
	// affects another page is shown where it happens. No-op if null or current.
	void switchToCanvas(GUICanvas *canvas);
	// Select a tab by index (safe, event-free), for undo/redo of tab commands
	// whose canvas pointer can't be followed. Updates currentCanvas + minimap.
	void showCanvasIndex(int idx);
	// Render the current canvas to a PNG (headless --render mode). Output size
	// follows the canvas client area, which the caller sizes before calling.
	bool renderToPng(const wxString &path);

	// Workstream G: render the current canvas to a PNG through Skia (engine-
	// neutral Scene -> raster surface), instead of the OpenGL path above.
	// Returns false if built without WITH_SKIA.
	bool renderToPngSkia(const wxString &path, int width, int height);
	bool renderToSvgSkia(const wxString &path, int width, int height,
	                     bool showGrid, bool noColor);
	bool renderToPdfSkia(const wxString &path, int width, int height,
	                     bool showGrid, bool noColor);

	// Golden/permutation test hook (headless --render-gate[-skia]): create one
	// gate of the named type through the real GateLibrary path, rotate it by
	// `angle` (a gparam string, e.g. "0"/"90"/"180"/"270"), and render just that
	// gate to a PNG. `skia` picks the Skia path over OpenGL. Renders whatever the
	// app would create, so it exercises the true shape/pins/params.
	bool renderSingleGate(const std::string &gateName, const std::string &angle,
	                      const wxString &path, int width, int height, bool skia);

	// Wire-router test hook (headless --wire-shape): place two gates a fixed
	// distance apart, connect A's first output to B's first input (which runs
	// calcShape), and write the resulting segment map to `path` as text -- a
	// deterministic before/after net for router changes, since loaded circuits
	// reuse their saved segMap and never route.
	bool dumpWireShape(const std::string &gateA, const std::string &gateB,
	                   const std::string &angleA, const std::string &angleB,
	                   const wxString &path);
	// Wire-router test hook (headless --wire-drag): same two-gate wire as
	// dumpWireShape, then programmatically drag the longest segment perpendicular
	// by a fixed grid delta (startSegDrag -> updateSegDrag -> endSegDrag) and dump
	// the segment map before/after. Gives the interactive drag path -- the code a
	// segment-graph / collision-proxy refactor touches most -- a golden net.
	bool dumpWireDrag(const std::string &gateA, const std::string &gateB,
	                  const std::string &angleA, const std::string &angleB,
	                  const wxString &path);
	// Ask which format to save an old-format file in. Returns 1/2/3, or -1 to
	// cancel. New/v3 circuits return 3 without prompting.
	int chooseSaveFormat();

	void PreGateDrag();

	//Julian: Added to simplify exporting and copying to clipboard
	wxBitmap getBitmap(bool withGrid, bool noColor = false, int multiplier = 2);
	
private:
    // helper function - creates a new thread (but doesn't run it)
	threadLogic *CreateThread();
	autoSaveThread *CreateSaveThread(); //Julian
	

	vector< GUICanvas* > canvases;
	GUICircuit* gCircuit;
	GUICanvas* currentCanvas;
	klsMiniMap* miniMap;
	
	wxCommandProcessor* commandProcessor;

	wxPanel* mainPanel;
	wxToolBar* toolBar;
	wxBitmap pauseIcon;
	wxBitmap playIcon;
	wxBitmap lockedIcon;
	wxBitmap unlockedIcon;

	//Julian: Re-added timers to fix refresh error
	wxTimer* simTimer;
	wxTimer* idleTimer;

#ifdef __WXOSX__
	wxNotebook* canvasBook;
#else
	//JV - Changed to AuiNoteBook to allow for close tab button
	wxAuiNotebook* canvasBook;
#endif
	
	// Instance variables
	bool sizeChanged;
	bool doOpenFile;
	wxString lastDirectory;
	wxString openedFilename;
	int loadedFileFormat = 3;  // format the current circuit was opened from (1/2/3); 3 for new
	bool saveFormatDecided = false;  // user has answered the keep-or-migrate prompt for this file
	unsigned int currentTempNum;

	bool handlingEvent; //Julian: Prevents autosaving from occuring during an open/new/saveas/etc...
	const string CRASH_FILENAME = "crashfile.temp"; //Julian: Filename to check.
	string lastSaveError; // Detailed error message from last save attempt
	
	wxSlider* timeStepModSlider;
	wxStaticText* timeStepModVal;
	PaletteFrame* gatePalette;
	
	wxSplitterWindow* rightSplitter;
	OscopeFrame* oscopePanel;
	wxBoxSizer* mainSizer;
	
    // any class wishing to process wxWidgets events must use this macro
    DECLARE_EVENT_TABLE()
};

#endif /*MAINFRAME_H_*/
