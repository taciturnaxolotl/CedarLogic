/*****************************************************************************
   Project: CEDAR Logic Simulator

   CrashReporter: sending a crash somewhere it can be read.
*****************************************************************************/

#include "CrashReporter.h"

#include "AdminPolicy.h"
#include "../version.h"
#include "wx/filename.h"
#include "wx/stdpaths.h"

#ifdef CEDARLOGIC_SENTRY_DSN
#include <sentry.h>
#endif

namespace cl {
namespace crash {

#ifdef CEDARLOGIC_SENTRY_DSN

namespace {

bool g_started = false;

// The one off switch. There is no per-user setting, so this is what a lab or a
// department uses, and it has to be read before anything is sent rather than
// consulted later -- a report already on its way cannot be recalled.
const char *const kPolicyValue = "DisableCrashReporting";

// The handler is a second program, and has to be: the whole point is that it is
// still alive and uninvolved when this one is not. It is installed beside the
// executable, which inside a macOS bundle means Contents/MacOS.
wxFileName handlerPath() {
    wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
    wxFileName handler(exe.GetPath(), "crashpad_handler");
#ifdef _WIN32
    handler.SetExt("exe");
#endif
    return handler;
}

// Somewhere writable that survives a restart, for the consent answer and for
// anything captured while offline. Deliberately not the install directory:
// that is read-only for a normal user on Windows, and the library treats this
// as its own and will delete things inside it.
wxFileName databasePath() {
    wxFileName db(wxStandardPaths::Get().GetUserLocalDataDir(), "");
    db.AppendDir("crashdb");
    return db;
}

}  // namespace

bool reportingBuilt() { return true; }

void startReporter() {
    if (g_started) return;
    if (cl::policy::disabledBy(kPolicyValue)) return;

    const wxFileName handler = handlerPath();
    // Without the handler there is nothing to catch a crash, and starting
    // anyway would claim to be reporting while silently doing nothing.
    if (!handler.FileExists()) return;

    wxFileName db = databasePath();
    db.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    sentry_options_t *options = sentry_options_new();
    sentry_options_set_dsn(options, CEDARLOGIC_SENTRY_DSN);

    // Matches the release name the symbols are uploaded under, which is what
    // ties a report back to the build that produced it.
    const std::string release = "cedarlogic@" + VERSION_NUMBER();
    sentry_options_set_release(options, release.c_str());

    // Paths go through the wide-character setters on Windows: an install
    // directory or a user name containing non-ASCII is ordinary, and the narrow
    // setters would mangle it into a path that does not exist.
#ifdef _WIN32
    sentry_options_set_handler_pathw(options, handler.GetFullPath().wc_str());
    sentry_options_set_database_pathw(options, db.GetPath().wc_str());
#else
    sentry_options_set_handler_path(options, handler.GetFullPath().utf8_str());
    sentry_options_set_database_path(options, db.GetPath().utf8_str());
#endif

    g_started = sentry_init(options) == 0;
}

void stopReporter() {
    if (!g_started) return;
    g_started = false;
    sentry_close();
}

std::string statusLine() {
    cl::policy::Reading r;
    if (cl::policy::disabledBy(kPolicyValue, &r)) {
        return std::string("Crash reporting: DISABLED by administrator policy\n") +
               "  Policy  " + cl::policy::keyPath() + "\n" +
               "          " + kPolicyValue + " = " + r.data +
               " (" + r.type + ", found in the " + r.view + " view)";
    }
    if (!g_started) {
        return "Crash reporting: INACTIVE (the reporting helper was not found "
               "beside the program)";
    }
    return "Crash reporting: ENABLED";
}

#else  // no DSN configured: this build reports nowhere.

bool reportingBuilt() { return false; }
void startReporter() {}
void stopReporter() {}

std::string statusLine() {
    return "Crash reporting: NOT BUILT IN (this build has no reporting address)";
}

#endif

}  // namespace crash
}  // namespace cl
