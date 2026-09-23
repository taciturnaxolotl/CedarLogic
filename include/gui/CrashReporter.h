/*****************************************************************************
   Project: CEDAR Logic Simulator

   CrashReporter: sending a crash somewhere it can be read.

   Separate from CrashTrace, which writes a local file for the user to see and
   hand over. This is the automatic path, and it exists mostly for macOS and
   Linux: a signal handler may not walk its own stack safely, so those platforms
   record a signal name and nothing else. A separate handler process has no such
   restriction, because it is not the process that is dying.

   Nothing is asked of the user. A crash is reported the way the updater checks
   for a new version: automatically. An administrator can turn it off for a
   whole deployment through the same policy key that turns off update checks.

   The report carries a stack, a version and an operating system. It does not
   carry the circuit, the file, or anything the user typed.
*****************************************************************************/

#pragma once

#include <string>

namespace cl {
namespace crash {

// Bring the reporter up. Reports automatically from then on, unless an
// administrator has turned it off for this machine.
void startReporter();

// Flush whatever is queued and shut down. Called once on the way out.
void stopReporter();

// What reporting is doing and why. Facts only: the wording lives with the
// command-line flag that prints it, the way describeUpdatePolicy() and
// `--update-status` already divide the same job.
struct Status {
    bool active = false;   // a crash right now would be reported
    bool built = false;    // reporting was compiled in at all
    bool disabledByPolicy = false;

    bool policyFound = false;  // the policy value exists somewhere
    std::string policyView;    // which registry view it came from
    std::string policyType;    // "REG_DWORD" or "REG_SZ", as actually stored
    std::string policyData;    // its value, rendered for a human

    // Where the policy lives, for naming it in a message. Empty on platforms
    // with no policy mechanism.
    std::string policyKey;
};

Status status();

}  // namespace crash
}  // namespace cl
