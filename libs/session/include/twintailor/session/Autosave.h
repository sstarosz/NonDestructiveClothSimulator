#pragma once
// A recovery copy of unsaved work: it is written after every change, into a
// folder of its own, so that a crash loses little. The pattern's own file is
// never touched.
//
//   Autosave autosave(folder);                        // UTF-8, there already, used by no one else
//   autosave.update(session, seconds, title, path);   // each frame
//   autosave.forget();                                // saved, or thrown away
//   ...
//   auto copy = readRecovery(folder);                 // what a run that stopped left there
//
// A change is a command, a finished drag or typing, undo and redo, and what
// else a file would hold: what is hidden and frozen, the body's looks, and the
// cloth once it comes to rest. While the cloth runs or is pulled, only a
// change of the document writes a copy, with the cloth as it hangs then; the
// rest waits until it stops.
//
// The copy is two files, one for the pattern with its scene and one for the
// avatar, which can be many times as big as all the rest and is written only
// when it is another one. Their names carry a number, the same for two that
// belong together. The copy is written on a thread of its own, one at a time:
// no frame waits for it. It stays when the Autosave goes; only forget() and
// saved work take it away.

#include <cstdint>
#include <expected>
#include <future>
#include <memory>
#include <optional>
#include <string>

#include "twintailor/core/File.h"
#include "twintailor/session/Session.h"

namespace twintailor::session {

// The unsaved work as it was last copied.
struct Recovery {
    std::string title;  // the pattern's name, for people
    std::string path;   // its own file; empty if it never had one
    SavedScene scene;
};

bool hasRecovery(const std::string& folder);
// The copy in the folder, or why it cannot be read.
std::expected<Recovery, std::string> readRecovery(const std::string& folder);

class Autosave {
public:
    // `waitSeconds`: how long until a copy that could not be written is tried
    // again, and until what an unfinished edit has changed is copied all the same.
    explicit Autosave(std::string folder, double waitSeconds = 30.0);
    ~Autosave();  // waits for a copy that is being written

    // Writes a copy if the unsaved work has changed since the last one and none
    // is being written. Work that is saved (or undone back to what is) takes the
    // copy away. `now` is in seconds, from any start. Returns why a copy could
    // not be written, the first time.
    std::optional<std::string> update(const Session& session, double now, const std::string& title, const std::string& path);
    // A copy of the work as it is now.
    void write(const Session& session, const std::string& title, const std::string& path);
    // The same, on disk before this returns; or why it could not be written,
    // every time. For when there is no later: the program is about to end.
    std::expected<void, std::string> writeNow(const Session& session, const std::string& title, const std::string& path);
    // No copy: the work is saved, or thrown away.
    void forget();
    // Until no copy is being written. Returns why it could not be, if so.
    std::optional<std::string> wait();

private:
    // What became of the copy being written: result() says why it failed, or
    // nothing; finished() the same in words, for the first failure.
    std::string result();
    std::optional<std::string> finished();

    std::string folder_;
    double wait_;
    struct Versions {
        std::uint64_t document, scene;
    };
    std::optional<Versions> copied_;   // what the latest copy is of
    std::uint64_t atRest_ = 0;         // the scene's version when the cloth was last at rest
    std::optional<double> changedAt_;  // since when something has not been copied
    std::optional<double> retryAt_;    // after a failure: not before then
    // The avatar file of the latest copy. Empty: the next copy writes one.
    struct AvatarFile {
        std::uint64_t number;
        std::shared_ptr<const Avatar> avatar;  // none: the file says there is none
    };
    std::optional<AvatarFile> avatar_;
    std::uint64_t numbers_ = 0;  // the highest number used in the folder
    std::future<std::string> writing_;
    bool writingAvatar_ = false;         // the copy being written writes the avatar's file too
    bool failed_ = false;                // a copy has failed, and update() has not seen to it
    bool kept_ = false;                  // the folder may hold a copy of this Autosave's
    std::optional<std::string> toTell_;  // why one could not be written, for the next update()
    bool told_ = false;                  // a failure has been reported, and no copy has been written since
};

} // namespace twintailor::session
