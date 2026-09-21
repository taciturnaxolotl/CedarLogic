/*****************************************************************************
   Project: CEDAR Logic Simulator

   UpdateInfo: what the appcast says is newer than what is installed.

   The crash dialog wants to tell the user a fix already exists before asking
   them to file a report ("you are on 3.1.1, 3.1.2 is available, try that
   first"). That means reading the appcast ourselves rather than letting the
   updater own the question, because the updater's UI only appears when it
   decides to appear and cannot be embedded in our own dialog.

   Everything here except FetchAppcastLatest is pure: no wx, no sockets, no
   platform headers. That is deliberate, so the parsing and the version
   comparison can be unit tested from logic/tests without a display.
*****************************************************************************/

#pragma once

#include <string>

namespace cl {
namespace update {

// A dotted version, component by component. Shorter vectors compare as if the
// missing components were zero, so "3.1" and "3.1.0" are equal.
struct Version {
    int parts[4] = {0, 0, 0, 0};
    int count = 0;
    bool valid = false;
};

// Parse "3.1.2" style text. Junk components become zero; a string with no
// leading digits is invalid rather than silently 0.0.0.
Version parseVersion(const std::string &s);

// True iff a is strictly newer than b.
bool newerThan(const Version &a, const Version &b);

// Split an absolute URL into the host and the path-and-query. Returns false if
// the URL has no host, so a caller cannot fetch from an empty address.
bool splitUrl(const std::string &url, std::string &host, std::string &path);

// Pick the newest release from an appcast's XML for one platform. "macos" or
// "windows". Returns false if the feed holds no item for that platform, which a
// caller must treat as "unknown", never as "up to date".
bool appcastLatest(const std::string &xml, const std::string &os, Version &out);

// The body of the appcast feed, empty on any failure. Blocking, with its own
// timeout, so it belongs on a worker thread.
std::string fetchAppcast(const std::string &url);

// True when an administrator has turned update checking off for this machine,
// for deployments where the organisation owns the installed version (campus
// software distribution, managed labs).
//
// Windows reads "DisableUpdateChecks" under
//   HKLM\SOFTWARE\Policies\Cedarville University\CedarLogic
// as either a REG_DWORD or a REG_SZ: writing the string "1" where a number was
// meant is an easy slip, and silently ignoring it is the kind of failure an
// administrator only discovers months later. Anything non-zero, or the text
// "1"/"true"/"yes", disables checking. The key lives under SOFTWARE\Policies
// because that tree is writable only by administrators and is what Intune and
// Group Policy target, so a user cannot turn checking back on. Nothing here
// ever enables checking: the policy can only switch it off.
//
// Other platforms always return false; macOS deployments manage Sparkle through
// its own configuration profile instead.
bool checksDisabled();

// What checksDisabled() found and where, for `--update-status`. An
// administrator otherwise has no way to tell a working policy from a typo,
// since both look like an application that simply does not check for updates.
struct PolicyStatus {
    bool disabled = false;      // the decision checksDisabled() returns

    bool policyFound = false;   // the policy value exists somewhere
    std::string policyView;     // which registry view it came from
    std::string policyType;     // "REG_DWORD" or "REG_SZ", as actually stored
    std::string policyData;     // its value, rendered for a human

    // WinSparkle keeps its own "CheckForUpdates" setting, which reads like the
    // way to turn updates off and is not: the user's own copy in HKCU
    // overrides it. Reported when present so that reaching for the wrong lever
    // shows up as something other than silence. Never affects `disabled`.
    bool sparkleFound = false;
    std::string sparkleWhere;
    std::string sparkleValue;

    std::string platformNote;   // set where no policy mechanism exists
};

PolicyStatus describeUpdatePolicy();

#ifdef _WIN32
// Reads one WinSparkle setting, looking in both registry views rather than
// whichever one the process happens to get. Installed as WinSparkle's
// config_read so that a machine-wide value an administrator wrote with 64-bit
// tools is actually seen by this 32-bit program. Returns false when absent.
bool readWinSparkleSetting(const char *name, std::wstring &out,
                           std::string *whereFound = nullptr);
#endif

}  // namespace update
}  // namespace cl
