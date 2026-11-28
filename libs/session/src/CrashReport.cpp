#include "twintailor/session/CrashReport.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <system_error>
#include <utility>
#include <vector>

#include "RecoveryFiles.h"
#include "twintailor/core/File.h"

namespace twintailor::session {

namespace {

constexpr const char* logName = "actions.log";

} // namespace

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

std::string actionLogIn(const std::string& run) { return run + "/" + logName; }

std::optional<std::string> keepCrashReport(const std::string& run, const std::string& reports, const std::string& name, std::size_t kept) {
    std::error_code problem;
    const std::filesystem::path log = filePath(actionLogIn(run));
    if (!std::filesystem::exists(log, problem)) return std::nullopt;

    std::string folder = reports + "/" + name;
    for (int again = 2; std::filesystem::exists(filePath(folder), problem); ++again) folder = reports + "/" + name + " (" + std::to_string(again) + ")";
    std::filesystem::create_directories(filePath(folder), problem);
    if (problem) return std::nullopt;
    std::filesystem::rename(log, filePath(actionLogIn(folder)), problem);
    if (problem) return std::nullopt;  // the log stays where it is, for the next start
    std::error_code ignored;
    if (const std::optional<std::filesystem::path> pattern = newestPatternIn(run))
        std::filesystem::copy_file(*pattern, filePath(folder + "/pattern.twintail"), ignored);

    // Only folders that hold a log are reports; anything else there is left alone.
    std::vector<std::filesystem::path> made;
    for (std::filesystem::directory_iterator at(filePath(reports), problem), end; !problem && at != end; at.increment(problem))
        if (std::filesystem::exists(at->path() / logName, ignored)) made.push_back(at->path());
    std::sort(made.begin(), made.end(), std::greater<>());
    for (std::size_t i = kept; i < made.size(); ++i) std::filesystem::remove_all(made[i], ignored);
    return folder;
}

} // namespace twintailor::session
