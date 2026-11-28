#pragma once
// A crash that is no error the program raises (memory used wrongly, an abort)
// writes a minidump: opened in Visual Studio with the release's debug symbols,
// it shows where the program was. Windows only; elsewhere these do nothing.
//
//   catchCrashes();                          // once, as the program starts
//   writeCrashDumpTo(crashDumpIn(run));      // UTF-8; empty: write none
//
// Both are for the whole process: Windows hands whoever catches a crash nothing
// of the caller's, so where the dump goes is kept here.

#include <string>

namespace twintailor::session {

// From now on a crash of this process writes a dump, the first time only, where
// writeCrashDumpTo says; Windows then ends the program as it would have.
void catchCrashes();

// Where a crash from now on writes its dump; empty: nowhere.
void writeCrashDumpTo(const std::string& file);

// The dump's file in the folder of a run.
std::string crashDumpIn(const std::string& run);

} // namespace twintailor::session
