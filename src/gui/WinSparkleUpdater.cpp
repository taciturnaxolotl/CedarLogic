/*****************************************************************************
   Project: CEDAR Logic Simulator
   WinSparkleUpdater: Windows auto-update support via WinSparkle library
*****************************************************************************/

#ifdef _WIN32

#include "WinSparkleUpdater.h"
#include "winsparkle.h"
#include "../version.h"
#include "MainApp.h"
#include "MainFrame.h"
#include "wx/app.h"

DECLARE_APP(MainApp)

namespace {

// WinSparkle asks this before offering to install, and refusing only produces
// its own "CedarLogic cannot be restarted" dialog. Unsaved work is not a reason
// to refuse: the close below asks about it, in the words the rest of the
// program uses, and lets the user keep it.
int __cdecl canShutdown() {
    return 1;
}

// Called once the installer is running, and this is the half that was missing:
// without it WinSparkle starts the installer and the old CedarLogic keeps
// running, so the new one has to be started by hand -- and the installer is
// rewriting the program underneath a copy of it that is still open.
//
// WinSparkle calls this on its own thread, where touching a window is not
// allowed, so the work is handed to the main thread. Closing the frame rather
// than exiting outright keeps the ordinary shutdown: the save prompt, the
// settings write, the updater thread coming down in OnExit.
void __cdecl requestShutdown() {
    wxTheApp->CallAfter([]() {
        MainFrame *frame = wxGetApp().mainframe;
        if (frame != NULL) {
            frame->Close(false);
        } else {
            wxTheApp->ExitMainLoop();
        }
    });
}

}  // namespace

void WinSparkleUpdater_Initialize() {
    // Set app metadata
    win_sparkle_set_app_details(L"Cedarville University", L"CedarLogic", VERSION_NUMBER_W().c_str());

    // Deliberately no win_sparkle_set_config_methods: it takes all three of
    // read/write/delete or none, and calls whichever it is handed unchecked.
    // Policy is enforced by checksDisabled() before we ever get here.

    // Quitting for an update is the application's job; WinSparkle only asks.
    // With nothing registered its request goes nowhere, which is why an update
    // used to install around a running copy of the program it was replacing.
    win_sparkle_set_can_shutdown_callback(canShutdown);
    win_sparkle_set_shutdown_request_callback(requestShutdown);

    // Set the appcast URL
    win_sparkle_set_appcast_url("https://taciturnaxolotl.github.io/CedarLogic/appcast.xml");

    // Initialize WinSparkle (starts background update checks)
    win_sparkle_init();
}

void WinSparkleUpdater_CheckForUpdates() {
    win_sparkle_check_update_with_ui();
}

void WinSparkleUpdater_Cleanup() {
    win_sparkle_cleanup();
}

#endif // _WIN32
