/*****************************************************************************
   Project: CEDAR Logic Simulator
   WinSparkleUpdater: Windows auto-update support via WinSparkle library
*****************************************************************************/

#ifdef _WIN32

#include "WinSparkleUpdater.h"
#include "winsparkle.h"
#include "../version.h"

void WinSparkleUpdater_Initialize() {
    // Set app metadata
    win_sparkle_set_app_details(L"Cedarville University", L"CedarLogic", VERSION_NUMBER_W().c_str());

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
