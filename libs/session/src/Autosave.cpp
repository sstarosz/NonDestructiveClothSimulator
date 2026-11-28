#include "twintailor/session/Autosave.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "RecoveryFiles.h"

namespace twintailor::session {

namespace {

// The files of a copy: the pattern with its scene and the avatar, numbered, and
// whose they are. A copy without a number is one file that holds all of it, as
// earlier versions of the program left it.
constexpr std::string_view patternKind = "pattern-", avatarKind = "avatar-", ending = ".twintail";
std::string numbered(const std::string& folder, std::string_view kind, std::uint64_t number) {
    return folder + "/" + std::string(kind) + std::to_string(number) + std::string(ending);
}
std::string unnumberedIn(const std::string& folder) { return folder + "/pattern.twintail"; }
std::string aboutIn(const std::string& folder) { return folder + "/belongs-to.txt"; }

// A file of a numbered copy, by its name: "avatar-3.twintail", or one being
// saved, "avatar-3.twintail.saving".
struct CopyFile {
    std::filesystem::path path;
    std::uint64_t number;
    bool pattern;
    bool whole;  // not one being saved
};

std::vector<CopyFile> copyFilesIn(const std::string& folder) {
    std::vector<CopyFile> found;
    std::error_code problem;
    for (std::filesystem::directory_iterator at(filePath(folder), problem), end; !problem && at != end; at.increment(problem)) {
        const std::u8string utf8 = at->path().filename().u8string();
        std::string_view name(reinterpret_cast<const char*>(utf8.data()), utf8.size());
        const bool pattern = name.starts_with(patternKind);
        if (!pattern && !name.starts_with(avatarKind)) continue;
        name.remove_prefix(pattern ? patternKind.size() : avatarKind.size());
        std::uint64_t number = 0;
        const auto [rest, wrong] = std::from_chars(name.data(), name.data() + name.size(), number);
        if (wrong != std::errc{}) continue;
        const std::string_view after(rest, static_cast<std::size_t>(name.data() + name.size() - rest));
        if (!after.starts_with(ending)) continue;
        found.push_back({at->path(), number, pattern, after == ending});
    }
    return found;
}

// The number of the newest copy that is whole: its pattern is written last.
std::optional<std::uint64_t> newestIn(const std::string& folder) {
    std::optional<std::uint64_t> newest;
    for (const CopyFile& file : copyFilesIn(folder))
        if (file.pattern && file.whole && (!newest || file.number > *newest)) newest = file.number;
    return newest;
}

// Removes every file of every copy, but those of the copy `kept`.
void removeCopies(const std::string& folder, std::optional<std::uint64_t> kept) {
    std::error_code ignored;
    for (const CopyFile& file : copyFilesIn(folder))
        if (!kept || file.number != *kept) std::filesystem::remove(file.path, ignored);
    for (const std::string& file : {unnumberedIn(folder), unnumberedIn(folder) + ".saving"}) std::filesystem::remove(filePath(file), ignored);
}

std::expected<SavedScene, std::string> read(const std::string& file) {
    std::variant<SavedScene, FileError> opened = twintailor::open(file);
    if (const FileError* problem = std::get_if<FileError>(&opened))
        return std::unexpected(problem->line ? "line " + std::to_string(problem->line) + ": " + problem->message : problem->message);
    return std::move(std::get<SavedScene>(opened));
}

}  // namespace

std::optional<std::filesystem::path> newestPatternIn(const std::string& folder) {
    if (const std::optional<std::uint64_t> number = newestIn(folder)) return filePath(numbered(folder, patternKind, *number));
    std::error_code ignored;
    if (std::filesystem::exists(filePath(unnumberedIn(folder)), ignored)) return filePath(unnumberedIn(folder));
    return std::nullopt;
}

bool hasRecovery(const std::string& folder) {
    return newestPatternIn(folder).has_value();
}

std::expected<Recovery, std::string> readRecovery(const std::string& folder) {
    const std::optional<std::uint64_t> number = newestIn(folder);
    std::expected<SavedScene, std::string> pattern = read(number ? numbered(folder, patternKind, *number) : unnumberedIn(folder));
    if (!pattern) return std::unexpected(pattern.error());
    if (number) {
        const std::expected<SavedScene, std::string> avatar = read(numbered(folder, avatarKind, *number));
        if (!avatar) return std::unexpected("its avatar: " + avatar.error());
        pattern->document.setAvatar(avatar->document.avatar());
    }
    Recovery found{.title = {}, .path = {}, .scene = std::move(*pattern)};
    std::ifstream about(filePath(aboutIn(folder)));
    std::getline(about, found.title);
    std::getline(about, found.path);
    if (found.title.empty()) found.title = "untitled";
    return found;
}

Autosave::Autosave(std::string folder, double waitSeconds) : folder_(std::move(folder)), wait_(waitSeconds) {
    for (const CopyFile& file : copyFilesIn(folder_)) numbers_ = std::max(numbers_, file.number);
}

Autosave::~Autosave() {
    if (writing_.valid()) writing_.wait();
}

std::optional<std::string> Autosave::update(const Session& session, double now, const std::string& title, const std::string& path) {
    std::optional<std::string> problem = std::exchange(toTell_, std::nullopt);
    if (writing_.valid() && writing_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        if (std::optional<std::string> failed = finished()) problem = std::move(failed);
    if (std::exchange(failed_, false)) retryAt_ = now + wait_;
    if (!session.unsaved()) {
        // A copy that is being written goes once it is: no frame waits for it.
        if (kept_ && !writing_.valid()) forget();
        changedAt_.reset();
        return problem;
    }
    // The cloth in motion is no change by itself: what it was at rest, before, is worth more than one step on.
    if (!session.simulating() && !session.pulling()) atRest_ = session.sceneVersion();
    if (copied_ && copied_->document == session.editor().version() && copied_->scene >= atRest_) {
        changedAt_.reset();
        return problem;
    }
    if (!changedAt_) changedAt_ = now;
    if (writing_.valid() || (retryAt_ && now < *retryAt_)) return problem;
    if (session.editor().unfinished() && now - *changedAt_ < wait_) return problem;
    write(session, title, path);
    return problem;
}

void Autosave::write(const Session& session, const std::string& title, const std::string& path) {
    if (writing_.valid())
        if (std::optional<std::string> failed = finished()) toTell_ = std::move(failed);
    copied_ = Versions{session.editor().version(), session.sceneVersion()};
    changedAt_.reset();
    retryAt_.reset();
    failed_ = false;
    kept_ = true;
    const std::shared_ptr<const Avatar> avatar = session.document().avatar();
    std::error_code ignored;
    // Also when the file has gone: a pattern without its avatar cannot be read.
    writingAvatar_ = !avatar_ || avatar_->avatar != avatar || !std::filesystem::exists(filePath(numbered(folder_, avatarKind, avatar_->number)), ignored);
    if (writingAvatar_) avatar_ = AvatarFile{++numbers_, avatar};
    Document pattern = session.document();
    pattern.setAvatar(nullptr);  // it has a file of its own
    // The work has its own copies; the avatar it shares with them is never changed.
    writing_ = std::async(std::launch::async,
                          [folder = folder_, pattern = std::move(pattern), state = session.state(), avatar, another = writingAvatar_,
                           number = avatar_->number, title, path]() -> std::string {
                              // Whatever goes wrong is the answer: an error left in the future would come out wherever that is asked.
                              try {
                                  // Whose it is first: a pattern with nothing beside it would come back without its name.
                                  std::ofstream about(filePath(aboutIn(folder)), std::ios::trunc);
                                  about << title << '\n' << path << '\n';
                                  about.close();
                                  if (!about) return "could not write into \"" + folder + "\"";
                                  // Then the avatar, so that a pattern is never there without the one it belongs to.
                                  if (another) {
                                      Document alone;
                                      alone.setAvatar(avatar);
                                      if (std::string failed = save(alone, numbered(folder, avatarKind, number)); !failed.empty()) return failed;
                                  }
                                  std::string failed = save(pattern, numbered(folder, patternKind, number), state);
                                  if (another) {
                                      std::error_code ignored;
                                      // A new avatar file with no pattern is of no use, and may be large.
                                      if (!failed.empty()) std::filesystem::remove(filePath(numbered(folder, avatarKind, number)), ignored);
                                      else removeCopies(folder, number);
                                  }
                                  return failed;
                              } catch (const std::exception& problem) {
                                  return problem.what();
                              }
                          });
}

std::expected<void, std::string> Autosave::writeNow(const Session& session, const std::string& title, const std::string& path) {
    write(session, title, path);
    const std::string failed = result();
    if (failed.empty()) return {};
    return std::unexpected(failed);
}

void Autosave::forget() {
    if (writing_.valid()) writing_.get();
    removeCopies(folder_, std::nullopt);
    std::error_code ignored;
    std::filesystem::remove(filePath(aboutIn(folder_)), ignored);
    copied_.reset();
    avatar_.reset();
    changedAt_.reset();
    retryAt_.reset();
    toTell_.reset();
    failed_ = false;
    kept_ = false;
    told_ = false;
}

std::optional<std::string> Autosave::wait() {
    if (!writing_.valid()) return std::exchange(toTell_, std::nullopt);
    return finished();
}

// What became of the copy that was being written, empty if it is written. One
// that failed is as if it had never been begun.
std::string Autosave::result() {
    std::string failed = writing_.get();
    if (!failed.empty()) {
        copied_.reset();
        if (writingAvatar_) avatar_.reset();
        failed_ = true;
    }
    return failed;
}

// The same, in words for the first failure.
std::optional<std::string> Autosave::finished() {
    const std::string failed = result();
    if (failed.empty()) {
        told_ = false;
        return std::nullopt;
    }
    if (std::exchange(told_, true)) return std::nullopt;
    return "The recovery copy could not be written (" + failed + "). Save your work.";
}

}  // namespace twintailor::session
