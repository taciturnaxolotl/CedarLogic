/*****************************************************************************
   Project: CEDAR Logic Simulator

   UpdateInfo: see UpdateInfo.h
*****************************************************************************/

#include "UpdateInfo.h"

#include <cctype>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <wininet.h>
#endif

#ifdef __APPLE__
// Implemented in SparkleUpdater.mm: NSURLSession has no C++ binding worth
// writing by hand, and that file is already Objective-C++.
//
// Declared weak so this translation unit still links in targets that compile it
// without SparkleUpdater.mm -- the logic test suite, which exercises the pure
// parsing below and has no business pulling in Foundation. When the real
// definition is linked it wins over the fallback here.
__attribute__((weak)) std::string cl_update_fetch_appcast_mac(const std::string &url) {
    (void)url;
    return std::string();
}
#endif

namespace cl {
namespace update {

Version parseVersion(const std::string &s) {
    Version v;
    size_t i = 0;
    while (i < s.size() && !isdigit((unsigned char)s[i])) i++;
    if (i == s.size()) return v; // no digits anywhere: not a version

    int part = 0;
    bool sawDigit = false;
    v.valid = true;
    while (i <= s.size()) {
        char c = i < s.size() ? s[i] : '.'; // trailing '.' flushes the last part
        if (isdigit((unsigned char)c)) {
            part = part * 10 + (c - '0');
            sawDigit = true;
        } else if (c == '.' || c == '-' || c == '+') {
            if (sawDigit && v.count < 4) v.parts[v.count++] = part;
            part = 0;
            sawDigit = false;
            if (c != '.') break; // pre-release suffix ends the comparison
        } else {
            break; // anything else (whitespace, 'v') ends it
        }
        i++;
    }
    return v;
}

bool newerThan(const Version &a, const Version &b) {
    for (int i = 0; i < 4; i++) {
        int x = i < a.count ? a.parts[i] : 0;
        int y = i < b.count ? b.parts[i] : 0;
        if (x != y) return x > y;
    }
    return false;
}

bool splitUrl(const std::string &url, std::string &host, std::string &path) {
    host.clear();
    path.clear();
    size_t scheme = url.find("://");
    if (scheme == std::string::npos) return false;
    size_t h = scheme + 3;
    size_t slash = url.find('/', h);
    if (slash == std::string::npos) {
        host = url.substr(h);
        path = "/";
    } else {
        host = url.substr(h, slash - h);
        path = url.substr(slash);
    }
    return !host.empty();
}

// The value of one attribute on one tag, or empty if absent. Written as a scan
// rather than an XML parse on purpose: the feed is ours, it is a few hundred
// bytes, and a crash-recovery path should not carry a parser.
static std::string attrOf(const std::string &tag, const std::string &name) {
    std::string needle = name + "=\"";
    size_t at = tag.find(needle);
    if (at == std::string::npos) return std::string();
    at += needle.size();
    size_t end = tag.find('"', at);
    return end == std::string::npos ? std::string() : tag.substr(at, end - at);
}

// The text of <name>...</name> within a range, or empty if absent.
static std::string elemText(const std::string &xml, size_t from, size_t to,
                            const std::string &name) {
    std::string open = "<" + name + ">", close = "</" + name + ">";
    size_t at = xml.find(open, from);
    if (at == std::string::npos || at >= to) return std::string();
    at += open.size();
    size_t end = xml.find(close, at);
    if (end == std::string::npos || end > to) return std::string();
    return xml.substr(at, end - at);
}

bool appcastLatest(const std::string &xml, const std::string &os, Version &out) {
    bool found = false;
    size_t at = 0;
    while ((at = xml.find("<item", at)) != std::string::npos) {
        size_t itemEnd = xml.find("</item>", at);
        if (itemEnd == std::string::npos) itemEnd = xml.size();
        std::string item = xml.substr(at, itemEnd - at);

        size_t enc = item.find("<enclosure");
        if (enc == std::string::npos) { at = itemEnd; continue; }
        size_t encEnd = item.find('>', enc);
        std::string tag = item.substr(enc, encEnd == std::string::npos
                                                 ? std::string::npos : encEnd - enc + 1);
        at = itemEnd;
        if (attrOf(tag, "sparkle:os") != os) continue;

        // The version is an attribute on <enclosure> in some feeds and a
        // <sparkle:version> element on the <item> in others -- ours is the
        // latter, which is what scripts/update-appcast.sh writes. Accept both,
        // the way WinSparkle's own appcast parser does.
        Version v = parseVersion(attrOf(tag, "sparkle:version"));
        if (!v.valid) v = parseVersion(elemText(item, 0, item.size(), "sparkle:version"));
        if (!v.valid) continue;
        if (!found || newerThan(v, out)) {
            out = v;
            found = true;
        }
    }
    return found;
}

std::string fetchAppcast(const std::string &url) {
    std::string host, path;
    if (!splitUrl(url, host, path)) return std::string();

#ifdef _WIN32
    // WinINet: already loaded by WinSparkle, and the crash path needs the
    // fewest moving parts it can get. One redirect is followed by hand so a
    // moved feed still answers. `url` is const, so the redirect target lands in
    // a local copy.
    std::string target = url;
    for (int hop = 0; hop < 2; hop++) {
        HINTERNET inet = InternetOpenA("CedarLogic", INTERNET_OPEN_TYPE_PRECONFIG,
                                       NULL, NULL, 0);
        if (!inet) return std::string();
        HINTERNET req = InternetOpenUrlA(inet, target.c_str(), NULL, 0,
                                         INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
        if (!req) { InternetCloseHandle(inet); return std::string(); }

        DWORD status = 0, len = sizeof(status);
        HttpQueryInfoA(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                       &status, &len, NULL);
        if (status == 301 || status == 302 || status == 307 || status == 308) {
            char buf[2048];
            DWORD blen = sizeof(buf);
            if (HttpQueryInfoA(req, HTTP_QUERY_LOCATION, buf, &blen, NULL) && blen) {
                target.assign(buf, blen);
                InternetCloseHandle(req);
                InternetCloseHandle(inet);
                continue;
            }
        }
        if (status != 200) {
            InternetCloseHandle(req);
            InternetCloseHandle(inet);
            return std::string();
        }

        std::string body;
        char chunk[8192];
        DWORD got = 0;
        while (body.size() < (1u << 20) &&
               InternetReadFile(req, chunk, sizeof(chunk), &got) && got > 0) {
            body.append(chunk, got);
        }
        InternetCloseHandle(req);
        InternetCloseHandle(inet);
        return body;
    }
    return std::string();
#elif defined(__APPLE__)
    return cl_update_fetch_appcast_mac(url);
#else
    // Linux builds have no auto-updater, so nothing consumes this.
    (void)host;
    (void)path;
    return std::string();
#endif
}

}  // namespace update
}  // namespace cl
