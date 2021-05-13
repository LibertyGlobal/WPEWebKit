#include <config.h>
#include <wtf/Assertions.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>

#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include "HijackSignals.h"

#ifdef LOG
#undef LOG
#endif

namespace {

const char *fname(const char *const path)
{
    if(!path) return "";

    const auto p = strrchr(path, '/');

    return p ? p + 1 /* skip '/' */ : path;
}

#define LOG(fmt, ...) fprintf(stderr, "%s:%d %s [%d] " fmt "\n", fname(__FILE__), __LINE__, __FUNCTION__, int(::getpid()), ##__VA_ARGS__)

constexpr int signals_[] =
{
    SIGABRT, // Abort signal from abort(3), a synonym for IOT
    SIGBUS,  // Bus error (bad memory access)
    SIGFPE,  // Floating point exception
    SIGILL,  // Illegal Instruction
    SIGSEGV, // Invalid memory reference
    SIGSYS,  // Bad argument to routine (SVr4)
    SIGTRAP, // Trace/breakpoint trap
    SIGXCPU, // CPU time limit exceeded (4.2BSD)
    SIGXFSZ, // File size limit exceeded (4.2BSD)
};

constexpr auto signalsNum_ = sizeof(signals_) / sizeof(signals_[0]);

struct SigactionBackup
{
    std::mutex mutex;
    struct sigaction handler[signalsNum_];
};

SigactionBackup sigactionBackup_;

using signalHandler_t = void (*)(int, siginfo_t *, void *);


void hijackSignalsImpl(signalHandler_t handler)
{
    LOG();

    std::lock_guard<std::mutex> lock{sigactionBackup_.mutex};

    for (int signalNo : signals_)
    {
        struct sigaction action;

        sigemptyset(&action.sa_mask);
        action.sa_sigaction = handler;
        action.sa_flags = (SA_SIGINFO | SA_NODEFER);

        if(0 != ::sigaction(signalNo, &action, &sigactionBackup_.handler[signalNo]))
        {
            LOG("failed to set handler for %s(%d)", strsignal(signalNo), signalNo);
        }
        else
        {
            LOG("set handler for %s(%d)", strsignal(signalNo), signalNo);
        }
    }
}

void restoreSignalHandlers()
{
    LOG();

    std::lock_guard<std::mutex> lock{sigactionBackup_.mutex};

    for (int signalNo : signals_)
    {
        if(0 != ::sigaction(signalNo, &sigactionBackup_.handler[signalNo], nullptr))
        {
            LOG("failed to set handler for %s(%d)", strsignal(signalNo), signalNo);
        }
        else
        {
            LOG("set handler for %s(%d)", strsignal(signalNo), signalNo);
        }
    }
}

bool skip(const struct sigaction &action)
{
    return
        SA_SIGINFO == (action.sa_flags & SA_SIGINFO)
        ? (
            nullptr == action.sa_sigaction)
        : (
            nullptr == action.sa_handler
            || SIG_DFL == action.sa_handler
            || SIG_IGN == action.sa_handler);
}

void handler(int signalNo, siginfo_t *info, void *ctx)
{
    LOG("%s(%d)", strsignal(signalNo), signalNo);

    /* restore original signal handlers from backup - we allow signals
     * to be delivered during signal handler execution */
    restoreSignalHandlers();

    WTFReportBacktrace();

    const auto i =
        std::find_if(
            std::begin(signals_), std::end(signals_),
            [=](int n){return n == signalNo;});

    if(std::end(signals_) == i)
    {
        LOG("unexpected signal %s(%d) delivered, aborting", strsignal(signalNo), signalNo);
        std::abort();
    }

    const auto offset = std::distance(std::begin(signals_), i);

    {
        std::lock_guard<std::mutex> lock{sigactionBackup_.mutex};

        if(skip(sigactionBackup_.handler[offset]))
        {
            /* there is no valid handler address present */
            goto raise;
        }


        LOG("BEGIN: calling original handler for %s(%d)", strsignal(signalNo), signalNo);

        if(SA_SIGINFO == (sigactionBackup_.handler[offset].sa_flags & SA_SIGINFO))
        {
            (sigactionBackup_.handler[offset].sa_sigaction)(signalNo, info, ctx);
        }
        else
        {
            (sigactionBackup_.handler[offset].sa_handler)(signalNo);
        }

        LOG("END: calling original handler for %s(%d)", strsignal(signalNo), signalNo);
    }
    return;
raise:
    LOG("raising %s(%d) to self", strsignal(signalNo), signalNo);
    /* make sure not lock is held at this point */
    ::raise(signalNo);
}

} // namespace


void hijackSignals()
{
    LOG();

    hijackSignalsImpl(handler);
    WTFSetCrashHook(restoreSignalHandlers);
}
