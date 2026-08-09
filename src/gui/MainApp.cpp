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
#endif
#include "wx/fileconf.h"

// Crash reporter: portable pieces (report path, URL helpers, the next-launch
// dialog) compile everywhere; the trace writer is per-platform below.
#include <cstdio>
#include <cctype>
#include <string>
#include "wx/dialog.h"
#include "wx/sizer.h"
#include "wx/stattext.h"
#include "wx/textctrl.h"
#include "wx/button.h"
#include "wx/clipbrd.h"
#include "wx/dataobj.h"
#include "wx/font.h"
#include "wx/filename.h"
#include "wx/utils.h"

#ifdef __APPLE__
#include "SparkleUpdater.h"
#endif
#ifdef _WIN32
#include "WinSparkleUpdater.h"
#include <windows.h>
#include <mmsystem.h>
#include <dbghelp.h>
#include <shellapi.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "shell32.lib")
#else
#include <csignal>
#include <cstring>
#include <execinfo.h>
#include <unistd.h>
#include <fcntl.h>
#endif

IMPLEMENT_APP(MainApp)

// ===== Crash reporter =====================================================
// A trace is written when the app crashes, then offered for reporting: on
// Windows right away (native MessageBox) and, on every platform, via a wx
// dialog the next time the app starts. The report path and header are computed
// once at startup so the crash handler itself stays allocation-light.

static std::string g_crashLogPath;
static std::string g_crashHeader;

static std::string readFile(const std::string &path) {
    std::string out;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return out;
    char buf[4096];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, got);
    fclose(f);
    return out;
}

// Percent-encode everything outside the RFC 3986 unreserved set so a trace can
// ride safely in a GitHub issue URL's query string.
static std::string urlEncode(const std::string &s) {
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 0xF]; }
    }
    return out;
}

// The trace's first line is the version/OS header; the exception and stack
// follow it. Split them so each lands under its own heading in the issue body.
static std::string crashTraceBody(const std::string &trace) {
    size_t nl = trace.find('\n');
    if (nl == std::string::npos) return trace;
    std::string rest = trace.substr(nl + 1);
    size_t start = rest.find_first_not_of('\n');
    return start == std::string::npos ? std::string() : rest.substr(start);
}

static std::string crashVersionLine(const std::string &trace) {
    return trace.substr(0, trace.find('\n'));
}

// The filled-in issue template. Kept as the whole markdown body (headings +
// guiding comments) so it is exactly what we put on the clipboard: if the
// prefilled URL is truncated, the reporter can select-all and paste this.
static std::string crashIssueBody(const std::string &trace) {
    return
        "### Steps to Reproduce\n\n"
        "<!-- Describe what you were doing when it crashed -->\n\n\n\n"
        "### Crash trace\n"
        "<!-- This template was also copied to your clipboard if the trace appears cut off -->\n"
        "<!-- Ctrl+A (or Cmd+A if you are on Mac) and then paste the full trace -->\n\n"
        "```\n" + crashTraceBody(trace) + "\n```\n\n"
        "### Version\n\n" + crashVersionLine(trace) + "\n";
}

// Prefilled "new issue" URL. The full body always goes on the clipboard too;
// the copy in the URL is capped so it stays within what browsers/GitHub accept.
static std::string crashIssueUrl(const std::string &trace) {
    std::string body = crashIssueBody(trace);
    std::string firstFrame = crashTraceBody(trace);
    std::string title = "Crash: " + firstFrame.substr(0, firstFrame.find('\n'));
    if (body.size() > 6000) body.resize(6000);
    return "https://github.com/taciturnaxolotl/CedarLogic/issues/new?title=" +
           urlEncode(title) + "&body=" + urlEncode(body);
}

#ifdef _WIN32
// Copy text to the clipboard with only Win32 APIs, safe to call from the
// crash handler where wx and the app heap can't be trusted.
static void nativeSetClipboard(const std::string &text) {
    if (!OpenClipboard(NULL)) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (h) {
        void *dst = GlobalLock(h);
        if (dst) { memcpy(dst, text.c_str(), text.size() + 1); GlobalUnlock(h); SetClipboardData(CF_TEXT, h); }
    }
    CloseClipboard();
}

// Best-effort nudge from inside the crash handler: the process is in an
// undefined state, so use only native APIs (no wx/GL). Copy the trace to the
// clipboard and offer to open a prefilled GitHub issue right now. The reliable,
// full-featured path is the dialog shown on the next launch.
static void offerCrashReportNative(const std::string &logPath) {
    std::string trace = readFile(logPath);
    if (trace.empty()) return;
    nativeSetClipboard(crashIssueBody(trace));
    int r = MessageBoxW(NULL,
        L"CedarLogic closed unexpectedly.\n\n"
        L"A crash report was saved and copied to your clipboard. "
        L"Open a GitHub issue to report it now?",
        L"CedarLogic crashed", MB_YESNO | MB_ICONERROR | MB_SYSTEMMODAL);
    if (r == IDYES) {
        std::string url = crashIssueUrl(trace);
        std::wstring wurl(url.begin(), url.end());
        ShellExecuteW(NULL, L"open", wurl.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
}

// Shorten a compiler-emitted absolute source path to a repo-relative one with
// forward slashes (C:\...\CedarLogic\src\gui\guiGate.cpp -> src/gui/guiGate.cpp)
// so the trace's file column stays short and consistent. Writes into buf.
static const char *shortSourcePath(const char *full, char *buf, size_t bufLen) {
    const char *p = strstr(full, "src\\");
    if (!p) p = strstr(full, "src/");
    if (!p) { const char *s = strrchr(full, '\\'); p = s ? s + 1 : full; }
    size_t i = 0;
    for (; p[i] && i + 1 < bufLen; i++) buf[i] = (p[i] == '\\') ? '/' : p[i];
    buf[i] = '\0';
    return buf;
}

// On an otherwise-unhandled crash, walk the faulting thread's stack, symbolize
// it against the .pdb shipped next to the exe, and write a readable trace to
// %TEMP%\CedarLogic_crashtrace.log. Turns "it just crashed" into an actual
// function + file:line, which matters for a GUI app where the nastiest bugs
// only surface through live interaction and can't be caught under a debugger.
static LONG WINAPI writeCrashTrace(EXCEPTION_POINTERS *ep) {
    const std::string &logPath = g_crashLogPath;
    FILE *f = fopen(logPath.c_str(), "wb"); // binary: clean '\n', no '\r\n'
    if (f == NULL) return EXCEPTION_CONTINUE_SEARCH;

    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(proc, NULL, TRUE);

    fputs(g_crashHeader.c_str(), f);
    fprintf(f, "Unhandled exception 0x%08lX at %p\n",
            ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);

    // For an access violation, ExceptionInformation[0] is 0=read/1=write/8=DEP
    // and [1] is the faulting address -- the actual bad pointer we dereferenced.
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        ep->ExceptionRecord->NumberParameters >= 2) {
        ULONG_PTR kind = ep->ExceptionRecord->ExceptionInformation[0];
        fprintf(f, "  %s address 0x%p\n",
                kind == 1 ? "write to" : kind == 8 ? "execute at" : "read from",
                (void *)ep->ExceptionRecord->ExceptionInformation[1]);
    }

    // Faulting instruction as module+RVA, which is stable across ASLR runs and
    // can be mapped back to a source line with the .pdb offline.
    DWORD64 faultBase = SymGetModuleBase64(proc, (DWORD64)ep->ExceptionRecord->ExceptionAddress);
    if (faultBase) {
        char mod[MAX_PATH] = {};
        GetModuleFileNameA((HMODULE)faultBase, mod, MAX_PATH);
        fprintf(f, "  fault at %s +0x%llx (base 0x%llx)\n", mod,
                (unsigned long long)((DWORD64)ep->ExceptionRecord->ExceptionAddress - faultBase),
                (unsigned long long)faultBase);
    }
    fprintf(f, "\n");

    CONTEXT *ctx = ep->ContextRecord;
    STACKFRAME64 frame = {};
    DWORD machine;
#if defined(_M_X64)
    machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = ctx->Rip;    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx->Rbp; frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx->Rsp; frame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_IX86)
    machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = ctx->Eip;    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx->Ebp; frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx->Esp; frame.AddrStack.Mode = AddrModeFlat;
#else
    machine = IMAGE_FILE_MACHINE_UNKNOWN;
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
            if (SymGetLineFromAddr64(proc, addr, &lineDisp, &line)) {
                char rel[MAX_PATH];
                fprintf(f, "  %-42s %s:%lu\n", sym->Name,
                        shortSourcePath(line.FileName, rel, sizeof(rel)), line.LineNumber);
            } else {
                fprintf(f, "  %s +0x%llx\n", sym->Name, (unsigned long long)disp);
            }
        } else {
            DWORD64 base = SymGetModuleBase64(proc, addr);
            if (base) {
                char mod[MAX_PATH] = {};
                GetModuleFileNameA((HMODULE)base, mod, MAX_PATH);
                const char *slash = strrchr(mod, '\\');
                fprintf(f, "  %s +0x%llx\n", slash ? slash + 1 : mod,
                        (unsigned long long)(addr - base));
            } else {
                fprintf(f, "  0x%llx\n", (unsigned long long)addr);
            }
        }
    }
    fflush(f);
    fclose(f);

    offerCrashReportNative(logPath);
    return EXCEPTION_CONTINUE_SEARCH; // let the OS still report/terminate as usual
}
#else
// POSIX (macOS/Linux): catch fatal signals and write a backtrace to the log
// with async-signal-safe fd calls only (no malloc, no wx). Symbolization is
// module+symbol+offset via backtrace_symbols_fd; the next-launch dialog then
// offers it for reporting just like on Windows.
static const char *crashSignalName(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV (segmentation fault)";
        case SIGABRT: return "SIGABRT (abort)";
        case SIGBUS:  return "SIGBUS (bus error)";
        case SIGFPE:  return "SIGFPE (floating-point exception)";
        case SIGILL:  return "SIGILL (illegal instruction)";
        default:      return "fatal signal";
    }
}

static void posixCrashHandler(int sig, siginfo_t *, void *) {
    int fd = open(g_crashLogPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, g_crashHeader.data(), g_crashHeader.size());
        const char *label = "Fatal signal: ";
        write(fd, label, strlen(label));
        const char *name = crashSignalName(sig);
        write(fd, name, strlen(name));
        write(fd, "\n\n", 2);
        void *frames[64];
        int n = backtrace(frames, 64);
        backtrace_symbols_fd(frames, n, fd);
        close(fd);
    }
    // Restore the default action and re-raise so the OS still produces its
    // normal crash report / core dump.
    signal(sig, SIG_DFL);
    raise(sig);
}
#endif

// Compute the report path (needs wx) and install the platform crash handler.
// Called once at startup.
static void installCrashHandler() {
    wxFileName fn(wxStandardPaths::Get().GetTempDir(), "CedarLogic_crashtrace.log");
    g_crashLogPath = fn.GetFullPath().ToStdString();
    g_crashHeader = "CedarLogic " + VERSION_NUMBER() + " (" +
                    std::to_string((int)(sizeof(void *) * 8)) + "-bit) on " +
                    wxGetOsDescription().ToStdString() + "\n\n";
#ifdef _WIN32
    SetUnhandledExceptionFilter(writeCrashTrace);
#else
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = posixCrashHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
#endif
}

// Shown on the next launch if a crash log from a previous run is present: the
// reliable, full-featured counterpart to the crash-time nudge. The process is
// healthy here, so this is a normal wx dialog -- the trace in a read-only box,
// a copy button, and a button that opens a prefilled GitHub issue.
static void showPendingCrashReport(wxWindow *parent) {
    const std::string &logPath = g_crashLogPath;
    std::string trace = readFile(logPath);
    if (trace.empty()) return;

    wxDialog dlg(parent, wxID_ANY, "Report a crash", wxDefaultPosition,
                 wxSize(700, 500), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    wxBoxSizer *root = new wxBoxSizer(wxVERTICAL);
    root->Add(new wxStaticText(&dlg, wxID_ANY,
        "CedarLogic closed unexpectedly the last time it ran. Reporting this "
        "helps get it fixed.\nOpen a GitHub issue (the full report is copied to "
        "your clipboard so you can paste it in)."),
        0, wxALL, 12);

    wxTextCtrl *txt = new wxTextCtrl(&dlg, wxID_ANY, wxString::FromUTF8(trace),
        wxDefaultPosition, wxDefaultSize,
        wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    txt->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE)));
    root->Add(txt, 1, wxEXPAND | wxLEFT | wxRIGHT, 12);

    wxBoxSizer *btns = new wxBoxSizer(wxHORIZONTAL);
    wxButton *copyBtn = new wxButton(&dlg, wxID_ANY, "Copy to clipboard");
    wxButton *issueBtn = new wxButton(&dlg, wxID_ANY, "Open GitHub issue");
    wxButton *closeBtn = new wxButton(&dlg, wxID_CANCEL, "Dismiss");
    btns->Add(copyBtn, 0, wxALL, 6);
    btns->AddStretchSpacer();
    btns->Add(issueBtn, 0, wxALL, 6);
    btns->Add(closeBtn, 0, wxALL, 6);
    root->Add(btns, 0, wxEXPAND | wxALL, 6);
    dlg.SetSizer(root);

    std::string report = crashIssueBody(trace);
    auto copyReport = [report]() {
        if (wxTheClipboard->Open()) {
            wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(report)));
            wxTheClipboard->Close();
        }
    };
    copyBtn->Bind(wxEVT_BUTTON, [copyReport](wxCommandEvent &) { copyReport(); });
    issueBtn->Bind(wxEVT_BUTTON, [trace, copyReport](wxCommandEvent &) {
        copyReport();
        wxLaunchDefaultBrowser(wxString::FromUTF8(crashIssueUrl(trace)));
    });

    dlg.ShowModal();
    remove(logPath.c_str()); // only prompt once per crash
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
    installCrashHandler();
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
#ifdef __APPLE__
	helpController = new wxHtmlHelpController(wxHF_DEFAULT_STYLE | wxHF_OPEN_FILES);
	// Only load help if the file exists to avoid blocking
	if (wxFileExists(appConfig().appSettings.helpFile)) {
		if (!helpController->AddBook(appConfig().appSettings.helpFile)) {
			wxLogWarning("Failed to load help file: %s", appConfig().appSettings.helpFile);
		}
	}
#else
	helpController = new wxHelpController;
	helpController->Initialize(appConfig().appSettings.helpFile);
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
        fflush(nullptr);
        std::_Exit(ok ? 0 : 1);
    }
#endif

    // Headless render mode: `--render <input.cdl> <output.png> [width height]`.
    // Loads the circuit, writes a PNG of it, and exits without a main loop.
    string cmdFilename;
    string renderOutput;
    int renderW = 1600, renderH = 1000;
    bool renderSkia = false;   // --render-skia routes through Skia instead of GL
    bool renderSvg = false;    // --render-svg writes a vector SVG via Skia
    bool renderPdf = false;    // --render-pdf writes a vector PDF via Skia
    if (argc >= 4 && (wxString(argv[1]) == "--render" ||
                      wxString(argv[1]) == "--render-skia" ||
                      wxString(argv[1]) == "--render-svg" ||
                      wxString(argv[1]) == "--render-pdf")) {
        renderMode().headlessRender = true;
        renderSkia = (wxString(argv[1]) == "--render-skia");
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


    // create the main application window
    MainFrame *frame = new MainFrame(VERSION_TITLE(), cmdFilename);

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
                std::_Exit(1);
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
        frame->load(cmdFilename);
        wxYield();
        bool ok = renderPdf
            ? frame->renderToPdfSkia(renderOutput, renderW, renderH,
                                     /*showGrid=*/true, /*noColor=*/false)
            : renderSvg
            ? frame->renderToSvgSkia(renderOutput, renderW, renderH,
                                     /*showGrid=*/true, /*noColor=*/false)
            : renderSkia
            ? frame->renderToPngSkia(renderOutput, renderW, renderH)
            : frame->renderToPng(renderOutput);
        // The PNG is written; exit immediately rather than tear down the (shown)
        // frame + autosave thread, which otherwise hangs this one-shot process.
        fflush(nullptr);
        std::_Exit(ok ? 0 : 1);
    }

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

    // If the previous run left a crash trace, offer it for reporting once the
    // main window is up (deferred so it appears over a painted frame).
    CallAfter([this]() { showPendingCrashReport(mainframe); });

    return true;
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
		appConfig().resourcesDir = stdp.GetResourcesDir() + "/";
	}

	wxFileConfig *conf = new wxFileConfig("CedarLogic");
	wxConfigBase::Set(conf);
	wxConfigBase::DontCreateOnDemand();

	wxString str;
	conf->Read("GateLib", &str, "res/cl_gatedefs.xml");
	appConfig().appSettings.gateLibFile = appConfig().resourcesDir + str;

	#ifdef __APPLE__
	conf->Read("HelpFile", &str, "res/help/KLS_Logic.hhp");
#else
	conf->Read("HelpFile", &str, "res/KLS_Logic.chm");
#endif
	appConfig().appSettings.helpFile = appConfig().resourcesDir + str;

	conf->Read("TextFont", &str, "res/arial.glf");
	appConfig().appSettings.textFontFile = appConfig().resourcesDir + str;

	conf->Read("LastDirectory", &str, "");
	appConfig().appSettings.lastDir = str;

	conf->Read("FrameWidth", &appConfig().appSettings.mainFrameWidth, 600);
	conf->Read("FrameHeight", &appConfig().appSettings.mainFrameHeight, 600);
	conf->Read("FrameLeft", &appConfig().appSettings.mainFrameLeft, 20);
	conf->Read("FrameTop", &appConfig().appSettings.mainFrameTop, 20);
	conf->Read("RefreshRate", &appConfig().appSettings.refreshRate, 16); // ms (~60 FPS)
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
