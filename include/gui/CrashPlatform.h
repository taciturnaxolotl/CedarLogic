/*****************************************************************************
   Project: CEDAR Logic Simulator

   CrashPlatform: the seam between CrashTrace and the per-platform handler,
   CrashTraceWin.cpp or CrashTracePosix.cpp. Neither side needs wxWidgets.
*****************************************************************************/

#pragma once

#include <string>

namespace cl {
namespace crash {

// Catch fatal crashes process-wide: write `header` and a description to
// `logPath`, then hand the crash to whoever held the hook before.
void installPlatformHandler(const std::string &logPath, const std::string &header);

}  // namespace crash
}  // namespace cl
