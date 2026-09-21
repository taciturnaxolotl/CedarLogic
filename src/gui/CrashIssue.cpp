/*****************************************************************************
   Project: CEDAR Logic Simulator

   CrashIssue: turning a crash trace into a GitHub issue.
*****************************************************************************/

#include "CrashIssue.h"

#include <cctype>
#include <cstdio>

namespace cl {
namespace crash {

// What the prefilled link may carry, after encoding. The rest is on the
// clipboard, which the issue template tells the reporter to paste.
static const size_t kUrlTitleBudget = 200;
static const size_t kUrlBodyBudget = 6000;

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
// A trace that is only a header has no stack, so the body is empty rather than
// the header over again.
std::string crashTraceBody(const std::string &trace) {
    size_t nl = trace.find('\n');
    if (nl == std::string::npos) return std::string();
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

// Bytes in the UTF-8 character starting at `i`, or 1 for anything malformed so
// the walk always advances.
static size_t charLen(const std::string &s, size_t i) {
    const unsigned char c = s[i];
    const size_t n = (c < 0x80) ? 1 : (c >> 5) == 0x6 ? 2
                   : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
    return (i + n <= s.size()) ? n : 1;
}

// Encode until the budget is spent, a whole character at a time. Capping the
// source instead would miss by 3x, since one byte encodes to three.
static std::string encodeCapped(const std::string &s, size_t budget) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        const size_t n = charLen(s, i);
        const std::string piece = urlEncode(s.substr(i, n));
        if (out.size() + piece.size() > budget) break;
        out += piece;
        i += n;
    }
    return out;
}

// Prefilled "new issue" URL. The body always goes on the clipboard in full.
std::string crashIssueUrl(const std::string &trace) {
    const std::string body = crashTraceBody(trace);
    const std::string first = body.substr(0, body.find('\n'));
    const std::string title = first.empty() ? "Crash" : "Crash: " + first;
    return "https://github.com/taciturnaxolotl/CedarLogic/issues/new?title=" +
           encodeCapped(title, kUrlTitleBudget) + "&body=" +
           encodeCapped(crashIssueBody(trace), kUrlBodyBudget);
}

}  // namespace crash
}  // namespace cl
