#pragma once
// The files of a recovery copy (Autosave.cpp), for what else reads them.

#include <filesystem>
#include <optional>
#include <string>

namespace twintailor::session {

// The pattern's file of the newest whole copy in `folder`; empty: there is none.
std::optional<std::filesystem::path> newestPatternIn(const std::string& folder);

} // namespace twintailor::session
