#pragma once
// Any change at all to a session's document, for a test that needs one no
// command makes. The program has no such way: it runs commands.
//
//   REQUIRE(tests::change(session, [&](Document& doc) { doc.remove(panel); }));

#include <expected>
#include <functional>
#include <string>

#include "twintailor/session/Session.h"

namespace twintailor::tests {

inline std::expected<void, std::string> change(session::Session& session, const std::function<void(Document&)>& verb, session::ChangeRules rules = {}) {
    // The session hands its editor out for reading only; the one it holds is not const.
    return const_cast<session::Editor&>(session.editor()).change(verb, rules);
}

} // namespace twintailor::tests
