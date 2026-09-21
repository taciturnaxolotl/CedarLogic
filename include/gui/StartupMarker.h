/*****************************************************************************
   Project: CEDAR Logic Simulator

   StartupMarker: telling a startup crash from a later one.

   A crash log alone only says the last run died somewhere. To tell a startup
   crash from one that happened later, the app writes a marker as its first act
   and deletes it once the main window exists. If the marker survives to the next
   launch, the previous run never got that far.

   The file holds an attempt counter, so a first failure can be treated as
   possibly-a-fluke and only repeated failures trigger recovery.

   Deliberately not RAII. The marker exists to survive abnormal termination, and
   a destructor runs in none of the cases it is there to detect -- not on a
   crash, not on std::_Exit -- while it would run on the "Update now" path, the
   one place the marker has to be left behind.
*****************************************************************************/

#pragma once

#include <string>

namespace cl {

// Startup crashes before this count are treated as ordinary crashes; at or above
// it the app offers an update before retrying the work that keeps failing.
const int kStartupCrashRecoveryThreshold = 2;

class StartupMarker {
public:
    explicit StartupMarker(const std::string &tempDir);

    // How many consecutive launches have died during startup, including the one
    // in progress. Zero means the last run reached a main window.
    int consecutiveFailures() const;

    void arm(int attempt);
    void disarm() const;

private:
    std::string path;
};

}  // namespace cl
