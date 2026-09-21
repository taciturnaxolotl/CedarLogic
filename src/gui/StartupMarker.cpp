/*****************************************************************************
   Project: CEDAR Logic Simulator

   StartupMarker: telling a startup crash from a later one.
*****************************************************************************/

#include "StartupMarker.h"

#include "CrashIssue.h"

#include <cstdio>
#include <cstdlib>

namespace cl {

#ifdef _WIN32
static const char kPathSeparator = '\\';
#else
static const char kPathSeparator = '/';
#endif

StartupMarker::StartupMarker(const std::string &tempDir) : path(tempDir) {
    if (!path.empty() && path[path.size() - 1] != '/' &&
        path[path.size() - 1] != '\\')
        path += kPathSeparator;
    path += "CedarLogic_startup.marker";
}

int StartupMarker::consecutiveFailures() const {
    std::string body = crash::readFile(path);
    while (!body.empty() &&
           (body[body.size() - 1] == '\n' || body[body.size() - 1] == '\r'))
        body.erase(body.size() - 1);
    if (body.empty()) return 0;
    int n = atoi(body.c_str());
    return n > 0 ? n : 1; // a marker with no readable count still means one failure
}

void StartupMarker::arm(int attempt) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "%d\n", attempt);
    fclose(f);
}

void StartupMarker::disarm() const {
    remove(path.c_str());
}

}  // namespace cl
