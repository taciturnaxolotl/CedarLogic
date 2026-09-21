/*****************************************************************************
   Project: CEDAR Logic Simulator
   WinSparkleUpdater: Windows auto-update support via WinSparkle library
*****************************************************************************/

#ifdef _WIN32

#include "WinSparkleUpdater.h"
#include "winsparkle.h"
#include "UpdateInfo.h"
#include "../version.h"

#include <cstring>
#include <string>

namespace {

// WinSparkle reads its settings with no registry view flag, so from this
// 32-bit program every read lands in SOFTWARE\WOW6432Node. An administrator
// who sets a machine-wide default with the usual 64-bit tools writes the plain
// path, which WinSparkle then never sees: no error, no warning, the setting
// simply does nothing. Overriding the read lets us look in both views.
//
// Only config_read is overridden. WinSparkle documents a null pointer as "use
// the default for that action", so writes and deletes still go exactly where
// they always did. Settings a user already has keep working, and there is
// nothing to migrate.
int __cdecl configRead(const char *name, wchar_t *buf, size_t len, void *) {
    std::wstring value;
    // 1/0 rather than TRUE/FALSE: winsparkle.h pulls in only <stddef.h> and
    // <time.h>, so the Windows macros are not necessarily in scope here.
    if (!cl::update::readWinSparkleSetting(name, value)) return 0;
    // `len` counts wchar_t, matching WinSparkle's own reader.
    if (value.size() + 1 > len) return 0;
    std::memcpy(buf, value.c_str(), (value.size() + 1) * sizeof(wchar_t));
    return 1;
}

}  // namespace

void WinSparkleUpdater_Initialize() {
    // Set app metadata
    win_sparkle_set_app_details(L"Cedarville University", L"CedarLogic", VERSION_NUMBER_W().c_str());

    // Installed before init, so even the first setting read goes through it.
    static win_sparkle_config_methods_t configMethods = {};
    configMethods.config_read = &configRead;
    win_sparkle_set_config_methods(&configMethods);

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
