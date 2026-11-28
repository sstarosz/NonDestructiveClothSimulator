#include "twintailor/session/CrashDump.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// After windows.h, which it needs.
#include <dbghelp.h>
#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iterator>

#include "twintailor/core/File.h"
#endif

namespace twintailor::session {

std::string crashDumpIn(const std::string& run) { return run + "/crash.dmp"; }

#ifdef _WIN32

namespace {

// Kept for the whole process (see the header), and made without allocating, which
// a crash may not be able to do.
wchar_t dumpPath[2048] = {};            // empty: nowhere
std::atomic<DWORD> crashedThread = 0;  // the first one to crash; 0: none yet

struct Crash {
    EXCEPTION_POINTERS* what;
    DWORD thread;
};

DWORD WINAPI writeDump(void* given) {
    const Crash& crash = *static_cast<const Crash*>(given);
    const HANDLE file = CreateFileW(dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return 0;
    MINIDUMP_EXCEPTION_INFORMATION information{crash.thread, crash.what, FALSE};
    // The stacks and what they point at: enough to see where it was and with what, and not all of the memory.
    const auto kind = static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo);
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, kind, &information, nullptr, nullptr);
    CloseHandle(file);
    return 0;
}

void dump(EXCEPTION_POINTERS* what) {
    if (dumpPath[0] == L'\0') return;
    const DWORD here = GetCurrentThreadId();
    DWORD first = 0;
    if (!crashedThread.compare_exchange_strong(first, here)) {
        // Another thread's crash is being written: give it time before this one ends the program.
        if (first != here) Sleep(5000);
        return;
    }
    // Written by a thread of its own: the one that crashed may have no stack left.
    Crash crash{what, here};
    if (const HANDLE writer = CreateThread(nullptr, 0, writeDump, &crash, 0, nullptr)) {
        WaitForSingleObject(writer, 30000);
        CloseHandle(writer);
    }
}

LONG WINAPI onException(EXCEPTION_POINTERS* what) {
    dump(what);
    return EXCEPTION_CONTINUE_SEARCH;
}

// For an end that is no exception of Windows: the dump is of this very place.
void dumpHere() {
    CONTEXT context{};
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record{};
    record.ExceptionCode = 0xE0000001;  // this program's own: ended by the C runtime
    record.ExceptionAddress = _ReturnAddress();
    EXCEPTION_POINTERS here{&record, &context};
    dump(&here);
}

// abort() passes the exceptions by, and std::terminate ends in it.
void onAbort(int) { dumpHere(); }

// So does the C runtime when it refuses an argument, and a call of a pure
// virtual function. Both would end the program: they still do.
void onRefusedArgument(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, std::uintptr_t) {
    dumpHere();
    std::abort();
}

void onPureCall() {
    dumpHere();
    std::abort();
}

} // namespace

void catchCrashes() {
    // Room left on this thread's stack to catch its running out.
    ULONG room = 64 * 1024;
    SetThreadStackGuarantee(&room);
    SetUnhandledExceptionFilter(onException);
    std::signal(SIGABRT, onAbort);
    _set_invalid_parameter_handler(onRefusedArgument);
    _set_purecall_handler(onPureCall);
}

void writeCrashDumpTo(const std::string& file) {
    const std::wstring path = filePath(file).wstring();
    dumpPath[0] = L'\0';
    if (path.empty() || path.size() >= std::size(dumpPath)) return;
    std::copy(path.begin() + 1, path.end(), dumpPath + 1);
    dumpPath[path.size()] = L'\0';
    dumpPath[0] = path.front();  // last: a path half written is not used
}

#else

void catchCrashes() {}
void writeCrashDumpTo(const std::string&) {}

#endif

} // namespace twintailor::session
