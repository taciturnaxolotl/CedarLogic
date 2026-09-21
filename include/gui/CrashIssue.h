/*****************************************************************************
   Project: CEDAR Logic Simulator

   CrashIssue: turning a crash trace into a GitHub issue.

   Nothing here touches wx, a window, or a platform header, so the encoding and
   the issue template can be unit tested from a machine with no display -- the
   same split UpdateInfo.h makes for version parsing.
*****************************************************************************/

#pragma once

#include <string>

namespace cl {
namespace crash {

std::string readFile(const std::string &path);

// Percent-encode everything outside the RFC 3986 unreserved set so a trace can
// ride safely in a GitHub issue URL's query string.
std::string urlEncode(const std::string &s);

// The trace's first line is the version/OS header; the exception and stack
// follow it. Split them so each lands under its own heading in the issue body.
std::string crashTraceBody(const std::string &trace);

std::string crashVersionLine(const std::string &trace);

// The filled-in issue template. Kept as the whole markdown body (headings +
// guiding comments) so it is exactly what we put on the clipboard: if the
// prefilled URL is truncated, the reporter can select-all and paste this.
std::string crashIssueBody(const std::string &trace);

// Prefilled "new issue" URL. The full body always goes on the clipboard too;
// the copy in the URL is capped so it stays within what browsers/GitHub accept.
std::string crashIssueUrl(const std::string &trace);

}  // namespace crash
}  // namespace cl
