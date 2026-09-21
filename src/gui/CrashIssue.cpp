/*****************************************************************************
   Project: CEDAR Logic Simulator

   CrashIssue: turning a crash trace into a GitHub issue.
*****************************************************************************/

#include "CrashIssue.h"

#include <cctype>
#include <cstdio>

namespace cl {
namespace crash {

std::string readFile(const std::string &path) {
    std::string out;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return out;
    char buf[4096];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, got);
    fclose(f);
    return out;
}

// Percent-encode everything outside the RFC 3986 unreserved set so a trace can
// ride safely in a GitHub issue URL's query string.
std::string urlEncode(const std::string &s) {
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 0xF]; }
    }
    return out;
}

// The trace's first line is the version/OS header; the exception and stack
// follow it. Split them so each lands under its own heading in the issue body.
std::string crashTraceBody(const std::string &trace) {
    size_t nl = trace.find('\n');
    if (nl == std::string::npos) return trace;
    std::string rest = trace.substr(nl + 1);
    size_t start = rest.find_first_not_of('\n');
    return start == std::string::npos ? std::string() : rest.substr(start);
}

std::string crashVersionLine(const std::string &trace) {
    return trace.substr(0, trace.find('\n'));
}

// The filled-in issue template. Kept as the whole markdown body (headings +
// guiding comments) so it is exactly what we put on the clipboard: if the
// prefilled URL is truncated, the reporter can select-all and paste this.
std::string crashIssueBody(const std::string &trace) {
    return
        "### Steps to Reproduce\n\n"
        "<!-- Describe what you were doing when it crashed -->\n\n\n\n"
        "### Crash trace\n"
        "<!-- This template was also copied to your clipboard if the trace appears cut off -->\n"
        "<!-- Ctrl+A (or Cmd+A if you are on Mac) and then paste the full trace -->\n\n"
        "```\n" + crashTraceBody(trace) + "\n```\n\n"
        "### Version\n\n" + crashVersionLine(trace) + "\n";
}

// Prefilled "new issue" URL. The full body always goes on the clipboard too;
// the copy in the URL is capped so it stays within what browsers/GitHub accept.
std::string crashIssueUrl(const std::string &trace) {
    std::string body = crashIssueBody(trace);
    std::string firstFrame = crashTraceBody(trace);
    std::string title = "Crash: " + firstFrame.substr(0, firstFrame.find('\n'));
    if (body.size() > 6000) body.resize(6000);
    return "https://github.com/taciturnaxolotl/CedarLogic/issues/new?title=" +
           urlEncode(title) + "&body=" + urlEncode(body);
}

}  // namespace crash
}  // namespace cl
