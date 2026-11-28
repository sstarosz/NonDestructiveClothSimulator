// Integration tests of the crash dump (libs/session, CrashDump.cpp): a program
// of its own crashes on purpose (CrashingProgram.cpp), and the dump it leaves is
// read as a debugger reads it, with the symbols made beside that program.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// After windows.h, which it needs.
#include <dbghelp.h>

#include <catch2/catch_test_macros.hpp>

#include "Expect.h"
#include "twintailor/core/File.h"
#include "twintailor/session/CrashDump.h"

using namespace twintailor;
using namespace twintailor::session;

namespace {

std::string emptyFolder(const char* name) {
    const std::filesystem::path folder = filePath(TWINTAILOR_TEST_FOLDER) / name;
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const std::u8string utf8 = folder.generic_u8string();
    return {utf8.begin(), utf8.end()};
}

// Runs the program that crashes; what it ended with.
int crash(const std::string& dump, const char* how) {
    const std::string command = std::string("\"\"") + TWINTAILOR_CRASHING_PROGRAM + "\" \"" + dump + "\" " + how + "\"";
    return std::system(command.c_str());
}

// What a dump says of the crash: which exception, and the source file the
// symbols give for the place it happened, empty if it is not in the program.
struct Read {
    DWORD code = 0;
    std::wstring source;
};

std::optional<Read> read(const std::string& file) {
    std::ifstream in(filePath(file), std::ios::binary);
    const std::string bytes(std::istreambuf_iterator<char>(in), {});
    if (bytes.size() < 4 || bytes.compare(0, 4, "MDMP") != 0) return std::nullopt;
    void* const base = const_cast<char*>(bytes.data());
    const auto stream = [&](ULONG kind) -> void* {
        MINIDUMP_DIRECTORY* directory = nullptr;
        void* found = nullptr;
        ULONG size = 0;
        return MiniDumpReadDumpStream(base, kind, &directory, &found, &size) ? found : nullptr;
    };
    const auto* exception = static_cast<const MINIDUMP_EXCEPTION_STREAM*>(stream(ExceptionStream));
    const auto* modules = static_cast<const MINIDUMP_MODULE_LIST*>(stream(ModuleListStream));
    if (!exception || !modules) return std::nullopt;
    Read read{.code = exception->ExceptionRecord.ExceptionCode, .source = {}};

    const DWORD64 at = exception->ExceptionRecord.ExceptionAddress;
    const std::filesystem::path program = filePath(TWINTAILOR_CRASHING_PROGRAM);
    for (ULONG32 i = 0; i < modules->NumberOfModules; ++i) {
        const MINIDUMP_MODULE& module = modules->Modules[i];
        if (at < module.BaseOfImage || at >= module.BaseOfImage + module.SizeOfImage) continue;
        const HANDLE session = reinterpret_cast<HANDLE>(1);  // any number that names this look at symbols
        SymSetOptions(SYMOPT_LOAD_LINES);
        if (!SymInitializeW(session, program.parent_path().c_str(), FALSE)) break;
        if (SymLoadModuleExW(session, nullptr, program.c_str(), nullptr, module.BaseOfImage, module.SizeOfImage, nullptr, 0)) {
            IMAGEHLP_LINEW64 line{};
            line.SizeOfStruct = sizeof line;
            DWORD into = 0;
            if (SymGetLineFromAddrW64(session, at, &into, &line)) read.source = line.FileName;
        }
        SymCleanup(session);
        break;
    }
    return read;
}

} // namespace

TEST_CASE("a crash writes a dump, and the symbols lead from it to the source file")
{
    const std::string file = crashDumpIn(emptyFolder("memory"));
    EXPECT((crash(file, "memory") != 0), "the program still ends as a crashed one");
    const std::optional<Read> found = read(file);
    REQUIRE(found);
    EXPECT((found->code == EXCEPTION_ACCESS_VIOLATION), "of memory used wrongly");
    EXPECT((found->source.ends_with(L"CrashingProgram.cpp")), "where it was, by the symbols");
}

TEST_CASE("an abort, an exception nobody catches on a thread, a full stack and a refused argument write a dump too")
{
    const std::string folder = emptyFolder("others");
    for (const char* how : {"abort", "thread", "stack", "argument"}) {
        const std::string file = folder + "/" + how + ".dmp";
        INFO(how);
        EXPECT((crash(file, how) != 0), "it ends as a crashed program");
        const std::optional<Read> found = read(file);
        EXPECT((found.has_value()), "with a dump that can be read");
        if (found && std::string(how) == "stack") EXPECT((found->code == EXCEPTION_STACK_OVERFLOW), "of a full stack");
    }
}

TEST_CASE("no dump without a crash, or without a place for it")
{
    const std::string file = crashDumpIn(emptyFolder("none"));
    EXPECT((crash(file, "nothing") == 0 && !std::filesystem::exists(filePath(file))), "a program that ends well leaves none");
    EXPECT((crash("-", "memory") != 0 && !std::filesystem::exists(filePath(file))), "nor one that was given no place");
}
