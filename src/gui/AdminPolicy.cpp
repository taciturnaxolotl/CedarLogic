/*****************************************************************************
   Project: CEDAR Logic Simulator

   AdminPolicy: what an administrator has turned off for this machine.
*****************************************************************************/

#include "AdminPolicy.h"

#ifdef _WIN32
#include <windows.h>
#include <cctype>
#endif

namespace cl {
namespace policy {

namespace {
const char *const kKeyPathText =
    "HKLM\\SOFTWARE\\Policies\\Cedarville University\\CedarLogic";
}

const char *keyPath() { return kKeyPathText; }

#ifdef _WIN32

namespace {

const wchar_t *const kKey = L"SOFTWARE\\Policies\\Cedarville University\\CedarLogic";

// ASCII only, so a byte at a time is exact. Not a UTF-8 conversion; every value
// name here is a compile-time constant in this repository.
std::wstring widenAscii(const char *s) {
    std::wstring w;
    for (; s && *s; ++s) w += static_cast<wchar_t>(*s);
    return w;
}

bool readOne(REGSAM view, const wchar_t *value, Reading &out) {
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kKey, 0, KEY_READ | view, &key) !=
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
                out.disabled = data != 0;
                out.type = "REG_DWORD";
                out.data = std::to_string(data);
                ok = true;
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
                // value: a policy here can only ever turn something off.
                out.disabled = (text == "1" || text == "true" ||
                                text == "yes" || text == "on");
                out.type = "REG_SZ";
                out.data = text;
                ok = true;
            }
        }
    }
    RegCloseKey(key);
    return ok;
}

}  // namespace

bool disabledBy(const char *valueName, Reading *detail) {
    const std::wstring value = widenAscii(valueName);
    Reading r;
    if (readOne(KEY_WOW64_64KEY, value.c_str(), r)) {
        r.found = true;
        r.view = "64-bit";
    } else if (readOne(KEY_WOW64_32KEY, value.c_str(), r)) {
        r.found = true;
        r.view = "32-bit (WOW6432Node)";
    }
    if (detail) *detail = r;
    return r.found && r.disabled;
}

#else

bool disabledBy(const char *, Reading *detail) {
    if (detail) *detail = Reading();
    return false;
}

#endif

}  // namespace policy
}  // namespace cl
