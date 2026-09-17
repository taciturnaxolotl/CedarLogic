/*****************************************************************************
   Project: CEDAR Logic Simulator

   UpdateInfo: what the appcast says is newer than what is installed.

   The crash dialog wants to tell the user a fix already exists before asking
   them to file a report ("you are on 3.1.1, 3.1.2 is available, try that
   first"). That means reading the appcast ourselves rather than letting the
   updater own the question, because the updater's UI only appears when it
   decides to appear and cannot be embedded in our own dialog.

   Everything here except FetchAppcastLatest is pure: no wx, no sockets, no
   platform headers. That is deliberate, so the parsing and the version
   comparison can be unit tested from logic/tests without a display.
*****************************************************************************/

#pragma once

#include <string>

namespace cl {
namespace update {

// A dotted version, component by component. Shorter vectors compare as if the
// missing components were zero, so "3.1" and "3.1.0" are equal.
struct Version {
    int parts[4] = {0, 0, 0, 0};
    int count = 0;
    bool valid = false;
};

// Parse "3.1.2" style text. Junk components become zero; a string with no
// leading digits is invalid rather than silently 0.0.0.
Version parseVersion(const std::string &s);

// True iff a is strictly newer than b.
bool newerThan(const Version &a, const Version &b);

// Split an absolute URL into the host and the path-and-query. Returns false if
// the URL has no host, so a caller cannot fetch from an empty address.
bool splitUrl(const std::string &url, std::string &host, std::string &path);

// Pick the newest release from an appcast's XML for one platform. "macos" or
// "windows". Returns false if the feed holds no item for that platform, which a
// caller must treat as "unknown", never as "up to date".
bool appcastLatest(const std::string &xml, const std::string &os, Version &out);

// The body of the appcast feed, empty on any failure. Blocking, with its own
// timeout, so it belongs on a worker thread.
std::string fetchAppcast(const std::string &url);

}  // namespace update
}  // namespace cl
