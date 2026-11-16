#pragma once
// Colours and sizes shared by the panes, so the same thing looks the same
// everywhere: a formula in a field and the edge it drives on the canvas.

#include <imgui.h>

namespace clothcad::app {

namespace color {
inline const ImVec4 error{1.00f, 0.42f, 0.38f, 1.0f};
inline const ImVec4 driven{0.43f, 0.86f, 0.78f, 1.0f};    // a value that comes from a formula
inline const ImVec4 instance{0.75f, 0.62f, 0.95f, 1.0f};  // a panel that follows another panel
inline const ImVec4 symmetry{0.95f, 0.50f, 0.78f, 1.0f};  // a mirror's axis
inline const ImVec4 seam{0.45f, 0.68f, 1.00f, 1.0f};      // edges sewn together
inline const ImVec4 selected{1.00f, 0.75f, 0.27f, 1.0f};
// The directions in space: x across, y up, z forward.
inline const ImVec4 axisX{0.90f, 0.39f, 0.39f, 1.0f};
inline const ImVec4 axisY{0.47f, 0.82f, 0.47f, 1.0f};
inline const ImVec4 axisZ{0.43f, 0.59f, 0.94f, 1.0f};
} // namespace color

// One "unit" of line width, dash length or padding. It follows the text size,
// so everything stays in proportion on a high-resolution display.
inline float pixel() { return ImGui::GetFontSize() / 16.0f; }

} // namespace clothcad::app
