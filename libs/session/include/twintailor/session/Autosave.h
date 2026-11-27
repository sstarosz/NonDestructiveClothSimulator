#pragma once
// A recovery copy of unsaved work: while there is some, it is written now and
// then into a folder of its own, so that a crash loses little. The pattern's
// own file is never touched.
//
//   Autosave autosave(folder);                        // UTF-8, there already, used by no one else
//   autosave.update(session, seconds, title, path);   // each frame
//   autosave.forget();                                // saved, or thrown away
//   ...
//   auto copy = readRecovery(folder);                 // what a run that stopped left there
//
// The copy is written on a thread of its own: no frame waits for it. It stays
// when the Autosave goes; only forget() and saved work take it away.

#include <cstdint>
#include <expected>
#include <future>
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
    explicit Autosave(std::string folder, double everySeconds = 30.0);
    ~Autosave();  // waits for a copy that is being written

    // Writes a copy when one is due: the work has been unsaved for the given
    // time, and has changed since the last copy. Work that is saved (or undone
    // back to what is) takes the copy away. `now` is in seconds, from any
    // start. Returns why the last copy could not be written, the first time.
    std::optional<std::string> update(const Session& session, double now, const std::string& title, const std::string& path);
    // A copy of the work as it is, whatever the time.
    void write(const Session& session, const std::string& title, const std::string& path);
    // The same, on disk before this returns; or why it could not be written,
    // every time. For when there is no later: the program is about to end.
    std::expected<void, std::string> writeNow(const Session& session, const std::string& title, const std::string& path);
    // No copy: the work is saved, or thrown away.
    void forget();
    // Until no copy is being written. Returns why it could not be, if so.
    std::optional<std::string> wait();

private:
    std::optional<std::string> finished();

    std::string folder_;
    double every_;
    std::optional<double> since_;  // when the work became unsaved, or was last copied
    struct Versions {
        std::uint64_t document, scene;
        friend bool operator==(const Versions&, const Versions&) = default;
    };
    std::optional<Versions> copied_;  // what the copy in the folder is of
    std::future<std::string> writing_;
    bool told_ = false;  // a failure has been reported, and no copy has been written since
};

} // namespace twintailor::session
