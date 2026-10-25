/*****************************************************************************
   Project: CEDAR Logic Simulator

   CrashTracePosix: the POSIX half of CrashTrace.

   A signal handler may only call async-signal-safe functions; backtrace() can
   allocate or take the loader lock and turn a crash into a hang. So this only
   records the signal and leaves the stack to the platform report.
*****************************************************************************/

#include "CrashPlatform.h"
#include "CrashTrace.h"

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace cl {
namespace crash {

namespace {

// Fixed before the handler is installed, so it only ever reads them.
std::string g_logPath;
std::string g_header;

const int kFatalSignals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL};
const size_t kFatalSignalCount = sizeof(kFatalSignals) / sizeof(kFatalSignals[0]);

// Whoever held each signal before us (the reporter, where one is configured).
struct sigaction g_previous[kFatalSignalCount];

const char *signalName(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV (segmentation fault)";
        case SIGABRT: return "SIGABRT (abort)";
        case SIGBUS:  return "SIGBUS (bus error)";
        case SIGFPE:  return "SIGFPE (floating-point exception)";
        case SIGILL:  return "SIGILL (illegal instruction)";
        default:      return "fatal signal";
    }
}

void writeAll(int fd, const char *text) {
    write(fd, text, strlen(text));
}

void writeTrace(int sig) {
    int fd = open(g_logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    writeAll(fd, g_header.c_str());
    writeAll(fd, "Fatal signal: ");
    writeAll(fd, signalName(sig));
    writeAll(fd, "\n\n");
    close(fd);
}

// Call the displaced handler directly, so it sees the original siginfo and context.
void chainToPreviousAction(int sig, siginfo_t *info, void *context) {
    for (size_t i = 0; i < kFatalSignalCount; ++i) {
        if (kFatalSignals[i] != sig) continue;
        const struct sigaction &prev = g_previous[i];
        if ((prev.sa_flags & SA_SIGINFO) != 0 && prev.sa_sigaction != NULL)
            prev.sa_sigaction(sig, info, context);
        else if (prev.sa_handler != SIG_DFL && prev.sa_handler != SIG_IGN)
            prev.sa_handler(sig);
        return;
    }
}

// Reset by hand: SA_RESETHAND leaves SIGILL installed on macOS.
void dieOnThisThread(int sig) {
    struct sigaction dfl;
    memset(&dfl, 0, sizeof(dfl));
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    sigaction(sig, &dfl, NULL);
    // raise() keeps the crash on this thread with the kernel's fault record;
    // kill(getpid()) lets an idle thread take it and hides the real stack.
    raise(sig);
}

void onFatalSignal(int sig, siginfo_t *info, void *context) {
    writeTrace(sig);
    chainToPreviousAction(sig, info, context);
    dieOnThisThread(sig);
}

// Owns one thread's spare stack and gives it back when the thread exits.
struct SpareStack {
    void *memory = nullptr;
    ~SpareStack() {
        if (memory == nullptr) return;
        stack_t off;
        memset(&off, 0, sizeof(off));
        off.ss_flags = SS_DISABLE;
        sigaltstack(&off, NULL);
        free(memory);
    }
};
thread_local SpareStack t_spareStack;

}  // namespace

void giveThisThreadASignalStack() {
    stack_t current;
    // Leave one alone if someone (Sentry, on Linux) already set it up.
    if (sigaltstack(NULL, &current) == 0 && !(current.ss_flags & SS_DISABLE))
        return;
    const size_t size = std::max<size_t>(SIGSTKSZ, 64 * 1024);
    stack_t spare;
    memset(&spare, 0, sizeof(spare));
    spare.ss_sp = malloc(size);
    spare.ss_size = size;
    if (spare.ss_sp == NULL) return;
    if (sigaltstack(&spare, NULL) == 0)
        t_spareStack.memory = spare.ss_sp;
    else
        free(spare.ss_sp);
}

void installPlatformHandler(const std::string &logPath, const std::string &header) {
    g_logPath = logPath;
    g_header = header;
    giveThisThreadASignalStack();

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = onFatalSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    for (size_t i = 0; i < kFatalSignalCount; ++i)
        sigaction(kFatalSignals[i], &sa, &g_previous[i]);
}

}  // namespace crash
}  // namespace cl
