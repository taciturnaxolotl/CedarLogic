/*****************************************************************************
   Project: CEDAR Logic Simulator

   CrashReporter: sending a crash somewhere it can be read.

   Separate from CrashTrace, which writes a local file for the user to see and
   hand over. This is the automatic path, and it exists mostly for macOS and
   Linux: a signal handler may not walk its own stack safely, so those platforms
   record a signal name and nothing else. A separate handler process has no such
   restriction, because it is not the process that is dying.

   Nothing is asked of the user. A crash is reported the way the updater checks
   for a new version: automatically, because a bug nobody reports is a bug
   nobody fixes, and the report says where the program died and not what the
   user was building. An administrator can turn it off for a whole deployment
   through the same policy key that turns off update checks; there is no
   per-user switch, for the same reason there is not one for updates.

   The report carries a stack, a version and an operating system. It does not
   carry the circuit, the file, or anything the user typed.
*****************************************************************************/

#pragma once

#include <string>

namespace cl {
namespace crash {

// False when this build has no reporting address compiled in, which is every
// build except an official release: a fork, a contributor's checkout and a
// local build all report nowhere. Everything below is a no-op in that case.
bool reportingBuilt();

// Bring the reporter up. Reports automatically from then on, unless an
// administrator has turned it off for this machine.
void startReporter();

// Flush whatever is queued and shut down. Called once on the way out.
void stopReporter();

// One line saying what reporting is doing and why, for the command-line status
// flag an administrator uses to confirm a deployment behaves as intended.
// A policy that silently does nothing is worse than no policy.
std::string statusLine();

}  // namespace crash
}  // namespace cl
