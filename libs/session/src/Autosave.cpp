#include "clothcad/session/Autosave.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>
#include <variant>

namespace clothcad::session {

namespace {

// The two files of a copy: the pattern with its scene, and whose it is.
std::string patternIn(const std::string& folder) { return folder + "/pattern.clothcad"; }
std::string aboutIn(const std::string& folder) { return folder + "/belongs-to.txt"; }

}  // namespace

bool hasRecovery(const std::string& folder) {
    std::error_code ignored;
    return std::filesystem::exists(filePath(patternIn(folder)), ignored);
}

std::expected<Recovery, std::string> readRecovery(const std::string& folder) {
    std::variant<SavedScene, FileError> opened = clothcad::open(patternIn(folder));
    if (const FileError* problem = std::get_if<FileError>(&opened))
        return std::unexpected(problem->line ? "line " + std::to_string(problem->line) + ": " + problem->message : problem->message);
    Recovery found{.title = {}, .path = {}, .scene = std::move(std::get<SavedScene>(opened))};
    std::ifstream about(filePath(aboutIn(folder)));
    std::getline(about, found.title);
    std::getline(about, found.path);
    if (found.title.empty()) found.title = "untitled";
    return found;
}

Autosave::Autosave(std::string folder, double everySeconds) : folder_(std::move(folder)), every_(everySeconds) {}

Autosave::~Autosave() {
    if (writing_.valid()) writing_.wait();
}

std::optional<std::string> Autosave::update(const Session& session, double now, const std::string& title, const std::string& path) {
    std::optional<std::string> problem;
    if (writing_.valid() && writing_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) problem = finished();
    if (!session.unsaved()) {
        if (copied_) forget();
        since_.reset();
        return problem;
    }
    if (!since_) since_ = now;
    if (writing_.valid() || now - *since_ < every_) return problem;
    if (copied_ == Versions{session.editor().version(), session.sceneVersion()}) return problem;  // the copy is of this
    write(session, title, path);
    since_ = now;
    return problem;
}

void Autosave::write(const Session& session, const std::string& title, const std::string& path) {
    if (writing_.valid()) writing_.wait();
    copied_ = Versions{session.editor().version(), session.sceneVersion()};
    // The work has its own copies; the avatar it shares with them is never changed.
    writing_ = std::async(std::launch::async,
                          [folder = folder_, document = session.document(), state = session.state(), title, path]() -> std::string {
                              // Whatever goes wrong is the answer: an error left in the future would come out wherever that is asked.
                              try {
                                  // Whose it is first: a pattern with nothing beside it would come back without its name.
                                  std::ofstream about(filePath(aboutIn(folder)), std::ios::trunc);
                                  about << title << '\n' << path << '\n';
                                  about.close();
                                  if (!about) return "could not write into \"" + folder + "\"";
                                  return save(document, patternIn(folder), state);
                              } catch (const std::exception& problem) {
                                  return problem.what();
                              }
                          });
}

std::expected<void, std::string> Autosave::writeNow(const Session& session, const std::string& title, const std::string& path) {
    write(session, title, path);
    const std::string failed = writing_.get();
    if (failed.empty()) return {};
    copied_.reset();
    return std::unexpected(failed);
}

void Autosave::forget() {
    if (writing_.valid()) writing_.get();
    std::error_code ignored;
    for (const std::string& file : {patternIn(folder_), patternIn(folder_) + ".saving", aboutIn(folder_)}) std::filesystem::remove(filePath(file), ignored);
    copied_.reset();
    since_.reset();
    told_ = false;
}

std::optional<std::string> Autosave::wait() {
    if (!writing_.valid()) return std::nullopt;
    return finished();
}

// What became of the copy that was being written.
std::optional<std::string> Autosave::finished() {
    const std::string failed = writing_.get();
    if (failed.empty()) {
        told_ = false;
        return std::nullopt;
    }
    copied_.reset();  // there is none of this: tried again when due
    if (std::exchange(told_, true)) return std::nullopt;
    return "The recovery copy could not be written (" + failed + "). Save your work.";
}

}  // namespace clothcad::session
