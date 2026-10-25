/*****************************************************************************
   Project: CEDAR Logic Simulator

   CrashTrace: writing a stack trace when the app dies.

   Writes the file and nothing else; the next launch offers it. Paths are
   computed at startup so the handler itself allocates nothing.
*****************************************************************************/

#pragma once

#include <string>

namespace cl {
namespace crash {

// Compute the report path (needs wx) and install the platform crash handler.
// Called once at startup, on the main thread.
void installCrashHandler();

// Where installCrashHandler() decided the trace goes. Empty until then.
const std::string &logPath();

// Lets the crash handler run after this thread overflows its stack. One thread
// per call; installCrashHandler() covers the thread that calls it.
void giveThisThreadASignalStack();

}  // namespace crash
}  // namespace cl
