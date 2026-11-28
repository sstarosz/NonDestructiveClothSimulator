#pragma once
// What is kept to find out why a run of the program did not end as it should: a
// log of what it did, and the report made of it at the next start. Both stay on
// this computer.
//
//   ActionLog log(actionLogIn(run), "Twintailor 0.1.0");  // `run`: a folder that is there
//   log.note("Play");                                    // "   12.40 s  Play"
//   ...the program stops...
//   auto report = keepCrashReport(run, reports, "2026-11-28 12-30-05");
//
// A line is written before the thing is done, so after a crash the last one says
// what was being done. A line holds what was done, or an error's words: no name
// of a panel or of a file, but for what an error itself says.

#include <chrono>
#include <cstddef>
#include <deque>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <typeinfo>

namespace twintailor::session {

class ActionLog {
public:
    // Begins a log in `file` (UTF-8): `heading` is its first line and stays;
    // after it come the latest lines, at least `kept` of them.
    ActionLog(std::string file, std::string heading, std::size_t kept = 500);

    // Adds a line, with the seconds since the log began; line breaks become
    // spaces. What cannot be written is dropped: a log never stops the program.
    void note(std::string_view what);
    const std::string& file() const { return file_; }

private:
    void startAgain();  // the file with the heading and the lines that are kept

    std::string file_, heading_;
    std::size_t kept_;
    std::deque<std::string> lines_;  // the latest `kept_`
    std::size_t written_ = 0;        // lines in the file after the heading
    std::ofstream out_;
    std::chrono::steady_clock::time_point began_ = std::chrono::steady_clock::now();
};

// A type's name without its namespaces, for the log: "MoveCorners".
std::string_view plainName(const std::type_info& type);

// The log's file in the folder of a run.
std::string actionLogIn(const std::string& run);

// For the folder of a run that has stopped. If it left a log, it did not end as
// it should: the log is moved into a new folder under `reports`, called `name`,
// and the pattern as the run's recovery copy last had it, without its avatar,
// is put beside it. Of the reports there the last `kept` by name stay. Returns
// the new folder; empty: there was no log, or no report could be made.
std::optional<std::string> keepCrashReport(const std::string& run, const std::string& reports, const std::string& name, std::size_t kept = 5);

} // namespace twintailor::session
