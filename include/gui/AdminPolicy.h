/*****************************************************************************
   Project: CEDAR Logic Simulator

   AdminPolicy: what an administrator has turned off for this machine.

   For deployments where the organisation owns the installed copy -- campus
   software distribution, managed labs -- and a user must not be able to undo
   the decision. The values live under SOFTWARE\Policies, which is writable only
   by administrators and is the tree Intune and Group Policy target.

   A policy here can only ever switch something off. Nothing in this file can
   turn a feature on that the program would not otherwise do, so a malformed or
   hostile value fails safe in the direction of the program's own default.

   Other platforms have no equivalent and report nothing as disabled; macOS
   deployments manage the updater through its own configuration profile.
*****************************************************************************/

#pragma once

#include <string>

namespace cl {
namespace policy {

// What was read, for the status flags that let an administrator tell a working
// policy from a typo. Both look identical from outside -- a program that simply
// does not do the thing -- which is how a policy comes to be wrong for months.
struct Reading {
    bool found = false;   // the value exists somewhere we looked
    bool disabled = false;  // ...and says yes
    std::string view;     // which registry view it was found in
    std::string type;     // REG_DWORD or REG_SZ
    std::string data;     // what it held, as text
};

// True when `valueName` under the policy key says this feature is off.
//
// Accepts a REG_DWORD or a REG_SZ: writing the string "1" where a number was
// meant is an easy slip, and silently ignoring it is the kind of failure an
// administrator only discovers months later. Anything non-zero, or the text
// "1"/"true"/"yes"/"on", disables. Anything else counts as "no" rather than as
// unreadable.
//
// Reads the 64-bit view first, since that is the plain path an administrator
// writes, then the 32-bit view, for a policy set from a 32-bit tool. A 32-bit
// program is redirected to WOW6432Node unless it asks otherwise, which is
// exactly the trap this exists to avoid.
bool disabledBy(const char *valueName, Reading *detail = nullptr);

// Where the policies live, for printing in a status message.
const char *keyPath();

}  // namespace policy
}  // namespace cl
