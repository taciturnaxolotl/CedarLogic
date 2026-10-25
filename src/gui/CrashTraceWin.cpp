/*****************************************************************************
   Project: CEDAR Logic Simulator

   CrashTraceWin: the Windows half of CrashTrace. Walks and symbolizes the
   faulting stack with dbghelp, then hands the crash to the reporter.
*****************************************************************************/

#include "CrashPlatform.h"
#include "CrashTrace.h"

#include <cstdio>
#include <cstring>
#include <string>

#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

namespace cl {
namespace crash {

namespace {

std::string g_crashLogPath;
std::string g_crashHeader;
// The install folder: dbghelp's default path never includes it, so the .pdb beside the exe went unread.
std::string g_symbolSearchPath;
// Usually crashpad's. A process has one filter, so we hand every crash on to it.
LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter;

// No allocation and no CRT buffering, for a stack too exhausted to afford either.
void rawWriteLine(HANDLE h, const char *s) {
    if (h == INVALID_HANDLE_VALUE || !s) return;
    DWORD wrote = 0;
    WriteFile(h, s, (DWORD)strlen(s), &wrote, NULL);
}

// Shorten a compiler-emitted absolute source path to a repo-relative one with
// forward slashes (C:\...\CedarLogic\src\gui\guiGate.cpp -> src/gui/guiGate.cpp)
// so the trace's file column stays short and consistent. Writes into buf.
const char *shortSourcePath(const char *full, char *buf, size_t bufLen) {
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
LONG chainToPreviousFilter(EXCEPTION_POINTERS *ep) {
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
LONG WINAPI writeCrashTrace(EXCEPTION_POINTERS *ep) {
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

std::string exeDirectory() {
    char path[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string dir(path, n);
    const size_t slash = dir.find_last_of("\\/");
    return slash == std::string::npos ? std::string() : dir.substr(0, slash);
}

}  // namespace

void installPlatformHandler(const std::string &logPath, const std::string &header) {
    g_crashLogPath = logPath;
    g_crashHeader = header;
    g_symbolSearchPath = exeDirectory();
    g_previousFilter = SetUnhandledExceptionFilter(writeCrashTrace);
}

// The overflow branch of writeCrashTrace already runs on the one guard page Windows leaves.
void giveThisThreadASignalStack() {}

}  // namespace crash
}  // namespace cl
