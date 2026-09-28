/*****************************************************************************
   Project: CEDAR Logic Simulator

   CrashTrace: writing a stack trace when the app dies.
*****************************************************************************/

#include "CrashTrace.h"

#include "../version.h"
#include "wx/filename.h"
#include "wx/stdpaths.h"
#include "wx/utils.h"

#include <cstdio>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#else
#include <csignal>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace cl {
namespace crash {

static std::string g_crashLogPath;
static std::string g_crashHeader;
#ifdef _WIN32
// Where dbghelp should look for the .pdb. Computed at startup because the
// handler must not allocate, and passed explicitly because the default does not
// include the directory the program was installed into -- see
// installCrashHandler().
static std::string g_symbolSearchPath;
// Whoever held the top-level filter before us. In a build with crash reporting
// configured that is crashpad, which installs its own and keeps no link back to
// what it replaced. A process has room for exactly one filter, so writing the
// trace and sending the report are only both possible if we hand the exception
// on at the end. See installCrashHandler().
static LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter;
#else
// The strings are finalized before the handler is installed and never changed.
// Keep their raw storage here so the handler does not call into std::string.
static const char *g_crashLogPathRaw;
static const char *g_crashHeaderRaw;
static size_t g_crashHeaderSize;

// The handlers we displaced, one per signal, for the same reason as
// g_previousFilter above. Plain storage and a linear scan, because a signal
// handler may not allocate and may not take a lock.
struct SavedSignalAction {
    int number;
    struct sigaction action;
};
static SavedSignalAction g_savedSignalActions[8];
static size_t g_savedSignalActionCount;
#endif

const std::string &logPath() {
    return g_crashLogPath;
}

#ifdef _WIN32
// No allocation and no CRT buffering, for a stack too exhausted to afford either.
static void rawWriteLine(HANDLE h, const char *s) {
    if (h == INVALID_HANDLE_VALUE || !s) return;
    DWORD wrote = 0;
    WriteFile(h, s, (DWORD)strlen(s), &wrote, NULL);
}

// Shorten a compiler-emitted absolute source path to a repo-relative one with
// forward slashes (C:\...\CedarLogic\src\gui\guiGate.cpp -> src/gui/guiGate.cpp)
// so the trace's file column stays short and consistent. Writes into buf.
static const char *shortSourcePath(const char *full, char *buf, size_t bufLen) {
    const char *p = strstr(full, "src\\");
    if (!p) p = strstr(full, "src/");
    if (!p) { const char *s = strrchr(full, '\\'); p = s ? s + 1 : full; }
    size_t i = 0;
    for (; p[i] && i + 1 < bufLen; i++) buf[i] = (p[i] == '\\') ? '/' : p[i];
    buf[i] = '\0';
    return buf;
}

// Pass the exception to the filter we displaced, with the original record, so
// the reporter sees the fault itself rather than wherever we happened to give
// up. EXCEPTION_CONTINUE_SEARCH when there is nobody behind us, which leaves
// WER and any attached debugger exactly as they were.
static LONG chainToPreviousFilter(EXCEPTION_POINTERS *ep) {
    return g_previousFilter ? g_previousFilter(ep) : EXCEPTION_CONTINUE_SEARCH;
}

// On an otherwise-unhandled crash, walk the faulting thread's stack, symbolize
// it against the .pdb shipped next to the exe, and write a readable trace to
// %TEMP%\CedarLogic_crashtrace.log. Turns "it just crashed" into an actual
// function + file:line, which matters for a GUI app where the nastiest bugs
// only surface through live interaction and can't be caught under a debugger.
//
// Writes the file and hands the crash on. A message box here pumps messages
// back into a dead process, faults, and re-enters this handler over its own
// report.
static LONG WINAPI writeCrashTrace(EXCEPTION_POINTERS *ep) {
    // Never reset: a second fault must not truncate the first one's report.
    static LONG entered = 0;
    if (InterlockedExchange(&entered, 1) != 0) return chainToPreviousFilter(ep);

    const std::string &logPath = g_crashLogPath;
    const DWORD code = ep->ExceptionRecord->ExceptionCode;

    // One guard page left. The walk below needs far more and would fault again.
    if (code == EXCEPTION_STACK_OVERFLOW) {
        HANDLE h = CreateFileA(logPath.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        rawWriteLine(h, g_crashHeader.c_str());
        rawWriteLine(h,
                     "Stack overflow (0xC00000FD).\n\n"
                     "  No stack trace: unwinding needs stack this crash has "
                     "already used up.\n"
                     "  Almost always unbounded recursion.\n");
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        return chainToPreviousFilter(ep);
    }

    FILE *f = fopen(logPath.c_str(), "wb"); // binary: clean '\n', no '\r\n'
    if (f == NULL) return chainToPreviousFilter(ep);

    HANDLE proc = GetCurrentProcess();
    // NO_PROMPTS: dbghelp otherwise opens its own dialogs from a dead process.
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME |
                  SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
    SymInitialize(proc, g_symbolSearchPath.empty() ? NULL
                                                   : g_symbolSearchPath.c_str(),
                  TRUE);

    fputs(g_crashHeader.c_str(), f);
    fprintf(f, "Unhandled exception 0x%08lX at %p\n",
            code, ep->ExceptionRecord->ExceptionAddress);

    // For an access violation, ExceptionInformation[0] is 0=read/1=write/8=DEP
    // and [1] is the faulting address -- the actual bad pointer we dereferenced.
    if (code == EXCEPTION_ACCESS_VIOLATION &&
        ep->ExceptionRecord->NumberParameters >= 2) {
        ULONG_PTR kind = ep->ExceptionRecord->ExceptionInformation[0];
        fprintf(f, "  %s address 0x%p\n",
                kind == 1 ? "write to" : kind == 8 ? "execute at" : "read from",
                (void *)ep->ExceptionRecord->ExceptionInformation[1]);
    }

    // Faulting instruction as module+RVA, which is stable across ASLR runs and
    // can be mapped back to a source line with the .pdb offline.
    DWORD64 faultBase = SymGetModuleBase64(proc, (DWORD64)ep->ExceptionRecord->ExceptionAddress);
    if (faultBase) {
        char mod[MAX_PATH] = {};
        GetModuleFileNameA((HMODULE)faultBase, mod, MAX_PATH);
        fprintf(f, "  fault at %s +0x%llx (base 0x%llx)\n", mod,
                (unsigned long long)((DWORD64)ep->ExceptionRecord->ExceptionAddress - faultBase),
                (unsigned long long)faultBase);
    }
    fprintf(f, "\n");

    // StackWalk64 mutates the CONTEXT as it unwinds; the OS still needs the real
    // one to bucket the crash, so walk a copy.
    CONTEXT walkCtx = *ep->ContextRecord;
    CONTEXT *ctx = &walkCtx;
    STACKFRAME64 frame = {};
    DWORD machine;
#if defined(_M_X64)
    machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = ctx->Rip;    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx->Rbp; frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx->Rsp; frame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_IX86)
    machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = ctx->Eip;    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx->Ebp; frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx->Esp; frame.AddrStack.Mode = AddrModeFlat;
#else
    machine = IMAGE_FILE_MACHINE_UNKNOWN;
#endif
    int resolved = 0;
    for (int i = 0; i < 96; i++) {
        if (!StackWalk64(machine, proc, GetCurrentThread(), &frame, ctx, NULL,
                         SymFunctionTableAccess64, SymGetModuleBase64, NULL))
            break;
        if (frame.AddrPC.Offset == 0) break;
        DWORD64 addr = frame.AddrPC.Offset;

        char symBuf[sizeof(SYMBOL_INFO) + 512] = {};
        SYMBOL_INFO *sym = (SYMBOL_INFO *)symBuf;
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 511;
        DWORD64 disp = 0;
        if (SymFromAddr(proc, addr, &disp, sym)) {
            resolved++;
            IMAGEHLP_LINE64 line = {};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisp = 0;
            if (SymGetLineFromAddr64(proc, addr, &lineDisp, &line)) {
                char rel[MAX_PATH];
                fprintf(f, "  %-42s %s:%lu\n", sym->Name,
                        shortSourcePath(line.FileName, rel, sizeof(rel)), line.LineNumber);
            } else {
                fprintf(f, "  %s +0x%llx\n", sym->Name, (unsigned long long)disp);
            }
        } else {
            DWORD64 base = SymGetModuleBase64(proc, addr);
            if (base) {
                char mod[MAX_PATH] = {};
                GetModuleFileNameA((HMODULE)base, mod, MAX_PATH);
                const char *slash = strrchr(mod, '\\');
                fprintf(f, "  %s +0x%llx\n", slash ? slash + 1 : mod,
                        (unsigned long long)(addr - base));
            } else {
                fprintf(f, "  0x%llx\n", (unsigned long long)addr);
            }
        }
    }
    // A trace of bare offsets reads the same whether the .pdb was missing,
    // mismatched, or simply never looked for, and telling those apart after the
    // fact once cost an evening of disassembly. Say which it was, in the report.
    if (resolved == 0) {
        IMAGEHLP_MODULE64 mi = {};
        mi.SizeOfStruct = sizeof(mi);
        const char *state = "the faulting module was not found";
        if (faultBase && SymGetModuleInfo64(proc, faultBase, &mi)) {
            switch (mi.SymType) {
                case SymNone:     state = "no symbols for the module"; break;
                case SymExport:   state = "exports only, no .pdb"; break;
                case SymPdb:      state = "a .pdb loaded but matched nothing"; break;
                case SymDeferred: state = "symbol loading never ran"; break;
                default:          state = "symbols of some other kind"; break;
            }
        }
        fprintf(f, "\n  No names above: %s.\n  Looked in: %s\n", state,
                g_symbolSearchPath.empty() ? "(dbghelp's default path)"
                                           : g_symbolSearchPath.c_str());
    }

    fflush(f);
    fclose(f);

    return chainToPreviousFilter(ep);
}
#else
// POSIX signal handlers may only use async-signal-safe functions. In
// particular, backtrace() can allocate or acquire the loader lock, turning a
// crash into a permanent hang. Record the signal, then let the OS produce the
// platform report/core dump that can be symbolized outside the broken process.
struct CrashSignalName {
    const char *text;
    size_t size;
};

static CrashSignalName crashSignalName(int sig) {
    switch (sig) {
        case SIGSEGV: return {"SIGSEGV (segmentation fault)", sizeof("SIGSEGV (segmentation fault)") - 1};
        case SIGABRT: return {"SIGABRT (abort)", sizeof("SIGABRT (abort)") - 1};
        case SIGBUS:  return {"SIGBUS (bus error)", sizeof("SIGBUS (bus error)") - 1};
        case SIGFPE:  return {"SIGFPE (floating-point exception)", sizeof("SIGFPE (floating-point exception)") - 1};
        case SIGILL:  return {"SIGILL (illegal instruction)", sizeof("SIGILL (illegal instruction)") - 1};
        default:      return {"fatal signal", sizeof("fatal signal") - 1};
    }
}

static void posixCrashHandler(int sig, siginfo_t *info, void *context) {
    int fd = open(g_crashLogPathRaw, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, g_crashHeaderRaw, g_crashHeaderSize);
        write(fd, "Fatal signal: ", 14);
        CrashSignalName name = crashSignalName(sig);
        write(fd, name.text, name.size);
        write(fd, "\n\n", 2);
        close(fd);
    }

    // Hand the same signal, with its original siginfo and context, to whoever
    // handled it before us -- the reporter, where one is configured. Calling it
    // rather than re-raising matters: a re-raised signal carries the context of
    // the raise, so the report would name this line instead of the fault.
    const struct sigaction *previous = NULL;
    for (size_t i = 0; i < g_savedSignalActionCount; ++i) {
        if (g_savedSignalActions[i].number == sig) {
            previous = &g_savedSignalActions[i].action;
            break;
        }
    }
    if (previous != NULL) {
        if ((previous->sa_flags & SA_SIGINFO) != 0 &&
            previous->sa_sigaction != NULL) {
            previous->sa_sigaction(sig, info, context);
        } else if (previous->sa_handler != SIG_DFL &&
                   previous->sa_handler != SIG_IGN &&
                   previous->sa_handler != NULL) {
            previous->sa_handler(sig);
        }
    }

    // A handler normally terminates. If it returns, force the normal OS crash
    // path instead of resuming at the fatal instruction and faulting again.
    struct sigaction dfl;
    memset(&dfl, 0, sizeof(dfl));
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    sigaction(sig, &dfl, NULL);
    kill(getpid(), sig);
}
#endif

// Compute the report path (needs wx) and install the platform crash handler.
// Called once at startup, and deliberately after the crash reporter has
// started: both want the same hook, the last one to ask for it gets it, and
// only this one knows how to pass the crash along afterwards.
void installCrashHandler() {
    wxFileName fn(wxStandardPaths::Get().GetTempDir(), "CedarLogic_crashtrace.log");
    g_crashLogPath = fn.GetFullPath().ToStdString();
    g_crashHeader = "CedarLogic " + VERSION_NUMBER() + " (" +
                    std::to_string((int)(sizeof(void *) * 8)) + "-bit) on " +
                    wxGetOsDescription().ToStdString() + "\n\n";
#ifdef _WIN32
    // Tell dbghelp where the .pdb actually is. Its default search path is the
    // working directory plus _NT_SYMBOL_PATH, and neither is the install folder
    // when the program is launched from a Start Menu shortcut -- so the .pdb
    // sitting right beside the exe was never being looked at. The absolute path
    // baked into the exe at link time points at the build machine, which is why
    // a locally built copy symbolizes and a released one does not.
    wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
    g_symbolSearchPath = exe.GetPath().ToStdString();
    g_previousFilter = SetUnhandledExceptionFilter(writeCrashTrace);
#else
    g_crashLogPathRaw = g_crashLogPath.c_str();
    g_crashHeaderRaw = g_crashHeader.data();
    g_crashHeaderSize = g_crashHeader.size();

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = posixCrashHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;

    static const int kFatalSignals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL};
    for (size_t i = 0; i < sizeof(kFatalSignals) / sizeof(kFatalSignals[0]); ++i) {
        struct sigaction previous;
        memset(&previous, 0, sizeof(previous));
        if (sigaction(kFatalSignals[i], &sa, &previous) != 0) continue;
        if (g_savedSignalActionCount <
            sizeof(g_savedSignalActions) / sizeof(g_savedSignalActions[0])) {
            g_savedSignalActions[g_savedSignalActionCount].number = kFatalSignals[i];
            g_savedSignalActions[g_savedSignalActionCount].action = previous;
            ++g_savedSignalActionCount;
        }
    }
#endif
}

}  // namespace crash
}  // namespace cl
