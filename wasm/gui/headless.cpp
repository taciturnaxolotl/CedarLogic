// The desktop-only pieces of the core, answered for a browser.
//
// Three gate methods open wxWidgets windows, and the desktop defines them in
// src/gui/guiGateDialogs.cpp. The browser answers a parameter edit with its own
// UI and submits the same cmdSetParams, so here they do nothing -- but they must
// still exist, because they are virtual and the vtables reference them.
//
// The one MainApp instance is here for the same reason: a handful of core files
// still log through wxGetApp().logfile.

#include <fstream>

#include "MainApp.h"
#include "guiGate.h"

MainApp::MainApp() {}
bool MainApp::OnInit() { return true; }
int MainApp::OnExit() { return 0; }
void MainApp::SetCurrentCanvas(wxGLCanvas *) {}

// wxGetApp() is what DECLARE_APP(MainApp) declares. The log stream is left
// unopened: writing to a closed ofstream is a no-op, which is exactly the
// behaviour wanted in a browser with no filesystem to log to.
MainApp &wxGetApp() {
	static MainApp app;
	return app;
}

void guiGate::doParamsDialog(void *, wxCommandProcessor *) {}

guiGateRAM::~guiGateRAM() {}
void guiGateRAM::doParamsDialog(void *, wxCommandProcessor *) {}

// The browser's memory viewer polls the gate rather than being pushed to, so
// there is nothing to notify.
void guiGateRAM::notifyMemoryChanged(bool) {}
