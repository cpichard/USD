//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
/// \file debugger.cpp

#include "pxr/pxr.h"
#include "pxr/base/arch/debugger.h"
#include "pxr/base/arch/daemon.h"
#include "pxr/base/arch/env.h"
#include "pxr/base/arch/error.h"
#include "pxr/base/arch/export.h"
#include "pxr/base/arch/stackTrace.h"
#include "pxr/base/arch/systemInfo.h"
#if defined(ARCH_OS_LINUX) || defined(ARCH_OS_DARWIN)
#include "pxr/base/arch/inttypes.h"
#include <sys/types.h>
#if !defined(ARCH_OS_IPHONE)
#include <sys/ptrace.h>
#endif
#include <sys/stat.h>
#include <sys/wait.h>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <unistd.h>
#include <string>
#include <string_view>
#include <system_error>
#endif
#if defined(ARCH_OS_DARWIN)
#include <sys/sysctl.h>
#endif
#if defined(ARCH_OS_WINDOWS)
#include <Windows.h>
#endif
#if defined(ARCH_OS_WASM_VM)
#include <emscripten.h>
#endif
#include <atomic>

PXR_NAMESPACE_OPEN_SCOPE

// We don't want this inlined so ArchDebuggerTrap() is as clean as
// possible.  The fewer instructions in that function, the more likely
// we don't confuse the debugger's stack unwinding.
static void Arch_DebuggerInit() ARCH_NOINLINE;

static bool _archDebuggerInitialized = false;
static bool _archDebuggerEnabled = false;
static std::atomic<bool> _archDebuggerWait(false);

static char** _archDebuggerAttachArgs = 0;

#if defined(ARCH_OS_LINUX) || defined(ARCH_OS_DARWIN)
static
void
Arch_DebuggerTrapHandler(int)
{
    // If we're not configured to wait then do nothing.  Otherwise
    // reconfigure to not wait the next time then wait for the
    // debugger to continue us.
    bool oldVal = true;
    if (_archDebuggerWait.compare_exchange_strong(oldVal, false)) {
        raise(SIGSTOP);
    }
}
#endif

#if defined(ARCH_OS_LINUX) || defined(ARCH_OS_DARWIN)
static
void
Arch_DebuggerInitPosix()
{
    _archDebuggerInitialized = true;

    // Handle the SIGTRAP signal so if no debugger is attached then
    // nothing happens when ArchDebuggerTrap() is called.  If we
    // didn't handle this signal then the app would die.
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_flags   = SA_NODEFER;
    act.sa_handler = Arch_DebuggerTrapHandler;
    if (sigaction(SIGTRAP, &act, 0)) {
        ARCH_WARNING("Failed to set SIGTRAP handler;  "
                     "debug trap not enabled");
        _archDebuggerEnabled = false;
    }
    else {
        _archDebuggerEnabled = true;
    }
}
namespace {
struct InitPosix {
    InitPosix() { Arch_DebuggerInitPosix(); }
};
}
#endif

static
void
Arch_DebuggerInit()
{
#if defined(ARCH_OS_LINUX) || defined(ARCH_OS_DARWIN)
#if defined(ARCH_CPU_INTEL) && defined(ARCH_BITS_64)
    // Save some registers that normally don't have to be preserved.  We
    // do this so the caller of ArchDebuggerTrap() can see its arguments
    // that were passed in registers.  If that function doesn't use those
    // arguments after calling ArchDebuggerTrap() then the compiler is
    // free to not store them anywhere and, if we clobber their values
    // here, there's nowhere the debugger can look to get them.
    uint64_t rdi, rsi, rdx, rcx;
    asm volatile(
        "movq %%rdi, %[rdi];\n"
        "movq %%rsi, %[rsi];\n"
        "movq %%rdx, %[rdx];\n"
        "movq %%rcx, %[rcx];\n"
        : [rdi] "=m" (rdi), [rsi] "=m" (rsi), [rdx] "=m" (rdx), [rcx] "=m" (rcx)
        : // input
        : // clobbered
        );
#endif

    // Initialize once.
    static InitPosix initPosix;

#if defined(ARCH_CPU_INTEL) && defined(ARCH_BITS_64)
    // Restore the saved registers.
    asm volatile(
        "movq %[rdi], %%rdi;\n"
        "movq %[rsi], %%rsi;\n"
        "movq %[rdx], %%rdx;\n"
        "movq %[rcx], %%rcx;\n"
        : // output
        : [rdi] "m" (rdi), [rsi] "m" (rsi), [rdx] "m" (rdx), [rcx] "m" (rcx)
        : // clobbered
        );
#endif

#elif defined(ARCH_OS_WINDOWS)

    _archDebuggerInitialized = true;
    _archDebuggerEnabled = true;

#endif
}


#if defined(ARCH_OS_LINUX) || defined(ARCH_OS_DARWIN)
// Use a 'non-locking' fork so that we won't get hung up if we've
// had malloc corruption.  We can't prevent fork() from using the
// heap, unfortunately, since fork handlers can do whatever they
// want.  May want to use clone() on Linux or something similar.
static int
nonLockingFork()
{
    typedef int (*ForkFunc)(void);
    extern ForkFunc Arch_nonLockingFork;

    if (Arch_nonLockingFork != NULL) {
        return (Arch_nonLockingFork)();
    }
    else {
        return fork();
    }
}

// This is like fork() except the new process will have the init process as
// its parent and it will not and cannot have a controlling terminal.  We
// go through a fairly typical daemonize process except we don't exit the
// original process and we wait for the callback in the new process to exec
// or return.
bool
Arch_DebuggerRunUnrelatedProcessPosix(bool (*cb)(void*), void* data)
{
    // Do *not* use the heap in here.  Avoid using any functions except
    // system calls.

    // Open a pipe for communicating success failure from descendant
    // processes.
    int ready[2];
    if (pipe(ready) == -1) {
        return false;
    }

    // Fork so the child is not a process group leader and is in the
    // background.
    pid_t pid = nonLockingFork();
    if (pid == -1) {
        // fork failed!
        close(ready[0]);
        close(ready[1]);
        return false;
    }

    if (pid > 0) {
        // This is the parent.
        close(ready[1]);

        // Wait for the descendant to report status. We could collect the
        // entire result but we only care if we get any data or not.
        int result;
        ssize_t n = read(ready[0], &result, 1);
        while (n == -1) {
            n = read(ready[0], &result, 1);
        }

        // Done with pipe.
        close(ready[0]);

        // Success if descendant sent no data at all.
        return n == 0;
    }

    // This is the child.  Do *not* call exit() from here down.  We must
    // call _exit() to avoid running any atexit handlers.
    close(ready[0]);

    // Ensure that we cannot be stopped by ^Z
#if defined(SIGTTOU)
    signal(SIGTTOU, SIG_IGN);
#endif /* SIGTTOU */
#if defined(SIGTTIN)
    signal(SIGTTIN, SIG_IGN);
#endif /* SIGTTIN */
#if defined(SIGTTSTP)
    signal(SIGTTSTP, SIG_IGN);
#endif /* SIGTTSTP */

    // Create a new session and set this process as the process group
    // leader.  In addition, the process has no controlling terminal.
    if (setsid() == -1) {
        int result = errno;
        write(ready[1], &result, sizeof(result));
        _exit(1);
    }

    // Now we need to ensure that this process does not reacquire a
    // controlling terminal.  Typically, the first terminal opened by
    // a process that is the process group leader will become the
    // controlling terminal and will be inherited by all other processes
    // in the process group.  So we fork yet again to ensure that the
    // new child process is not the group leader.  Now it can never
    // acquire a controlling terminal.
    //
    // When the process group leader process exits, all other processes
    // in the group get a SIGHUP.  Since we don't want to die when our
    // newly created process group leader exits, we have to ignore SIGHUP.
    //
    // Note: BSD systems use ioctl(fd, TIOCNOTTY, NULL) to disable
    // controlling terminals, but macOS appears to have a working
    // setsid() call so I've used it.
    //
    signal(SIGHUP, SIG_IGN);

    pid = nonLockingFork();
    if (pid == -1) {
        // fork failed!
        int result = errno;
        write(ready[1], &result, sizeof(result));
        _exit(2);
    }
    if (pid > 0) {
        // This is the parent.
        _exit(0);
    }

    // Now we are in the grandchild process.  We are not a process group
    // leader, a session leader, and we have no controlling terminal.

    // Close all open file descriptors
    int result = ArchCloseAllFiles(1, &ready[1]);
    if (result == -1) {
        write(ready[1], &result, sizeof(result));
        _exit(3);
    }

    // Change directory to root to make sure that we are not on
    // a mounted file system.  We especially want to avoid being
    // in automounted file systems which may try to go away.
    //
    result = chdir("/");
    if (result == -1) {
        write(ready[1], &result, sizeof(result));
        _exit(4);
    }

    // Clear any inherited umask.
    umask(0);

    // Open stdin, stdout, stderr.
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_WRONLY);

    // Arrange for the ready pipe to close on exec.
    long arg = FD_CLOEXEC;
    if (fcntl(ready[1], F_SETFD, arg) == -1) {
        // We can't close on exec so we can't indicate success of exec.
        int result = errno;
        write(ready[1], &result, sizeof(result));
        _exit(5);
    }

    // Invoke callback.  If this calls execve() then ready[1] will close
    // automatically without us writing to it, indicating success.
    if (!cb(data)) {
        result = errno;
        write(ready[1], &result, sizeof(result));
        _exit(6);
    }

    // Success
    _exit(0);
}

static
bool
Arch_DebuggerAttachExecPosix(void* data)
{
    char** args = (char**)data;
    execve(args[0], args, ArchEnviron());
    return false;
}

#endif // defined(ARCH_OS_LINUX) || defined(ARCH_OS_DARWIN)

#if defined(ARCH_OS_LINUX)

// Reads /proc/self/status, finds the line starting with "field:", and
// returns the portion following the ":".
// Note that the returned string will generally include leading whitespace
static
std::string Arch_ReadProcStatusField(const std::string_view field)
{
    std::ifstream procStatusFile("/proc/self/status");
    if (!procStatusFile) {
        ARCH_WARNING("Unable to open /proc/self/status");
        return std::string();
    }
    for (std::string line; std::getline(procStatusFile, line);) {
        // the field needs to start with the given fieldLen AND the ':' char
        if (line.size() < field.size() + 1) {
            continue;
        }

        if (line.compare(0, field.size(), field) == 0 &&
            line[field.size()] == ':') {
            // We found our "field:" line
            return line.substr(field.size() + 1);
        }
    }

    ARCH_WARNING((std::string("Unable to find given field in "
                              "/proc/self/status: ") += field).c_str());
    return std::string();
}

// Reads the "TracerPid:" field from /proc/self/status
// Returns a result < 0 if there was an error.
static
int Arch_ReadTracerPid() {

    const std::string field = Arch_ReadProcStatusField("TracerPid");

    // Trim any leading spaces or tabs in a locale-independent way.
    char const *b = field.c_str();
    char const * const e = field.c_str() + field.size();
    while (b != e && (*b == '\t' || *b == ' ')) {
        ++b;
    }

    // Try to convert to int.
    int tracerPid = 0;
    auto [ptr, err] = std::from_chars(b, e, tracerPid);

    return err == std::errc() ? tracerPid : -1;
}

static
bool
Arch_DebuggerIsAttachedPosix()
{
    return Arch_ReadTracerPid() > 0;
}

#elif defined(ARCH_OS_DARWIN)

// From Apple:
//   https://developer.apple.com/library/content/qa/qa1361/_index.html
//
// Returns true if the current process is being debugged (either 
// running under the debugger or has a debugger attached post facto).
static bool
AmIBeingDebugged()
{
    int                 junk;
    int                 mib[4];
    struct kinfo_proc   info;
    size_t              size;

    // Initialize the flags so that, if sysctl fails for some bizarre 
    // reason, we get a predictable result.

    info.kp_proc.p_flag = 0;

    // Initialize mib, which tells sysctl the info we want, in this case
    // we're looking for information about a specific process ID.

    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_PID;
    mib[3] = getpid();

    // Call sysctl.

    size = sizeof(info);
    junk = sysctl(mib, sizeof(mib) / sizeof(*mib), &info, &size, NULL, 0);

    // We're being debugged if the P_TRACED flag is set.

    return junk == 0 && ( (info.kp_proc.p_flag & P_TRACED) != 0 );
}

#endif // defined(ARCH_OS_LINUX)

static
bool
Arch_DebuggerAttach()
{
    // Be very careful here to avoid using the heap.  We're not even sure
    // the stack is available but there's only so much we can do about that.

    // We assume Arch_DebuggerInit() has been called.
    if (!_archDebuggerEnabled) {
        return false;
    }

#if defined(ARCH_OS_LINUX) || defined(ARCH_OS_DARWIN)

    // To attach to gdb under Unix/Linux and Gnome:
    //   ARCH_DEBUGGER="gnome-terminal -e 'gdb -p %p'"
    //
    // To attach to TotalView:
    //   ARCH_DEBUGGER="totalview -pid %p %e"
    //
    // You can alternatively use:
    //   ARCH_DEBUGGER="totalview -e 'dset TV::dll_read_loader_symbols_only *' -pid %p %e"
    // to prevent TotalView from loading all debug symbols immediately.
    // You can achieve the same in gdb using the .gdbinit file or another
    // file with the -x option.
    //
    // Note that, as of this writing, TotalView does not notice stops due to
    // ArchDebuggerTrap() and there appears to be no way to fix that.  The
    // debugged program, however, does stop so users simply need to click
    // TotalView's pause button to see the program state.  Unfortunately,
    // there's no obvious indication that the program has stopped.
    //
    // To attach to lldb on Darwin:
    //   ARCH_DEBUGGER='osascript -e "tell application \"Terminal\"" -e "activate" -e "set newTab to do script(\"lldb -p %p\")" -e "end tell"'
    // This will bring up lldb in a (new) terminal window.  If your system
    // has System Integrity Protection then this won't work but there's a
    // workaround:  make a copy of Terminal (in /Applications/Utilities);
    // put it in /Applications with a new name, say, TerminalCopy;  then
    // use that new name in the environment variable above instead of
    // "Terminal".

    if (_archDebuggerAttachArgs) {
        // We need to start a process unrelated to this process so the
        // debugger's parent process is init, not this process (you can't
        // debug an ancestor process).
        if (Arch_DebuggerRunUnrelatedProcessPosix(
                    Arch_DebuggerAttachExecPosix, _archDebuggerAttachArgs)) {
            // Give the debugger a chance to attach.  We have no way of
            // blocking to wait for that and we can't be sure the client
            // is even going to start a debugger so we simply sleep for
            // what we hope is long enough.
            sleep(5);
            return true;
        }
    }

#elif defined(ARCH_OS_WINDOWS)
    DebugBreak();
#endif

    return false;
}


// Do initialization now that would require heap/stack when attaching.
ARCH_HIDDEN
void
Arch_InitDebuggerAttach()
{
#if defined(ARCH_OS_LINUX) || defined(ARCH_OS_DARWIN)
    // Maximum length of a pid written as a decimal.  It's okay for this
    // to be greater than that.
    static const size_t _decimalPidLength = 20;

    // Parse the contents of the ARCH_DEBUGGER environment variable and
    // store the result.  This way we can avoid using the heap or tricky
    // stack allocations when launching the debugger.
    char* e = getenv("ARCH_DEBUGGER");
    if (e && e[0]) {
        std::string link;

        // Compute the length of the string.
        size_t n = 0;
        for (char* i = e; *i; ++i) {
            if (i[0] == '%' && i[1] == 'p') {
                n += _decimalPidLength;
                ++i;
            }
            else if (i[0] == '%' && i[1] == 'e') {
                // Get the symlink in the proc filesystem if we haven't
                // yet.
                if (link.empty()) {
                    link = ArchGetExecutablePath();
                }

                n += link.size();
                ++i;
            }
            else {
                ++n;
            }
        }

        // Build the argv array.
        _archDebuggerAttachArgs = (char**)malloc(4 * sizeof(char*));
        _archDebuggerAttachArgs[0] = strdup("/bin/sh");
        _archDebuggerAttachArgs[1] = strdup("-c");
        _archDebuggerAttachArgs[2] = (char*)malloc(n + 1);
        _archDebuggerAttachArgs[3] = NULL;

        // Build the command string.
        char* a = _archDebuggerAttachArgs[2];
        for (char* i = e; *i; ++i) {
            if (i[0] == '%' && i[1] == 'p') {
                // Write the process id.
                sprintf(a, "%d", (int)getpid());

                // Skip past the written process id.
                while (*a) {
                    ++a;
                }

                // Skip over the '%p'.
                ++i;
            }
            else if (i[0] == '%' && i[1] == 'e') {
                // Write the executable path
                strcat(a, link.c_str());

                // Skip past the written executable path
                a += link.size();

                // Skip over the '%e'.
                ++i;
            }
            else {
                // Copy the character.
                *a++ = *i;
            }
        }

        // Terminate the command string.
        *a = '\0';
    }
#elif defined(ARCH_OS_WINDOWS)
    // If ARCH_DEBUGGER is in the environment then enable attaching.
    if (getenv("ARCH_DEBUGGER")) {
        _archDebuggerAttachArgs = (char**)&_archDebuggerAttachArgs;
    }
#endif
}

void
ArchDebuggerTrap()
{
    // Trap if a debugger is attached or we try and fail to attach one.
    // If we attach one we assume it will automatically stop this process.
    if (ArchDebuggerIsAttached() || !Arch_DebuggerAttach()) {
    if (_archDebuggerEnabled) {
#if defined(ARCH_OS_WINDOWS)
            DebugBreak();
#elif defined(ARCH_CPU_INTEL)
            asm("int $3");
#elif defined(ARCH_OS_WASM_VM)
            emscripten_debugger();
#else
            raise(SIGTRAP);
#endif
    }
}
}

void
ArchDebuggerWait(bool wait)
{
    _archDebuggerWait = wait;
}

namespace {
bool
_ArchAvoidJIT()
{
    return (getenv("ARCH_AVOID_JIT") != nullptr);
}
}

bool
ArchDebuggerAttach()
{
    return !_ArchAvoidJIT() &&
            (ArchDebuggerIsAttached() || Arch_DebuggerAttach());
}

bool
ArchDebuggerIsAttached()
{
    Arch_DebuggerInit();
#if defined(ARCH_OS_WINDOWS)
    return IsDebuggerPresent() == TRUE;
#elif defined(ARCH_OS_DARWIN)
    return AmIBeingDebugged();
#elif defined(ARCH_OS_LINUX)
    return Arch_DebuggerIsAttachedPosix();
#endif
    return false;
}

void
ArchAbort(bool logging)
{
    if (!_ArchAvoidJIT() || ArchDebuggerIsAttached()) {
        if (!logging) {
#if !defined(ARCH_OS_WINDOWS) && !defined(ARCH_OS_WASM_VM)
            // Remove signal handler.
            struct sigaction act;
            act.sa_handler = SIG_DFL;
            act.sa_flags   = 0;
            sigemptyset(&act.sa_mask);
            sigaction(SIGABRT, &act, NULL);
#endif
        }

#if defined(ARCH_OS_WASM_VM)
        emscripten_force_exit(134);
#else
        abort();
#endif
    }

    // The exit code for abort() (128 + SIGABRT).
    #if defined(ARCH_OS_WASM_VM)
        emscripten_force_exit(134);
    #else
        _exit(134);
    #endif
}

PXR_NAMESPACE_CLOSE_SCOPE
