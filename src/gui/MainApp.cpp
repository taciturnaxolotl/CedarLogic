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
#include <cstdlib>   // std::_Exit for the headless --render one-shot
#include <fstream>
#include <sstream>
#include "migrate.hpp"   // cl::loadCircuit, to validate a file before the GUI load
#include "wx/stdpaths.h"
#ifdef WITH_SKIA
#include "render/SkiaProbe.h"   // headless --skia-probe (no Skia headers leak here)
#include "render/RendererHealth.h"
#endif
#include "wx/fileconf.h"

// Crash reporter: the trace writer, the startup marker and the next-launch
// dialog each live in their own unit.
#include <cstdio>
#include <cstdlib>
#include <string>
#include "UpdateInfo.h"
#include "CrashReportDialog.h"
#include "CrashTrace.h"
#include "StartupMarker.h"
#include "wx/filename.h"
#include "wx/utils.h"

#ifdef __APPLE__
#include "SparkleUpdater.h"
#endif
#ifdef _WIN32
#include "WinSparkleUpdater.h"
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

IMPLEMENT_APP(MainApp)

static cl::StartupMarker &startupMarker() {
    static cl::StartupMarker marker(
        wxStandardPaths::Get().GetTempDir().ToStdString());
    return marker;
}

// A one-shot run never reaches a window, so it must retract the marker it armed.
[[noreturn]] static void exitOneShot(int code) {
    startupMarker().disarm();
    std::fflush(nullptr);
    std::_Exit(code);
}

static const wxCmdLineEntryDesc g_cmdLineDesc[] =
{
	{ wxCMD_LINE_PARAM, NULL, NULL, "input file", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL },
	{ wxCMD_LINE_NONE }
};

MainApp::MainApp()
{
    paletteDrag().showDragImage = false;
    mainframe = NULL;
    renderMode().doingBitmapExport = false;
	glContext = NULL;
#ifdef __WXGTK__
	// On Linux with wayland, wxGTK doesn't position glCanvas frames correctly.
	// This env var has to be set explicitly to instruct gtk to only use X11.
	::setenv("GDK_BACKEND", "x11", /* replace */ true);
#endif
}

bool MainApp::OnInit()
{
    cl::crash::installCrashHandler();

    // Arm the startup marker before anything can fail, so a crash below is
    // recognisable as a startup crash on the next launch rather than merely "a
    // crash". Cleared once the main window exists.
    const int previousStartupFailures = startupMarker().consecutiveFailures();
    startupMarker().arm(previousStartupFailures + 1);
#ifdef _WIN32
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
#ifdef _WIN32
	// Windows hands the .chm to the system help viewer.
	helpController = new wxHelpController;
	helpController->Initialize(appConfig().appSettings.helpFile);
#else
	// Everywhere else wx renders the help book itself, from the same loose
	// HTML the .chm is built from. Linux used to be pointed at the .chm too,
	// which wx cannot open without libmspack, so help there did nothing at all.
	helpController = new wxHtmlHelpController(wxHF_DEFAULT_STYLE | wxHF_OPEN_FILES);
	// Only load help if the file exists to avoid blocking
	if (wxFileExists(appConfig().appSettings.helpFile)) {
		if (!helpController->AddBook(appConfig().appSettings.helpFile)) {
			wxLogWarning("Failed to load help file: %s", appConfig().appSettings.helpFile);
		}
	}
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
    // `--update-status [file]`: print how update checking is configured and
    // exit. A policy that never took effect looks exactly like one that did --
    // an application quietly not checking for updates -- so without this an
    // administrator has no way to tell a working deployment from a typo.
    // Exits 0 when checks are enabled, 1 when a policy has turned them off, so
    // a deployment script can assert on it.
    if (argc >= 2 && wxString(argv[1]) == "--update-status") {
        const cl::update::PolicyStatus st = cl::update::describeUpdatePolicy();
        std::string out;
        out += "CedarLogic update status\n\n";
        out += std::string("Update checks: ") +
               (st.disabled ? "DISABLED by administrator policy\n" : "enabled\n");
        if (!st.platformNote.empty()) {
            out += "\n" + st.platformNote + "\n";
        } else {
            out += "\nPolicy  HKLM\\SOFTWARE\\Policies\\Cedarville University\\CedarLogic\n";
            out += "        DisableUpdateChecks";
            if (st.policyFound) {
                out += " = " + st.policyData + "  (" + st.policyType + ", " +
                       st.policyView + " view)\n";
            } else {
                out += " is not set\n";
            }
        }
        if (st.sparkleFound) {
            out += "\nWinSparkle  CheckForUpdates = \"" + st.sparkleValue +
                   "\"  (found in " + st.sparkleWhere + ")\n";
            if (!st.sparkleReachable) {
                out +=
                    "        This value has no effect. CedarLogic is 32-bit, so\n"
                    "        the updater reads HKLM\\SOFTWARE\\WOW6432Node and never\n"
                    "        sees the plain path. Write it in the 32-bit view, or\n"
                    "        better, use DisableUpdateChecks above, which is read\n"
                    "        from both views and cannot be overridden by a user.\n";
            } else if (!st.disabled && st.sparkleValue == "0") {
                out +=
                    "        This is NOT the policy, and it is not enforced: a\n"
                    "        user's own copy under HKCU overrides it. To turn\n"
                    "        update checks off for the machine, set\n"
                    "        DisableUpdateChecks above instead.\n";
            }
        }
#ifdef _WIN32
        // A GUI-subsystem program has no console of its own, so borrow the one
        // that launched it. Redirection to a file works either way.
        if (::AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE *unused = nullptr;
            freopen_s(&unused, "CONOUT$", "w", stdout);
        }
#endif
        if (argc >= 3) {
            if (FILE *f = fopen(wxString(argv[2]).ToStdString().c_str(), "w")) {
                fputs(out.c_str(), f);
                fclose(f);
            }
        }
        fputs(out.c_str(), stdout);
        exitOneShot(st.disabled ? 1 : 0);
    }

#ifdef WITH_SKIA
    // Headless Skia render proof: `--skia-probe <output.png> [width height]`.
    // Renders through Skia into an offscreen raster surface (no window, no GL)
    // and writes a PNG, then exits -- runs in CI with no display and proves Skia
    // rasterizes on this machine.
    if (argc >= 3 && wxString(argv[1]) == "--skia-probe") {
        int w = 320, h = 200;
        if (argc >= 5) { w = wxAtoi(argv[3]); h = wxAtoi(argv[4]); }
        bool ok = cl::render::skiaProbeToPng(
            wxString(argv[2]).ToStdString().c_str(), w, h);
        exitOneShot(ok ? 0 : 1);
    }
#endif

    // Headless render mode: `--render <input.cdl> <output.png> [width height]`.
    // Loads the circuit, writes a PNG of it, and exits without a main loop.
    string cmdFilename;
    string renderOutput;
    int renderW = 1600, renderH = 1000;
    bool renderSvg = false;    // --render-svg writes a vector SVG via Skia
    bool renderPdf = false;    // --render-pdf writes a vector PDF via Skia
    bool renderGate = false;   // --render-gate renders one library gate
    bool wireShape = false;    // --wire-shape dumps a routed wire's segment map
    bool wireDrag = false;     // --wire-drag dumps a wire's segment map after a seg drag
    bool wireDragV = false;    // --wire-drag-vertical, the same but dragging an upright segment
    bool wireMerge = false;    // --wire-merge dumps a wire's segment map after a merge
    std::string gateName, gateAngle;
    std::string wsGateA, wsGateB, wsAngleA, wsAngleB;
    if (argc >= 7 && (wxString(argv[1]) == "--wire-shape" ||
                      wxString(argv[1]) == "--wire-drag" ||
                      wxString(argv[1]) == "--wire-drag-vertical" ||
                      wxString(argv[1]) == "--wire-merge")) {
        // --wire-{shape,drag,drag-vertical,merge} <gateA> <gateB> <angleA> <angleB> <out.txt>
        renderMode().headlessRender = true;
        wireShape = (wxString(argv[1]) == "--wire-shape");
        wireDrag = (wxString(argv[1]) == "--wire-drag") ||
                   (wxString(argv[1]) == "--wire-drag-vertical");
        wireDragV = (wxString(argv[1]) == "--wire-drag-vertical");
        wireMerge = (wxString(argv[1]) == "--wire-merge");
        wsGateA = argv[2].ToStdString();
        wsGateB = argv[3].ToStdString();
        wsAngleA = argv[4].ToStdString();
        wsAngleB = argv[5].ToStdString();
        renderOutput = argv[6].ToStdString();
    } else if (argc >= 5 && (wxString(argv[1]) == "--render-gate" ||
                      wxString(argv[1]) == "--render-gate-skia")) {
        // --render-gate <GATENAME> <angle> <out.png> [W H]. Skia is the only
        // renderer, so --render-gate-skia is an accepted alias for the same path.
        renderMode().headlessRender = true;
        renderGate = true;
        gateName = argv[2].ToStdString();
        gateAngle = argv[3].ToStdString();
        renderOutput = argv[4].ToStdString();
        if (argc >= 7) { renderW = wxAtoi(argv[5]); renderH = wxAtoi(argv[6]); }
    } else if (argc >= 4 && (wxString(argv[1]) == "--render" ||
                      wxString(argv[1]) == "--render-skia" ||
                      wxString(argv[1]) == "--render-svg" ||
                      wxString(argv[1]) == "--render-pdf")) {
        // --render-skia is an accepted alias of --render (one renderer).
        renderMode().headlessRender = true;
        renderSvg = (wxString(argv[1]) == "--render-svg");
        renderPdf = (wxString(argv[1]) == "--render-pdf");
        cmdFilename = argv[2].ToStdString();
        renderOutput = argv[3].ToStdString();
        if (argc >= 6) { renderW = wxAtoi(argv[4]); renderH = wxAtoi(argv[5]); }
    } else if( argc >= 2 ){
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


    //////////////////////////////////////////////////////////////////////////
    // Startup-crash recovery
    //
    // If the last run died before its window appeared, the work below is very
    // likely to die the same way again. Offer a fix first: the report dialog,
    // which checks the appcast and names a newer version when one exists. This
    // is the only chance a broken install gets, because the updater is
    // initialised further down and a crash above it means it never runs, so the
    // app can never update itself out of that state on its own.
    //
    // One failure is treated as possibly a fluke; the dialog only pre-empts
    // startup once it has happened twice running. Headless runs (--render and
    // friends, used by CI) never see it, since a modal dialog would hang them.
    //////////////////////////////////////////////////////////////////////////
    if (previousStartupFailures + 1 >= cl::kStartupCrashRecoveryThreshold &&
        !renderMode().headlessRender) {
        if (cl::crash::showPendingCrashReport(NULL, /*duringStartup=*/true)) {
            // The releases page rather than the in-app updater: its installer
            // relaunches us, on a thread that OnInit returning would tear down.
            wxLaunchDefaultBrowser(
                "https://github.com/taciturnaxolotl/CedarLogic/releases/latest");
            // Not `return false`: wx skips OnExit then, and OnExit is where
            // this app terminates itself (see there).
            std::fflush(nullptr);
            std::_Exit(0);
        }
        // "Continue anyway": fall through and try to start normally.
    }

    // create the main application window
    MainFrame *frame = new MainFrame(VERSION_TITLE(), cmdFilename);

    if (renderMode().headlessRender && (wireShape || wireDrag || wireMerge)) {
        // Wire-router test path: two gates + a wire, dump the routed segment map
        // (--wire-shape), the map after a programmatic segment drag (--wire-drag),
        // or the map after every segment is cut up and rejoined (--wire-merge).
        frame->SetSize(renderW + 220, renderH + 140);
        frame->Show(true);
        wxYield();
        bool ok = wireDrag
            ? frame->dumpWireDrag(wsGateA, wsGateB, wsAngleA, wsAngleB,
                                  wireDragV ? "1" : "0", renderOutput)
            : wireMerge
            ? frame->dumpWireMerge(wsGateA, wsGateB, wsAngleA, wsAngleB, renderOutput)
            : frame->dumpWireShape(wsGateA, wsGateB, wsAngleA, wsAngleB, renderOutput);
        exitOneShot(ok ? 0 : 1);
    }

    if (renderMode().headlessRender && renderGate) {
        // Single-gate golden path: no file to load. Realize the frame so the
        // canvas has a client size + render geometry, place one gate, render,
        // and exit like the file path below.
        frame->SetSize(renderW + 220, renderH + 140);
        frame->Show(true);
        wxYield();
        bool ok = frame->renderSingleGate(gateName, gateAngle, renderOutput,
                                          renderW, renderH);
        exitOneShot(ok ? 0 : 1);
    }

    if (renderMode().headlessRender) {
        // Validate the file up front. A missing or malformed file would otherwise
        // pop a modal error in the load path -- which hangs this windowless
        // one-shot -- so parse it here first and exit cleanly if it won't load.
        {
            std::ifstream in(cmdFilename.c_str(), std::ios::binary);
            std::ostringstream ss;
            ss << in.rdbuf();
            try {
                cl::loadCircuit(ss.str());
            } catch (const std::exception &) {
                exitOneShot(1);
            } catch (...) {
                // See CircuitParse::readCircuit: in this binary the typed
                // handler alone does not catch a throw from libCircuitFile.
                exitOneShot(1);
            }
        }
        // Realize + size the window so the canvas has a client size, load the
        // circuit synchronously, render it offscreen, and exit. Realizing the
        // frame is required even for the Skia raster path: it initializes the
        // canvas + wire/gate render geometry that renderToScene reads (skipping
        // it crashes). Works with a real display; hangs under headless xvfb.
        frame->SetSize(renderW + 220, renderH + 140);
        frame->Show(true);
        wxYield();
        // Nothing may step the simulation on its own from here on: the picture
        // has to be of a circuit this code stepped a known number of times, not
        // of wherever a free-running timer had got to. settleSimulation drives
        // every step from now until the render.
        frame->stopTimers();
        frame->load(cmdFilename);
        wxYield();
        // Settle the circuit before drawing it. Without this the picture caught
        // the simulation wherever the logic thread happened to have got to, so
        // the same file could render two different images. See settleSimulation.
        frame->settleSimulation();
        bool ok = renderPdf
            ? frame->renderToPdfSkia(renderOutput, renderW, renderH,
                                     /*showGrid=*/true, /*noColor=*/false)
            : renderSvg
            ? frame->renderToSvgSkia(renderOutput, renderW, renderH,
                                     /*showGrid=*/true, /*noColor=*/false)
            : frame->renderToPngSkia(renderOutput, renderW, renderH);
        // The PNG is written; exit immediately rather than tear down the (shown)
        // frame + autosave thread, which otherwise hangs this one-shot process.
        exitOneShot(ok ? 0 : 1);
    }

    //**********************************************************
    //Edit by Joshua Lansford 12/31/06
    //Acording to 
    //http://www.wxwidgets.org/manuals/2.6.3/wx_wxappoverview.html#wxappoverview
    //the following function should be called at this time
    SetTopWindow(frame);
    
    mainframe = frame;
    //End of edit***********************************************

    // The main window exists, so this run is not a startup crash. Clearing the
    // marker here is what lets the next launch tell the two apart.
    startupMarker().disarm();

#ifdef __APPLE__
    // Initialize Sparkle auto-updater
    SparkleUpdater_Initialize();
    // Then, if this copy is somewhere it should not be run from, say so. After
    // MainFrame's crash recovery prompt on purpose: getting the last session's
    // work back on screen is the more urgent of the two.
    SparkleUpdater_WarnIfReadOnlyLocation();
#endif
#ifdef _WIN32
    // Initialize WinSparkle auto-updater, unless an administrator has turned
    // update checking off for this machine (see cl::update::checksDisabled).
    // Skipping win_sparkle_init() entirely means no background thread and no
    // request ever leaves the machine, rather than relying on WinSparkle's own
    // setting, which a user's HKCU value takes precedence over.
    if (!cl::update::checksDisabled()) {
        WinSparkleUpdater_Initialize();
    }
#endif

    // success: wxApp::OnRun() will be called which will enter the main message
    // loop and the application will run. If we returned false here, the
    // application would exit immediately

    // If the previous run left a crash trace, offer it for reporting once the
    // main window is up (deferred so it appears over a painted frame). Harmless
    // if the startup path above already ran: it deleted the trace.
    CallAfter([this]() { cl::crash::showPendingCrashReport(mainframe, /*duringStartup=*/false); });

    return true;
}

// Locate the directory holding res/, for the files still read from disk (the
// help book). The gate library and the icons are compiled in, so a wrong answer
// here no longer keeps the app from starting. It is still worth getting right:
// wxStandardPaths infers a prefix from a /bin/ in the executable path and a
// build tree has none, so probe the build layouts too -- res/ is copied to the
// build root, which is the executable's own directory for a single-config build
// and its parent for a multi-config one.
static wxString findResourcesDir(const wxStandardPathsBase& stdp) {
	wxString candidates[3];
	candidates[0] = stdp.GetResourcesDir();

	wxFileName exeDir(stdp.GetExecutablePath());
	exeDir.SetFullName("");
	candidates[1] = exeDir.GetPath();
	exeDir.RemoveLastDir();
	candidates[2] = exeDir.GetPath();

	for (const wxString& dir : candidates) {
		if (dir.empty()) continue;
		if (wxFileName::DirExists(dir + "/res")) {
			return dir + "/";
		}
	}
	return candidates[0] + "/";
}

void MainApp::loadSettings() {
	wxStandardPathsBase& stdp = wxStandardPaths::Get();
	stdp.SetFileLayout(wxStandardPaths::FileLayout_XDG);

	if (const char* r_dir = getenv("CEDARLOGIC_RESOURCES_DIR")) {
		appConfig().resourcesDir = r_dir;
		if (!appConfig().resourcesDir.empty()) {
			appConfig().resourcesDir += "/";
		}
	} else {
		appConfig().resourcesDir = findResourcesDir(stdp);
	}
#ifdef WITH_SKIA
	cl::render::setFontSearchDir(appConfig().resourcesDir.c_str());
#endif

	wxFileConfig *conf = new wxFileConfig("CedarLogic");
	wxConfigBase::Set(conf);
	wxConfigBase::DontCreateOnDemand();

	wxString str;
#ifdef _WIN32
	conf->Read("HelpFile", &str, "res/KLS_Logic.chm");
#else
	conf->Read("HelpFile", &str, "res/help/KLS_Logic.hhp");
	// Older builds pointed every platform at the Windows .chm, and that value is
	// still sitting in existing config files. wx cannot read a .chm here, so
	// honouring it would just open an empty help window.
	if (str.Lower().EndsWith(".chm")) {
		str = "res/help/KLS_Logic.hhp";
	}
#endif
	appConfig().appSettings.helpFile = appConfig().resourcesDir + str;

	conf->Read("LastDirectory", &str, "");
	appConfig().appSettings.lastDir = str;

	conf->Read("FrameWidth", &appConfig().appSettings.mainFrameWidth, 600);
	conf->Read("FrameHeight", &appConfig().appSettings.mainFrameHeight, 600);
	conf->Read("FrameLeft", &appConfig().appSettings.mainFrameLeft, 20);
	conf->Read("FrameTop", &appConfig().appSettings.mainFrameTop, 20);
	conf->Read("RefreshRate", &appConfig().appSettings.refreshRate, 16); // ms (~60 FPS)
	conf->Read("AutosaveSeconds", &appConfig().appSettings.autosaveSeconds, 180);
	conf->Read("TimeStep", &appConfig().appSettings.timePerStep, 25); // ms
	appConfig().timeStepMod = appConfig().appSettings.timePerStep;
	conf->Read("WireConnRadius", &appConfig().appSettings.wireConnRadius, 0.18f);
	conf->Read("WireConnVisible", &appConfig().appSettings.wireConnVisible, true);
	conf->Read("GridlineVisible", &appConfig().appSettings.gridlineVisible, true);
	conf->Read("RightClickRotate", &appConfig().appSettings.rightClickRotate, true);

	// check screen coords
	wxScreenDC sdc;
	if ( appConfig().appSettings.mainFrameLeft + appConfig().appSettings.mainFrameWidth > sdc.GetSize().GetWidth() ||
		appConfig().appSettings.mainFrameTop + appConfig().appSettings.mainFrameHeight > sdc.GetSize().GetHeight() ) {

		appConfig().appSettings.mainFrameWidth = appConfig().appSettings.mainFrameHeight = 600;
		appConfig().appSettings.mainFrameLeft = appConfig().appSettings.mainFrameTop = 20;
	}
}

int MainApp::OnExit() {
#ifdef _WIN32
	// Stop the WinSparkle updater's background thread. Symmetric with the
	// win_sparkle_init() in OnInit -- it was never called, so the updater thread
	// outlived the app.
	WinSparkleUpdater_Cleanup();
#endif
	delete glContext;
	glContext = NULL;
#ifdef _WIN32
	timeEndPeriod(1);
#endif
	int rc = wxApp::OnExit();
#ifdef _WIN32
	// The app's own shutdown is complete here: settings are saved (in
	// ~MainFrame), the logic/autosave threads are stopped, the GL context and
	// per-frame objects are freed, and the updater thread is down. wxWidgets'
	// post-OnExit framework teardown then spins forever winding down the detached
	// threads, so the process never terminates even though nothing is left to do.
	// Exit now rather than return into that teardown -- the same approach the
	// headless --render one-shot already takes. All persistent state is flushed.
	std::fflush(nullptr);
	std::_Exit(rc);
#endif
	return rc;
}

const wxGLAttributes& glCanvasAttributes()
{
	static const wxGLAttributes attrs = [] {
		wxGLAttributes a;
		a.PlatformDefaults().RGBA().DoubleBuffer().Stencil(8).EndList();
		return a;
	}();
	return attrs;
}

void MainApp::SetCurrentCanvas(wxGLCanvas *canvas)
{
	if (!glContext) {
#ifdef __WXOSX__
		// macOS hands out a legacy 2.1 context unless asked otherwise, and there
		// the shading language stops at GLSL 1.10 -- old enough that Skia's atlas
		// path renderer emits gl_VertexID into a #version 110 shader, which fails
		// to compile on every path-heavy frame before Skia falls back. 3.2 core is
		// the newest profile macOS offers and the one Ganesh expects. Windows and
		// Linux already hand out a modern compatibility context, so they keep the
		// driver default.
		//
		// Except when simulating an old machine. The processor fallback draws
		// with the fixed pipeline, which a core profile removes, so a forced
		// failure has to take the legacy context too. Otherwise the test would
		// cover a combination no real machine has: Windows and Linux, where the
		// fallback actually runs, are on compatibility contexts already.
		if (cl::render::forceGLFailure()) {
			glContext = new wxGLContext(canvas);
		} else {
			wxGLContextAttrs ctxAttrs;
			ctxAttrs.CoreProfile().EndList();
			glContext = new wxGLContext(canvas, NULL, &ctxAttrs);
		}
#else
		glContext = new wxGLContext(canvas);
#endif
	}
	glContext->SetCurrent(*canvas);

#ifdef __WXMSW__
	// Turn vsync OFF (once; needs a current context). Every canvas -- main, oscope,
	// minimap -- shares this GL context and ends its paint with SwapBuffers. With
	// vsync on, each swap blocks the GUI thread until the next vblank (up to
	// ~16ms), and this app repaints on demand rather than running a frame loop, so
	// there is nothing to gain from pacing to the display. With the interval at 0
	// SwapBuffers returns immediately (measured ~0.03ms), keeping the thread free
	// for input and the simulation.
	static bool s_vsyncSet = false;
	if (!s_vsyncSet) {
		s_vsyncSet = true;
		typedef BOOL(WINAPI * SwapIntervalProc)(int);
		SwapIntervalProc setSwap =
			(SwapIntervalProc)wglGetProcAddress("wglSwapIntervalEXT");
		if (setSwap) setSwap(0);
	}
#endif
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
