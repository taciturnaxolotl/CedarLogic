/*****************************************************************************
   Project: CEDAR Logic Simulator

   CrashReportDialog: the next-launch offer to update or report a crash.
*****************************************************************************/

#include "CrashReportDialog.h"

#include "../version.h"
#include "CrashIssue.h"
#include "CrashTrace.h"
#include "wx/button.h"
#include "wx/clipbrd.h"
#include "wx/dataobj.h"
#include "wx/font.h"
#include "wx/sizer.h"
#include "wx/stattext.h"
#include "wx/textctrl.h"
#include "wx/utils.h"

#include <cstdio>
#include <thread>

namespace cl {
namespace crash {

static const int kUpdate = 100;

CrashReportDialog::CrashReportDialog(wxWindow *parent, bool duringStartup,
                                     const std::string &trace)
    : wxDialog(parent, wxID_ANY,
        duringStartup ? "CedarLogic did not start" : "Report a crash",
        wxDefaultPosition, wxSize(700, 520),
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      trace(trace),
      report(crashIssueBody(trace)),
      feed(std::make_shared<FeedResult>()) {
    wxBoxSizer *root = new wxBoxSizer(wxVERTICAL);
    root->Add(new wxStaticText(this, wxID_ANY,
        duringStartup
            ? "CedarLogic closed before its window appeared the last time it ran. "
              "If a newer version fixes this, installing it is the quickest way back."
            : "CedarLogic closed unexpectedly the last time it ran. Reporting this "
              "helps get it fixed.\nOpen a GitHub issue (the full report is copied to "
              "your clipboard so you can paste it in)."),
        0, wxALL, 12);

    // The update offer, filled in once the feed answers.
    updateLine = new wxStaticText(this, wxID_ANY, "Checking for a newer version...");
    wxFont bold = updateLine->GetFont();
    bold.SetWeight(wxFONTWEIGHT_BOLD);
    updateLine->SetFont(bold);
    root->Add(updateLine, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    wxTextCtrl *txt = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(trace),
        wxDefaultPosition, wxDefaultSize,
        wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    txt->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE)));
    root->Add(txt, 1, wxEXPAND | wxLEFT | wxRIGHT, 12);

    wxBoxSizer *btns = new wxBoxSizer(wxHORIZONTAL);
    updateBtn = new wxButton(this, wxID_ANY, "Update now");
    wxButton *copyBtn = new wxButton(this, wxID_ANY, "Copy to clipboard");
    wxButton *issueBtn = new wxButton(this, wxID_ANY, "Open GitHub issue");
    // Labelled by what it does rather than "Dismiss": during a startup crash the
    // user is choosing to retry the work that just failed, and that should read
    // as a choice, not as closing a window.
    wxButton *closeBtn = new wxButton(this, wxID_CANCEL,
        duringStartup ? "Continue anyway" : "Dismiss");
    btns->Add(updateBtn, 0, wxALL, 6);
    btns->Add(copyBtn, 0, wxALL, 6);
    btns->AddStretchSpacer();
    btns->Add(issueBtn, 0, wxALL, 6);
    btns->Add(closeBtn, 0, wxALL, 6);
    root->Add(btns, 0, wxEXPAND | wxALL, 6);
    SetSizer(root);

    updateBtn->Enable(false); // nothing to update to until the feed says so

    // Where an administrator has disabled update checking, say so and make no
    // request at all. This dialog reads the feed itself rather than going
    // through the updater, so it is a second way out of the machine and has to
    // honour the policy on its own account.
    const bool managed = cl::update::checksDisabled();

    if (managed) {
        updateLine->SetLabel("Updates are managed by your administrator. "
                             "Reporting this is the best way to get it fixed.");
        updateBtn->Hide();
    } else {
        static const char *kAppcastUrl =
            "https://taciturnaxolotl.github.io/CedarLogic/appcast.xml";
        auto feed = this->feed;
        std::thread([feed]() {            std::string xml = cl::update::fetchAppcast(kAppcastUrl);
#ifdef _WIN32
            feed->ok.store(!xml.empty() &&
                           cl::update::appcastLatest(xml, "windows", feed->newest));
#elif defined(__APPLE__)
            feed->ok.store(!xml.empty() &&
                           cl::update::appcastLatest(xml, "macos", feed->newest));
#else
            feed->ok.store(false);
#endif
            feed->done.store(true);
        }).detach();
    }
    Layout();

    copyBtn->Bind(wxEVT_BUTTON, &CrashReportDialog::onCopy, this);
    issueBtn->Bind(wxEVT_BUTTON, &CrashReportDialog::onIssue, this);
    updateBtn->Bind(wxEVT_BUTTON, &CrashReportDialog::onUpdate, this);

    // Polled on the event loop: sleeping for the feed would freeze the dialog.
    feedPoll.SetOwner(this);
    if (!managed) {
        current = cl::update::parseVersion(VERSION_NUMBER());
        Bind(wxEVT_TIMER, &CrashReportDialog::onFeedPoll, this);
        feedPoll.Start(100);
    }
}

void CrashReportDialog::copyReport() {
    if (wxTheClipboard->Open()) {
        wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(report)));
        wxTheClipboard->Close();
    }
}

void CrashReportDialog::onCopy(wxCommandEvent &) {
    copyReport();
}

void CrashReportDialog::onIssue(wxCommandEvent &) {
    copyReport();
    wxLaunchDefaultBrowser(wxString::FromUTF8(crashIssueUrl(trace)));
}

void CrashReportDialog::onUpdate(wxCommandEvent &) {
    EndModal(kUpdate);
}

void CrashReportDialog::onFeedPoll(wxTimerEvent &) {
    elapsedMs += 100;
    const bool timedOut = elapsedMs >= 10000;
    if (!feed->done.load() && !timedOut) return;
    feedPoll.Stop();

    // `done` was stored after `newest`, so it publishes `newest` too.
    if (!feed->done.load() || !feed->ok.load()) {
        updateLine->SetLabel("Could not check for updates. Reporting "
                             "this is the best way to get it fixed.");
    } else if (cl::update::newerThan(feed->newest, current)) {
        wxString v;
        v.Printf("Version %d.%d.%d is available and may already fix this.",
                 feed->newest.parts[0], feed->newest.parts[1],
                 feed->newest.parts[2]);
        updateLine->SetLabel(v);
        updateBtn->Enable(true);
        updateBtn->SetDefault();
    } else {
        updateLine->SetLabel("You are on the latest version, so this is "
                             "worth reporting.");
    }
    Layout();
}

bool showPendingCrashReport(wxWindow *parent, bool duringStartup) {
    const std::string &logPath = cl::crash::logPath();
    std::string trace = readFile(logPath);
    if (trace.empty()) return false;

    CrashReportDialog dlg(parent, duringStartup, trace);
    const bool choseUpdate = dlg.ShowModal() == kUpdate;
    remove(logPath.c_str()); // only prompt once per crash
    return choseUpdate;
}

}  // namespace crash
}  // namespace cl
