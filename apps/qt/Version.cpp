#include "Version.h"

// TWINTAILOR_VERSION and TWINTAILOR_COMMIT are given to this file alone (see
// CMakeLists.txt), so that a new commit compiles nothing else again.

namespace twintailor::ui {

QString versionLine() {
    return QStringLiteral("Twintailor " TWINTAILOR_VERSION " (" TWINTAILOR_COMMIT ")");
}

QString aboutText() {
    return versionLine() + "\n\nMIT licence.\nQt is used under the LGPL 3. See THIRD-PARTY.md beside the program.";
}

} // namespace twintailor::ui
