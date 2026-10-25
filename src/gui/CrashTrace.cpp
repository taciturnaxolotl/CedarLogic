/*****************************************************************************
   Project: CEDAR Logic Simulator

   CrashTrace: writing a stack trace when the app dies. This file decides where
   the trace goes and what heads it; CrashTraceWin.cpp and CrashTracePosix.cpp
   catch the crash.
*****************************************************************************/

#include "CrashTrace.h"

#include "CrashPlatform.h"
#include "../version.h"
#include "wx/filename.h"
#include "wx/stdpaths.h"
#include "wx/utils.h"

#include <string>

namespace cl {
namespace crash {

static std::string g_crashLogPath;

const std::string &logPath() {
    return g_crashLogPath;
}

// Runs after the crash reporter starts: the last to take the crash hook gets it,
// and only this handler passes the crash on.
void installCrashHandler() {
    wxFileName fn(wxStandardPaths::Get().GetTempDir(), "CedarLogic_crashtrace.log");
    g_crashLogPath = fn.GetFullPath().ToStdString();
    const std::string header = "CedarLogic " + VERSION_NUMBER() + " (" +
                               std::to_string((int)(sizeof(void *) * 8)) + "-bit) on " +
                               wxGetOsDescription().ToStdString() + "\n\n";
    installPlatformHandler(g_crashLogPath, header);
}

}  // namespace crash
}  // namespace cl
