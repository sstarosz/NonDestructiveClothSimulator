#include "Version.h"

// CLOTHCAD_VERSION and CLOTHCAD_COMMIT are given to this file alone (see
// CMakeLists.txt), so that a new commit compiles nothing else again.

namespace clothcad::ui {

QString versionLine() {
    return QStringLiteral("Cloth CAD " CLOTHCAD_VERSION " (" CLOTHCAD_COMMIT ")");
}

QString aboutText() {
    return versionLine() + "\n\nMIT licence.\nQt is used under the LGPL 3. See THIRD-PARTY.md beside the program.";
}

} // namespace clothcad::ui
