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
#ifndef _WIN32
// The strings are finalized before the handler is installed and never changed.
// Keep their raw storage here so the handler does not call into std::string.
static const char *g_crashLogPathRaw;
static const char *g_crashHeaderRaw;
static size_t g_crashHeaderSize;
static struct sigaction g_defaultSignalAction;
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

// On an otherwise-unhandled crash, walk the faulting thread's stack, symbolize
// it against the .pdb shipped next to the exe, and write a readable trace to
// %TEMP%\CedarLogic_crashtrace.log. Turns "it just crashed" into an actual
// function + file:line, which matters for a GUI app where the nastiest bugs
// only surface through live interaction and can't be caught under a debugger.
//
// Writes the file and nothing else. A message box here pumps messages back into
// a dead process, faults, and re-enters this handler over its own report.
static LONG WINAPI writeCrashTrace(EXCEPTION_POINTERS *ep) {
    // Never reset: a second fault must not truncate the first one's report.
    static LONG entered = 0;
    if (InterlockedExchange(&entered, 1) != 0) return EXCEPTION_CONTINUE_SEARCH;

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
        return EXCEPTION_CONTINUE_SEARCH;
    }

    FILE *f = fopen(logPath.c_str(), "wb"); // binary: clean '\n', no '\r\n'
    if (f == NULL) return EXCEPTION_CONTINUE_SEARCH;

    HANDLE proc = GetCurrentProcess();
    // NO_PROMPTS: dbghelp otherwise opens its own dialogs from a dead process.
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME |
                  SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
    SymInitialize(proc, NULL, TRUE);

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
    fflush(f);
    fclose(f);

    return EXCEPTION_CONTINUE_SEARCH; // unchanged, so WER and any debugger still work
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

static void posixCrashHandler(int sig, siginfo_t *, void *) {
    int fd = open(g_crashLogPathRaw, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, g_crashHeaderRaw, g_crashHeaderSize);
        write(fd, "Fatal signal: ", 14);
        CrashSignalName name = crashSignalName(sig);
        write(fd, name.text, name.size);
        write(fd, "\n\n", 2);
        close(fd);
    }
    // Restore the default action and re-raise so the OS still produces its
    // normal crash report / core dump.
    sigaction(sig, &g_defaultSignalAction, NULL);
    kill(getpid(), sig);
}
#endif

// Compute the report path (needs wx) and install the platform crash handler.
// Called once at startup.
void installCrashHandler() {
    wxFileName fn(wxStandardPaths::Get().GetTempDir(), "CedarLogic_crashtrace.log");
    g_crashLogPath = fn.GetFullPath().ToStdString();
    g_crashHeader = "CedarLogic " + VERSION_NUMBER() + " (" +
                    std::to_string((int)(sizeof(void *) * 8)) + "-bit) on " +
                    wxGetOsDescription().ToStdString() + "\n\n";
#ifdef _WIN32
    SetUnhandledExceptionFilter(writeCrashTrace);
#else
    g_crashLogPathRaw = g_crashLogPath.c_str();
    g_crashHeaderRaw = g_crashHeader.data();
    g_crashHeaderSize = g_crashHeader.size();
    memset(&g_defaultSignalAction, 0, sizeof(g_defaultSignalAction));
    g_defaultSignalAction.sa_handler = SIG_DFL;
    sigemptyset(&g_defaultSignalAction.sa_mask);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = posixCrashHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
#endif
}

}  // namespace crash
}  // namespace cl
