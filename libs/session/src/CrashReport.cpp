#include "twintailor/session/CrashReport.h"

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <system_error>
#include <utility>
#include <vector>

#include "RecoveryFiles.h"
#include "twintailor/core/File.h"
#include "twintailor/session/CrashDump.h"

namespace twintailor::session {

ActionLog::ActionLog(std::string file, std::string heading, std::size_t kept) : file_(std::move(file)), heading_(std::move(heading)), kept_(kept) {
    startAgain();
}

void ActionLog::startAgain() {
    // Written beside the log and put in its place, so that a stop in between leaves the one before.
    const std::filesystem::path file = filePath(file_), beside = filePath(file_ + ".new");
    out_.close();
    {
        std::ofstream fresh(beside, std::ios::trunc);
        fresh << heading_ << '\n';
        for (const std::string& line : lines_) fresh << line << '\n';
    }
    std::error_code ignored;
    std::filesystem::rename(beside, file, ignored);
    out_.clear();
    out_.open(file, std::ios::app);
    written_ = lines_.size();
}

void ActionLog::note(std::string_view what) {
    try {
        const std::chrono::duration<double> after = std::chrono::steady_clock::now() - began_;
        std::string line = std::format("{:9.2f} s  {}", after.count(), what);
        std::replace(line.begin(), line.end(), '\n', ' ');
        std::replace(line.begin(), line.end(), '\r', ' ');
        lines_.push_back(std::move(line));
        if (lines_.size() > kept_) lines_.pop_front();
        if (written_ >= 2 * kept_) {
            startAgain();
            return;
        }
        // Handed to the system at once: it is there though the program stops right after.
        out_ << lines_.back() << std::endl;
        ++written_;
    } catch (const std::exception&) {  // out of memory, most likely: the program goes on without the line
    }
}

std::string_view plainName(const std::type_info& type) {
    const std::string_view full = type.name();
    const std::size_t last = full.find_last_of(": ");
    return last == std::string_view::npos ? full : full.substr(last + 1);
}

std::string actionLogIn(const std::string& run) { return run + "/actions.log"; }

std::optional<std::string> keepCrashReport(const std::string& run, const std::string& reports, const std::string& name, std::size_t kept) {
    std::error_code problem;
    // What a run leaves that did not end as it should, in whichever folder it is.
    const auto leftIn = [](const std::string& folder) { return std::array{filePath(actionLogIn(folder)), filePath(crashDumpIn(folder))}; };
    const std::array left = leftIn(run);
    const auto any = [&](const std::array<std::filesystem::path, 2>& files) {
        return std::any_of(files.begin(), files.end(), [&](const std::filesystem::path& file) { return std::filesystem::exists(file, problem); });
    };
    if (!any(left)) return std::nullopt;

    std::string folder = reports + "/" + name;
    for (int again = 2; std::filesystem::exists(filePath(folder), problem); ++again) folder = reports + "/" + name + " (" + std::to_string(again) + ")";
    std::filesystem::create_directories(filePath(folder), problem);
    if (problem) return std::nullopt;
    std::error_code ignored;
    const std::array moved = leftIn(folder);
    for (std::size_t i = 0; i < left.size(); ++i)
        if (std::filesystem::exists(left[i], ignored)) std::filesystem::rename(left[i], moved[i], ignored);
    if (!any(moved)) return std::nullopt;  // both stay where they are
    if (const std::optional<std::filesystem::path> pattern = newestPatternIn(run))
        std::filesystem::copy_file(*pattern, filePath(folder + "/pattern.twintail"), ignored);

    // Only folders that hold a log or a dump are reports; anything else there is left alone.
    std::vector<std::filesystem::path> made;
    for (std::filesystem::directory_iterator at(filePath(reports), problem), end; !problem && at != end; at.increment(problem)) {
        const std::u8string utf8 = at->path().u8string();
        if (any(leftIn({utf8.begin(), utf8.end()}))) made.push_back(at->path());
    }
    std::sort(made.begin(), made.end(), std::greater<>());
    for (std::size_t i = kept; i < made.size(); ++i) std::filesystem::remove_all(made[i], ignored);
    return folder;
}

} // namespace twintailor::session
