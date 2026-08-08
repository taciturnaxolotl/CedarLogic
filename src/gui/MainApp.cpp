/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   MainApp: Main application object
*****************************************************************************/

#include "MainApp.h"
#include "MainFrame.h"
#include "wx/cmdline.h"
#include "../version.h"
#include "wx/stdpaths.h"
#include "wx/fileconf.h"
#ifdef __APPLE__
#include "SparkleUpdater.h"
#endif
#ifdef _WIN32
#include "WinSparkleUpdater.h"
#include <windows.h>
#include <mmsystem.h>
#include <dbghelp.h>
#include <cstdio>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dbghelp.lib")
#endif

IMPLEMENT_APP(MainApp)

#ifdef _WIN32
// On an otherwise-unhandled crash, walk the faulting thread's stack, symbolize
// it against the .pdb shipped next to the exe, and write a readable trace to
// %TEMP%\CedarLogic_crashtrace.log. Turns "it just crashed" into an actual
// function + file:line, which matters for a GUI app where the nastiest bugs
// only surface through live interaction and can't be caught under a debugger.
static LONG WINAPI writeCrashTrace(EXCEPTION_POINTERS *ep) {
    char path[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, path);
    if (n == 0 || n > MAX_PATH) path[0] = '\0';
    strncat(path, "CedarLogic_crashtrace.log", MAX_PATH - strlen(path) - 1);

    FILE *f = fopen(path, "w");
    if (f == NULL) return EXCEPTION_CONTINUE_SEARCH;

    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(proc, NULL, TRUE);

    fprintf(f, "Unhandled exception 0x%08lX at %p\n\n",
            ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);

    CONTEXT *ctx = ep->ContextRecord;
    STACKFRAME64 frame = {};
    DWORD machine = IMAGE_FILE_MACHINE_AMD64;
#if defined(_M_X64)
    frame.AddrPC.Offset = ctx->Rip;    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx->Rbp; frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx->Rsp; frame.AddrStack.Mode = AddrModeFlat;
#endif
    for (int i = 0; i < 96; i++) {
        if (!StackWalk64(machine, proc, GetCurrentThread(), &frame, ctx, NULL,
                         SymFunctionTableAccess64, SymGetModuleBase64, NULL))
            break;
        if (frame.AddrPC.Offset == 0) break;
        DWORD64 addr = frame.AddrPC.Offset;

        char symBuf[sizeof(SYMBOL_INFO) + 512] = {};
        SYMBOL_INFO *sym = (SYMBOL_INFO *)symBuf;
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 511;
        DWORD64 disp = 0;
        if (SymFromAddr(proc, addr, &disp, sym)) {
            IMAGEHLP_LINE64 line = {};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisp = 0;
            if (SymGetLineFromAddr64(proc, addr, &lineDisp, &line))
                fprintf(f, "  %-40s %s:%lu\n", sym->Name, line.FileName, line.LineNumber);
            else
                fprintf(f, "  %s +0x%llx\n", sym->Name, (unsigned long long)disp);
        } else {
            fprintf(f, "  0x%llx\n", (unsigned long long)addr);
        }
    }
    fflush(f);
    fclose(f);
    return EXCEPTION_CONTINUE_SEARCH; // let the OS still report/terminate as usual
}
#endif

static const wxCmdLineEntryDesc g_cmdLineDesc[] =
{
	{ wxCMD_LINE_PARAM, NULL, NULL, "input file", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL },
	{ wxCMD_LINE_NONE }
};

MainApp::MainApp()
     : m_semAllDone(), simulate(), readyToSend()
{
    m_waitingUntilAllDone = false;
    showDragImage = false;
    mainframe = NULL;
    doingBitmapExport = false;
	glContext = NULL;
#ifdef __WXGTK__
	// On Linux with wayland, wxGTK doesn't position glCanvas frames correctly.
	// This env var has to be set explicitly to instruct gtk to only use X11.
	::setenv("GDK_BACKEND", "x11", /* replace */ true);
#endif
}

bool MainApp::OnInit()
{
#ifdef _WIN32
    SetUnhandledExceptionFilter(writeCrashTrace);
    // Windows' default timer resolution (~15.6 ms) rounds wxTimer waits up to
    // the next system tick, so the 20 ms render/sim timers actually fire at
    // ~31 ms -- capping the render loop near 32 fps with visible jitter (and
    // making animation feel choppy compared to macOS, whose timers are ~1 ms).
    // Request 1 ms timer resolution for smooth, accurate timers. Released in
    // OnExit via timeEndPeriod(1).
    timeBeginPeriod(1);
#endif
#ifndef _PRODUCTION_
    logfile.open( "guilog.log" );
#endif
	loadSettings();
	
    wxFileSystem::AddHandler( new wxZipFSHandler );
#ifdef __APPLE__
	helpController = new wxHtmlHelpController(wxHF_DEFAULT_STYLE | wxHF_OPEN_FILES);
	// Only load help if the file exists to avoid blocking
	if (wxFileExists(appSettings.helpFile)) {
		if (!helpController->AddBook(appSettings.helpFile)) {
			wxLogWarning("Failed to load help file: %s", appSettings.helpFile);
		}
	}
#else
	helpController = new wxHelpController;
	helpController->Initialize(appSettings.helpFile);
#endif


	//*****************************************
	//Edit by Joshua Lansford 2/15/07
	//wxCmdLineParser is all fine and great,
	//but it is over kill.  Besides if you say
	//to windows to open a cdl file with cedarls,
	//it isn't going to prefix the file with anything
	//unless you do some special options which are
	//not necisary.  Therefore the argv can be used
	//directly without passing it into a cmdLineParser
	//
	//wxString cmdFilename;
	//wxCmdLineParser cmdParser(g_cmdLineDesc, argc, argv);
	//if (cmdParser.GetParamCount() > 0) {
	//	cmdFilename = cmdParser.GetParam(0);
	//	wxFileName fName(cmdFilename);
	//	fName.Normalize(wxPATH_NORM_LONG|wxPATH_NORM_DOTS|wxPATH_NORM_TILDE|wxPATH_NORM_ABSOLUTE);
	//	cmdFilename = fName.GetFullPath();
    //}	
    string cmdFilename;
	if( argc >= 2 ){
		cmdFilename = argv[1].ToStdString();
//		logfile << "cmdFilename = " << cmdFilename << endl;
	}
#ifdef __WXOSX__
	// On macOS, MacOpenFile may have been called before OnInit
	if (cmdFilename.empty() && !pendingOpenFile.empty()) {
		cmdFilename = pendingOpenFile;
		pendingOpenFile.clear();
	}
#endif
	//End of edit
	//**********************************


    // create the main application window
    MainFrame *frame = new MainFrame(VERSION_TITLE(), cmdFilename);

    //**********************************************************
    //Edit by Joshua Lansford 12/31/06
    //Acording to 
    //http://www.wxwidgets.org/manuals/2.6.3/wx_wxappoverview.html#wxappoverview
    //the following function should be called at this time
    SetTopWindow(frame);
    
    mainframe = frame;
    //End of edit***********************************************

#ifdef __APPLE__
    // Initialize Sparkle auto-updater
    SparkleUpdater_Initialize();
#endif
#ifdef _WIN32
    // Initialize WinSparkle auto-updater
    WinSparkleUpdater_Initialize();
#endif

    // success: wxApp::OnRun() will be called which will enter the main message
    // loop and the application will run. If we returned false here, the
    // application would exit immediately

    return true;
}

void MainApp::loadSettings() {
	wxStandardPathsBase& stdp = wxStandardPaths::Get();
	stdp.SetFileLayout(wxStandardPaths::FileLayout_XDG);

	if (const char* r_dir = getenv("CEDARLOGIC_RESOURCES_DIR")) {
		resourcesDir = r_dir;
		if (!resourcesDir.empty()) {
			resourcesDir += "/";
		}
	} else {
		resourcesDir = stdp.GetResourcesDir() + "/";
	}

	wxFileConfig *conf = new wxFileConfig("CedarLogic");
	wxConfigBase::Set(conf);
	wxConfigBase::DontCreateOnDemand();

	wxString str;
	conf->Read("GateLib", &str, "res/cl_gatedefs.xml");
	appSettings.gateLibFile = resourcesDir + str;

	#ifdef __APPLE__
	conf->Read("HelpFile", &str, "res/help/KLS_Logic.hhp");
#else
	conf->Read("HelpFile", &str, "res/KLS_Logic.chm");
#endif
	appSettings.helpFile = resourcesDir + str;

	conf->Read("TextFont", &str, "res/arial.glf");
	appSettings.textFontFile = resourcesDir + str;

	conf->Read("LastDirectory", &str, "");
	appSettings.lastDir = str;

	conf->Read("FrameWidth", &appSettings.mainFrameWidth, 600);
	conf->Read("FrameHeight", &appSettings.mainFrameHeight, 600);
	conf->Read("FrameLeft", &appSettings.mainFrameLeft, 20);
	conf->Read("FrameTop", &appSettings.mainFrameTop, 20);
	conf->Read("RefreshRate", &appSettings.refreshRate, 16); // ms (~60 FPS)
	conf->Read("TimeStep", &appSettings.timePerStep, 25); // ms
	timeStepMod = appSettings.timePerStep;
	conf->Read("WireConnRadius", &appSettings.wireConnRadius, 0.18f);
	conf->Read("WireConnVisible", &appSettings.wireConnVisible, true);
	conf->Read("GridlineVisible", &appSettings.gridlineVisible, true);
	conf->Read("RightClickRotate", &appSettings.rightClickRotate, true);

	// check screen coords
	wxScreenDC sdc;
	if ( appSettings.mainFrameLeft + appSettings.mainFrameWidth > sdc.GetSize().GetWidth() ||
		appSettings.mainFrameTop + appSettings.mainFrameHeight > sdc.GetSize().GetHeight() ) {

		appSettings.mainFrameWidth = appSettings.mainFrameHeight = 600;
		appSettings.mainFrameLeft = appSettings.mainFrameTop = 20;
	}
}

int MainApp::OnExit() {
	delete glContext;
	glContext = NULL;
#ifdef _WIN32
	timeEndPeriod(1);
#endif
	return wxApp::OnExit();
}

void MainApp::SetCurrentCanvas(wxGLCanvas *canvas)
{
	if (!glContext)
		glContext = new wxGLContext(canvas);
	glContext->SetCurrent(*canvas);
}

#ifdef __WXOSX__
void MainApp::MacOpenFile(const wxString& fileName)
{
	if (mainframe) {
		// Use the existing idle-based file opening mechanism
		mainframe->openFileFromFinder(fileName);
	} else {
		// Store for later - OnInit hasn't completed yet
		pendingOpenFile = fileName.ToStdString();
	}
}
#endif
