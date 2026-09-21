/*****************************************************************************
   Project: CEDAR Logic Simulator

   CrashIssue: turning a crash trace into a GitHub issue.

   No wx and no platform headers, so it can be tested without a display.
*****************************************************************************/

#pragma once

#include <string>

namespace cl {
namespace crash {

std::string readFile(const std::string &path);

// Percent-encode everything outside the RFC 3986 unreserved set so a trace can
// ride safely in a GitHub issue URL's query string.
std::string urlEncode(const std::string &s);

// First line of a trace is the version/OS header; the stack follows it.
std::string crashTraceBody(const std::string &trace);
std::string crashVersionLine(const std::string &trace);

// The filled-in markdown template; also what goes on the clipboard.
std::string crashIssueBody(const std::string &trace);

// Same body, capped and percent-encoded into a prefilled "new issue" link.
std::string crashIssueUrl(const std::string &trace);

}  // namespace crash
}  // namespace cl
