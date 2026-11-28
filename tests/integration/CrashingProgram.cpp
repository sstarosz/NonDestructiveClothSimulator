// A program that crashes on purpose, in one of the ways programs do, for
// CrashDumpTests.cpp:
//
//   twintailor_crashing_program <where the dump goes, or "-"> <how>

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "twintailor/session/CrashDump.h"

namespace {

// Uses up the stack: each call keeps a block that the next one is added to.
int deeper(int depth) {
    volatile char kept[4096];
    kept[0] = static_cast<char>(depth);
    if (depth < 0) return 0;  // never: said so that the compiler lets the calls be
    return deeper(depth + 1) + kept[0];
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    // No window of Windows' own about the crash, which would wait for someone.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    twintailor::session::catchCrashes();
    if (std::string_view(argv[1]) != "-") twintailor::session::writeCrashDumpTo(argv[1]);

    const std::string_view how = argv[2];
    if (how == "memory") {
        volatile int* volatile nowhere = nullptr;
        *nowhere = 1;
    } else if (how == "thread") {
        std::thread([] { throw std::runtime_error("nobody catches this"); }).join();
    } else if (how == "abort") {
        std::abort();
    } else if (how == "stack") {
        return deeper(argc);
    } else if (how == "argument") {
        char two[2];
        strcpy_s(two, sizeof two, argv[2]);  // far too long: the C runtime refuses it
    }
    return 0;  // "nothing"
}
