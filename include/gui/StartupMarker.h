/*****************************************************************************
   Project: CEDAR Logic Simulator

   StartupMarker: telling a startup crash from a later one.

   Armed as the app's first act, disarmed once a window exists; surviving to the
   next launch means the last run never got that far. Counts attempts so one
   failure can be a fluke. Not RAII on purpose: a destructor runs on none of the
   deaths this detects, and would run on the one path that must keep the marker.
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
