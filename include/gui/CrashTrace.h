/*****************************************************************************
   Project: CEDAR Logic Simulator

   CrashTrace: writing a stack trace when the app dies.

   A trace is written when the app crashes, then offered for reporting: on
   Windows right away (native MessageBox) and, on every platform, via a wx
   dialog the next time the app starts. The report path and header are computed
   once at startup so the crash handler itself stays allocation-light.
*****************************************************************************/

#pragma once

#include <string>

namespace cl {
namespace crash {

// Compute the report path (needs wx) and install the platform crash handler.
// Called once at startup.
void installCrashHandler();

// Where installCrashHandler() decided the trace goes. Empty until then.
const std::string &logPath();

}  // namespace crash
}  // namespace cl
