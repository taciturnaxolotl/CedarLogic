/*****************************************************************************
   Project: CEDAR Logic Simulator

   UpdateInfo: see UpdateInfo.h
*****************************************************************************/

#include "UpdateInfo.h"

#include <cctype>
#include <cstdlib>
#include <cwchar>
#include <string>

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

#ifdef _WIN32
// One registry read from an explicitly named view, so a caller can ask for the
// 64-bit tree rather than accepting whatever the redirector hands a 32-bit
// process. Accepts REG_DWORD or REG_SZ: administrators write both, and
// rejecting one of them leaves a policy that silently does nothing.
//
// `typeOut` and `dataOut` report what was actually stored, for --update-status.
static bool readPolicyFlag(REGSAM view, const wchar_t *subkey,
                           const wchar_t *value, bool &out,
                           std::string *typeOut = nullptr,
                           std::string *dataOut = nullptr) {
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, KEY_READ | view, &key) !=
        ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0, size = 0;
    bool ok = false;
    if (RegQueryValueExW(key, value, NULL, &type, NULL, &size) == ERROR_SUCCESS) {
        if (type == REG_DWORD) {
            DWORD data = 0;
            size = sizeof(data);
            if (RegQueryValueExW(key, value, NULL, &type,
                                 reinterpret_cast<BYTE *>(&data),
                                 &size) == ERROR_SUCCESS) {
                out = data != 0;
                ok = true;
                if (typeOut) *typeOut = "REG_DWORD";
                if (dataOut) *dataOut = std::to_string(data);
            }
        } else if (type == REG_SZ || type == REG_EXPAND_SZ) {
            std::wstring buf(size / sizeof(wchar_t) + 1, L'\0');
            DWORD bytes = static_cast<DWORD>(buf.size() * sizeof(wchar_t));
            if (RegQueryValueExW(key, value, NULL, &type,
                                 reinterpret_cast<BYTE *>(&buf[0]),
                                 &bytes) == ERROR_SUCCESS) {
                std::string text;
                for (wchar_t c : buf) {
                    if (c == L'\0') break;
                    text += static_cast<char>(
                        std::tolower(static_cast<unsigned char>(c)));
                }
                // Everything an administrator plausibly types for "yes".
                // Anything else counts as "no" rather than as an unreadable
                // value: this policy can only ever turn checking off.
                out = (text == "1" || text == "true" || text == "yes" ||
                       text == "on");
                ok = true;
                if (typeOut) *typeOut = "REG_SZ";
                if (dataOut) *dataOut = text;
            }
        }
    }
    RegCloseKey(key);
    return ok;
}

static const wchar_t *const kPolicyKey =
    L"SOFTWARE\\Policies\\Cedarville University\\CedarLogic";
static const wchar_t *const kPolicyValue = L"DisableUpdateChecks";
static const wchar_t *const kSparkleKey =
    L"Software\\Cedarville University\\CedarLogic\\WinSparkle";

// WinSparkle opens the registry with no view flag, so from this 32-bit program
// it only ever sees SOFTWARE\WOW6432Node. An administrator setting a
// machine-wide default with 64-bit tools writes the plain path, which
// WinSparkle then never finds. Look in both views, keeping WinSparkle's own
// precedence: the user's setting first, the machine's only as a default.
bool readWinSparkleSetting(const char *name, std::wstring &out,
                           std::string *whereFound) {
    std::wstring wide;
    for (const char *p = name; p && *p; ++p) wide += static_cast<wchar_t>(*p);

    struct Source { HKEY root; REGSAM view; const char *label; };
    // HKCU's 32-bit view comes first because that is where WinSparkle itself
    // writes, so a setting the user already has keeps winning.
    static const Source kSources[] = {
        {HKEY_CURRENT_USER,  KEY_WOW64_32KEY, "HKCU (WOW6432Node)"},
        {HKEY_CURRENT_USER,  KEY_WOW64_64KEY, "HKCU"},
        {HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, "HKLM"},
        {HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, "HKLM (WOW6432Node)"},
    };

    for (const Source &src : kSources) {
        HKEY key = NULL;
        if (RegOpenKeyExW(src.root, kSparkleKey, 0, KEY_QUERY_VALUE | src.view,
                          &key) != ERROR_SUCCESS) {
            continue;
        }
        DWORD type = 0, size = 0;
        bool ok = false;
        // REG_SZ only, matching WinSparkle, which stores every setting as a
        // string and ignores a value of any other type.
        if (RegQueryValueExW(key, wide.c_str(), NULL, &type, NULL, &size) ==
                ERROR_SUCCESS &&
            type == REG_SZ) {
            std::wstring buf(size / sizeof(wchar_t) + 1, L'\0');
            DWORD bytes = static_cast<DWORD>(buf.size() * sizeof(wchar_t));
            if (RegQueryValueExW(key, wide.c_str(), NULL, &type,
                                 reinterpret_cast<BYTE *>(&buf[0]),
                                 &bytes) == ERROR_SUCCESS) {
                buf.resize(wcslen(buf.c_str()));
                out = buf;
                ok = true;
            }
        }
        RegCloseKey(key);
        if (ok) {
            if (whereFound) *whereFound = src.label;
            return true;
        }
    }
    return false;
}
#endif

PolicyStatus describeUpdatePolicy() {
    PolicyStatus st;
#ifdef _WIN32
    // The 64-bit view first: that is the plain path an administrator writes.
    // The 32-bit view second, for anyone who set the policy from a 32-bit tool
    // or on a 32-bit machine.
    bool disabled = false;
    if (readPolicyFlag(KEY_WOW64_64KEY, kPolicyKey, kPolicyValue, disabled,
                       &st.policyType, &st.policyData)) {
        st.policyFound = true;
        st.policyView = "64-bit";
    } else if (readPolicyFlag(KEY_WOW64_32KEY, kPolicyKey, kPolicyValue,
                              disabled, &st.policyType, &st.policyData)) {
        st.policyFound = true;
        st.policyView = "32-bit (WOW6432Node)";
    }
    st.disabled = st.policyFound && disabled;

    std::wstring sparkle;
    std::string where;
    if (readWinSparkleSetting("CheckForUpdates", sparkle, &where)) {
        st.sparkleFound = true;
        st.sparkleWhere = where;
        for (wchar_t c : sparkle) st.sparkleValue += static_cast<char>(c);
    }
#else
    st.platformNote =
        "No update policy mechanism on this platform. macOS deployments "
        "configure Sparkle through a configuration profile instead.";
#endif
    return st;
}

bool checksDisabled() {
    return describeUpdatePolicy().disabled;
}

}  // namespace update
}  // namespace cl
