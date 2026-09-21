/*****************************************************************************
   Project: CEDAR Logic Simulator

   CrashReportDialog: the next-launch offer to update or report a crash.
*****************************************************************************/

#pragma once

#include "UpdateInfo.h"
#include "wx/dialog.h"
#include "wx/timer.h"

#include <atomic>
#include <memory>
#include <string>

class wxButton;
class wxStaticText;

namespace cl {
namespace crash {

class CrashReportDialog : public wxDialog {
public:
    CrashReportDialog(wxWindow *parent, bool duringStartup,
                      const std::string &trace);

private:
    // shared_ptr because a stalled fetch may outlive the dialog.
    struct FeedResult {
        std::atomic<bool> done{false};
        std::atomic<bool> ok{false};
        cl::update::Version newest;  // published by `done`
    };

    void copyReport();
    void onCopy(wxCommandEvent &);
    void onIssue(wxCommandEvent &);
    void onUpdate(wxCommandEvent &);
    void onFeedPoll(wxTimerEvent &);

    std::string trace;
    std::string report;
    std::shared_ptr<FeedResult> feed;
    cl::update::Version current;
    wxTimer feedPoll;
    int elapsedMs = 0;
    wxStaticText *updateLine = nullptr;
    wxButton *updateBtn = nullptr;
};

// Shown on the next launch after a crash. The only place the user is told about
// it: the handler that writes the trace cannot be trusted to put up a window.
//
// It leads with an update check rather than a bug report, because a fix that
// already ships beats a report that has not been written yet -- and it deflects
// duplicate reports for bugs that are already closed. The appcast is read here
// instead of taken from the updater, which cannot be asked "what is newer than
// me" without showing its own window, and because a version cached from the last
// healthy run would by definition predate the fix.
//
// duringStartup says the previous run died before its main window appeared.
// Those are the crashes the user cannot report and the updater cannot reach, so
// the wording says the app never started rather than merely misbehaved.
//
// Returns true if the user chose to update. Only the startup caller acts on it.
bool showPendingCrashReport(wxWindow *parent, bool duringStartup);

}  // namespace crash
}  // namespace cl
