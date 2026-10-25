// The POSIX crash handler, run against real crashes in a forked child: it must
// leave a trace, hand the signal on, and die of it on the thread that crashed.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define DOCTEST_CONFIG_NO_POSIX_SIGNALS
#include <doctest/doctest.h>

#include "CrashPlatform.h"
#include "CrashTrace.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <pthread.h>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach/mach.h>
#endif

using namespace cl::crash;

namespace {

const std::string kHeader = "CedarLogic test\n\n";
// A handler that fails to die must fail the test, not hang it.
const int kDeadlineSeconds = 5;
volatile int *const kFaultAddress = reinterpret_cast<volatile int *>(0x1234);

enum class Crash { FaultOnWorker, AbortOnWorker, OverflowOnMain, OverflowOnWorker };

std::string readFile(const std::string &path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// ---- the child ------------------------------------------------------------

const char *g_chainMarker;

// Stands in for the reporter: proves it was called, with the original siginfo.
void previousHandler(int, siginfo_t *info, void *) {
    if (info->si_addr != (void *)kFaultAddress) return;
    int fd = open(g_chainMarker, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) close(fd);
}

void installPreviousHandler() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = previousHandler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
}

int recurseForever(int depth) {
    volatile char frame[512];
    frame[0] = static_cast<char>(depth);
    return recurseForever(depth + 1) + frame[0];
}

void *crashOnWorker(void *arg) {
    switch (*static_cast<Crash *>(arg)) {
        case Crash::FaultOnWorker: *kFaultAddress = 1; break;
        case Crash::AbortOnWorker: abort();
        case Crash::OverflowOnWorker:
            giveThisThreadASignalStack();
            recurseForever(0);
            break;
        case Crash::OverflowOnMain: break;
    }
    return nullptr;
}

[[noreturn]] void crashChild(const std::string &log, const std::string &marker, Crash how) {
    g_chainMarker = marker.c_str();
    installPreviousHandler();
    installPlatformHandler(log, kHeader);
    if (how == Crash::OverflowOnMain) _exit(recurseForever(0));
    pthread_t worker;
    pthread_create(&worker, nullptr, crashOnWorker, &how);
    for (;;) pause();
}

// ---- watching the crash from outside --------------------------------------

#ifdef __APPLE__
// Receives a forked child's EXC_CRASH, the message Crashpad sees. Exception
// ports are inherited across fork, so the child's crash comes here instead of
// to ReportCrash.
class MachCrashCatcher {
public:
    struct Report {
        bool caught = false;
        bool onMainThread = false;
        int originalException = -1;
        uint64_t faultAddress = 0;
    };

    MachCrashCatcher() {
        mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port_);
        mach_port_insert_right(mach_task_self(), port_, port_, MACH_MSG_TYPE_MAKE_SEND);
        mach_msg_type_number_t count = 1;
        task_swap_exception_ports(mach_task_self(), EXC_MASK_CRASH, port_,
                                  EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES, THREAD_STATE_NONE,
                                  &savedMask_, &count, &savedPort_, &savedBehavior_,
                                  &savedFlavor_);
    }

    ~MachCrashCatcher() {
        mach_port_deallocate(mach_task_self(), port_);
        mach_port_mod_refs(mach_task_self(), port_, MACH_PORT_RIGHT_RECEIVE, -1);
    }

    // In the parent, once the child holds the port: stop catching our own crashes.
    void restoreParentPorts() {
        task_set_exception_ports(mach_task_self(), EXC_MASK_CRASH, savedPort_,
                                 savedBehavior_, savedFlavor_);
    }

    Report wait() {
        Request req = {};
        Report report;
        if (mach_msg(&req.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(req),
                     port_, kDeadlineSeconds * 1000, MACH_PORT_NULL) != MACH_MSG_SUCCESS)
            return report;
        report.caught = req.exception == EXC_CRASH;
        report.originalException = static_cast<int>((req.code[0] >> 20) & 0xf);
        report.faultAddress = static_cast<uint64_t>(req.code[1]);
        report.onMainThread = isMainThread(req.task.name, req.thread.name);
        reply(req);
        return report;
    }

private:
    // mach_exception_raise from <mach/mach_exc.defs>; MIG packs it to 4 bytes.
#pragma pack(push, 4)
    struct Request {
        mach_msg_header_t header;
        mach_msg_body_t body;
        mach_msg_port_descriptor_t thread;
        mach_msg_port_descriptor_t task;
        NDR_record_t ndr;
        exception_type_t exception;
        mach_msg_type_number_t codeCount;
        int64_t code[2];
        mach_msg_trailer_t trailer;
    };
    struct Reply {
        mach_msg_header_t header;
        NDR_record_t ndr;
        kern_return_t result;
    };
#pragma pack(pop)

    static uint64_t threadId(mach_port_t thread) {
        thread_identifier_info_data_t info;
        mach_msg_type_number_t count = THREAD_IDENTIFIER_INFO_COUNT;
        thread_info(thread, THREAD_IDENTIFIER_INFO, (thread_info_t)&info, &count);
        return info.thread_id;
    }

    // task_threads lists the main thread first; Crashpad relies on the same order.
    static bool isMainThread(mach_port_t task, mach_port_t thread) {
        thread_act_array_t threads;
        mach_msg_type_number_t count = 0;
        if (task_threads(task, &threads, &count) != KERN_SUCCESS || count == 0) return false;
        return threadId(threads[0]) == threadId(thread);
    }

    static void reply(const Request &req) {
        Reply rep = {};
        rep.header.msgh_bits = MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(req.header.msgh_bits), 0);
        rep.header.msgh_size = sizeof(rep);
        rep.header.msgh_remote_port = req.header.msgh_remote_port;
        rep.header.msgh_id = req.header.msgh_id + 100;
        rep.ndr = NDR_record;
        rep.result = KERN_SUCCESS;
        mach_msg(&rep.header, MACH_SEND_MSG, sizeof(rep), 0, MACH_PORT_NULL,
                 MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    }

    mach_port_t port_ = MACH_PORT_NULL;
    exception_mask_t savedMask_;
    mach_port_t savedPort_;
    exception_behavior_t savedBehavior_;
    thread_state_flavor_t savedFlavor_;
};
#endif

struct Outcome {
    int termSignal = 0;
    std::string trace;
    bool chained = false;
#ifdef __APPLE__
    MachCrashCatcher::Report report;
#endif
};

std::string makeTempPath() {
    char path[] = "/tmp/cedarlogic_crashtrace_XXXXXX";
    const int fd = mkstemp(path);
    if (fd >= 0) close(fd);
    return path;
}

// SIGKILL, not alarm(): a thread looping through its own fault never takes SIGALRM.
int waitWithDeadline(pid_t child) {
    int status = 0;
    for (int tick = 0; tick < kDeadlineSeconds * 100; ++tick) {
        if (waitpid(child, &status, WNOHANG) == child) return status;
        usleep(10000);
    }
    kill(child, SIGKILL);
    waitpid(child, &status, 0);
    return status;
}

Outcome runCrash(Crash how) {
    const std::string log = makeTempPath();
    const std::string marker = log + ".chained";
    Outcome out;
#ifdef __APPLE__
    MachCrashCatcher catcher;
#endif

    const pid_t child = fork();
    if (child == 0) crashChild(log, marker, how);

#ifdef __APPLE__
    catcher.restoreParentPorts();
    out.report = catcher.wait();
#endif
    const int status = waitWithDeadline(child);
    if (WIFSIGNALED(status)) out.termSignal = WTERMSIG(status);
    out.trace = readFile(log);
    out.chained = access(marker.c_str(), F_OK) == 0;
    remove(log.c_str());
    remove(marker.c_str());
    return out;
}

bool isSegvOrBus(int sig) { return sig == SIGSEGV || sig == SIGBUS; }

}  // namespace

TEST_CASE("a fault on a worker thread is written down, handed on, and kept on that thread") {
    const Outcome out = runCrash(Crash::FaultOnWorker);
    CHECK(out.termSignal == SIGSEGV);
    CHECK(out.trace == kHeader + "Fatal signal: SIGSEGV (segmentation fault)\n\n");
    CHECK(out.chained);
#ifdef __APPLE__
    REQUIRE(out.report.caught);
    CHECK_FALSE(out.report.onMainThread);
    CHECK(out.report.originalException == EXC_BAD_ACCESS);
    CHECK(out.report.faultAddress == reinterpret_cast<uint64_t>(kFaultAddress));
#endif
}

TEST_CASE("an abort on a worker thread is written down and kept on that thread") {
    const Outcome out = runCrash(Crash::AbortOnWorker);
    CHECK(out.termSignal == SIGABRT);
    CHECK(out.trace == kHeader + "Fatal signal: SIGABRT (abort)\n\n");
#ifdef __APPLE__
    REQUIRE(out.report.caught);
    CHECK_FALSE(out.report.onMainThread);
#endif
}

TEST_CASE("a stack overflow on the installing thread still reaches the handler") {
    const Outcome out = runCrash(Crash::OverflowOnMain);
    CHECK(isSegvOrBus(out.termSignal));
    CHECK(out.trace.rfind(kHeader + "Fatal signal: SIG", 0) == 0);
#ifdef __APPLE__
    REQUIRE(out.report.caught);
    CHECK(out.report.onMainThread);
    CHECK(out.report.originalException == EXC_BAD_ACCESS);
#endif
}

TEST_CASE("a worker given a signal stack survives its own overflow long enough to report") {
    const Outcome out = runCrash(Crash::OverflowOnWorker);
    CHECK(isSegvOrBus(out.termSignal));
    CHECK(out.trace.rfind(kHeader + "Fatal signal: SIG", 0) == 0);
#ifdef __APPLE__
    REQUIRE(out.report.caught);
    CHECK_FALSE(out.report.onMainThread);
#endif
}
