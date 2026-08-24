// The whole of wxWidgets that the ported CedarLogic core actually touches.
//
// The model, geometry, parsing, command, and Scene-emitting layers are all but
// wx-free already: what remains is a wxString here, a wxMutexLocker there, and
// the wxDocument/wxCommand base classes the circuit and undo stack inherit.
// This header supplies exactly those, so the core compiles for the browser
// without a wx port and without #ifdef'ing the desktop sources.
//
// Every individual wx/*.h in this shim tree forwards here, so an #include the
// desktop source already has keeps working unchanged.
#ifndef CEDAR_WASM_WX_HEADLESS_H
#define CEDAR_WASM_WX_HEADLESS_H

#include <string>
#include <sstream>
#include <cstdint>

// ---------------------------------------------------------------- macros ---

#define wxOK          0x00000004
#define wxCANCEL      0x00000010
#define wxYES_NO      0x0000000A
#define wxICON_ERROR  0x00000100
#define wxICON_WARNING 0x00000200
#define wxICON_INFORMATION 0x00000800
#define wxT(s) s
#define wxEmptyString wxString()

#define IMPLEMENT_DYNAMIC_CLASS(a, b)
#define DECLARE_DYNAMIC_CLASS(a)
// DECLARE_APP(T) normally emits `T& wxGetApp()`. Keep that: the ported
// sources call wxGetApp().logfile, and wasm/gui/app.cpp defines the one
// headless MainApp instance it returns.
#define DECLARE_APP(a) extern a &wxGetApp();
#define IMPLEMENT_APP(a)

typedef long wxCoord;

// --------------------------------------------------------------- wxString ---

// wxString is used for message text and file paths only, so std::string with
// wx's streaming operator<< and ToStdString() covers every call site.
class wxString : public std::string {
public:
	wxString() {}
	wxString(const char *s) : std::string(s ? s : "") {}
	wxString(const std::string &s) : std::string(s) {}

	std::string ToStdString() const { return *this; }
	const char *mb_str() const { return c_str(); }

	template <class T>
	wxString &operator<<(const T &v) {
		std::ostringstream o;
		o << v;
		append(o.str());
		return *this;
	}
};

// ------------------------------------------------------- geometry structs ---

struct wxPoint {
	int x, y;
	wxPoint() : x(0), y(0) {}
	wxPoint(int x_, int y_) : x(x_), y(y_) {}
};

struct wxSize {
	int x, y;
	wxSize() : x(0), y(0) {}
	wxSize(int w, int h) : x(w), y(h) {}
	int GetWidth() const { return x; }
	int GetHeight() const { return y; }
};

// ------------------------------------------------------------ threading ---

// The browser build runs the simulation on the same thread as the UI, so the
// locks the desktop build needs between the GUI and logic threads degrade to
// no-ops rather than disappearing from the call sites.
class wxMutex {
public:
	void Lock() {}
	void Unlock() {}
	bool TryLock() { return true; }
};

class wxMutexLocker {
public:
	explicit wxMutexLocker(wxMutex &) {}
	bool IsOk() const { return true; }
};

class wxSemaphore {
public:
	wxSemaphore(int = 0, int = 0) {}
	void Post() {}
	void Wait() {}
	bool TryWait() { return true; }
};

class wxCondition {
public:
	explicit wxCondition(wxMutex &) {}
	void Signal() {}
	void Broadcast() {}
	void Wait() {}
};

class wxCriticalSection {
public:
	void Enter() {}
	void Leave() {}
};

class wxCriticalSectionLocker {
public:
	explicit wxCriticalSectionLocker(wxCriticalSection &) {}
};

// threadLogic derives from wxThread. In the browser the logic "thread" is
// driven synchronously from the render loop, so Entry() is called directly and
// the lifecycle calls below are inert.
class wxThread {
public:
	enum ExitCode_ { kOk = 0 };
	typedef void *ExitCode;
	enum { wxTHREAD_NO_ERROR = 0 };
	virtual ~wxThread() {}
	virtual void *Entry() = 0;
	virtual void OnExit() {}
	int Create(unsigned = 0) { return 0; }
	int Run() { return 0; }
	void Delete() {}
	bool TestDestroy() { return false; }
	static void Sleep(unsigned long) {}
	static void Yield() {}
};

class wxStopWatch {
public:
	wxStopWatch() : fStart(0) {}
	void Start(long t = 0) { fStart = t; }
	void Pause() {}
	void Resume() {}
	long Time() const { return 0; }
private:
	long fStart;
};

// ---------------------------------------------------------- doc/command ---

// wxCommand is the undo-stack element klsCommand derives from; wxCommandProcessor
// is the stack itself. The browser build wants the same Do/Undo contract, so
// this is a working implementation rather than a stub -- see cmdStack in the
// web shell, which drives it.
class wxCommand {
public:
	wxCommand(bool canUndo = false, const wxString &name = wxString())
		: fCanUndo(canUndo), fName(name) {}
	virtual ~wxCommand() {}

	virtual bool Do() = 0;
	virtual bool Undo() = 0;

	virtual bool CanUndo() const { return fCanUndo; }
	wxString GetName() const { return fName; }

private:
	bool fCanUndo;
	wxString fName;
};

class wxCommandProcessor {
public:
	virtual ~wxCommandProcessor() {}
	virtual bool Submit(wxCommand *cmd, bool = true) {
		return cmd && cmd->Do();
	}
};

class wxDocument {
public:
	virtual ~wxDocument() {}
	virtual bool OnNewDocument() { return true; }
	void Modify(bool m) { fModified = m; }
	bool IsModified() const { return fModified; }
private:
	bool fModified = false;
};

// ------------------------------------------------------------- gui shell ---

// The browser owns the window, the event loop, and the drawing surface, so the
// wx widget classes exist here only as empty bases: MainApp still derives from
// wxApp, canvases still derive from wxGLCanvas, and none of it does anything.
// Input arrives from JavaScript through the binding layer instead.
class wxEvtHandler {
public:
	virtual ~wxEvtHandler() {}
};

class wxWindow : public wxEvtHandler {
public:
	wxSize GetClientSize() const { return fSize; }
	void SetClientSize(const wxSize &s) { fSize = s; }
	void Refresh(bool = true) {}
	void Update() {}
private:
	wxSize fSize{ 0, 0 };
};

class wxApp : public wxEvtHandler {
public:
	virtual bool OnInit() { return true; }
	virtual int OnExit() { return 0; }
};

class wxGLAttributes {
public:
	wxGLAttributes &PlatformDefaults() { return *this; }
	wxGLAttributes &RGBA() { return *this; }
	wxGLAttributes &DoubleBuffer() { return *this; }
	wxGLAttributes &Depth(int) { return *this; }
	wxGLAttributes &Stencil(int) { return *this; }
	wxGLAttributes &SampleBuffers(int) { return *this; }
	wxGLAttributes &Samplers(int) { return *this; }
	wxGLAttributes &EndList() { return *this; }
};

class wxGLContext;

class wxGLCanvas : public wxWindow {
public:
	bool SetCurrent(const wxGLContext &) const { return true; }
	void SwapBuffers() {}
};

class wxGLContext {
public:
	explicit wxGLContext(wxGLCanvas * = nullptr) {}
};

class wxHelpController {
public:
	bool DisplayContents() { return false; }
};

class wxHtmlHelpController : public wxHelpController {};

// ------------------------------------------------------------- messages ---

// Dialogs have no headless equivalent. Errors surface to JavaScript through
// the binding layer's error channel instead; see wasm/gui/app.cpp.
inline int wxMessageBox(const wxString &, const wxString & = wxString(),
                        long = 0, void * = nullptr) { return wxOK; }

#endif
