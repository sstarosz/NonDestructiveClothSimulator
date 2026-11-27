#pragma once
// Which build of the program this is, for Help > About and `twintailor
// --version`, so that a report can say what was run: the version in
// CMakeLists.txt and the commit it was built from ("unknown" without Git).
//
//   versionLine();  // "Twintailor 0.1.0 (a1b2c3d)"

#include <QString>

namespace twintailor::ui {

QString versionLine();
// The version, and under which licences the program and Qt are given.
QString aboutText();

} // namespace twintailor::ui
