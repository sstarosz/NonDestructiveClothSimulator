#pragma once
// A document as text, for saving and opening files, with how the scene was
// left: the draped cloth and what was hidden or frozen. Everything the
// document and the scene hold is written, and reading gives back equal ones:
// the same ids, formulas, references, seams and avatar. The file stands on its
// own: it names no other file.
//
//   std::string text = toText(doc);
//   auto read = fromText(text);          // a SavedScene, or what is wrong and on which line
//
//   save(doc, "skirt.clothcad", state);  // empty on success, else why not
//   auto opened = open("skirt.clothcad");
//
// The format is one thing per line and meant to be stable; its first line
// carries a version number. Paths are UTF-8, whatever the system's own code
// page is.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "clothcad/core/Document.h"

namespace clothcad {

// The cloth of one panel as it hung: its particles, each where it is in the
// panel's own flat coordinates and where it is in space, and the triangles
// between them. The flat coordinates let a drape be carried over to a mesh
// that is not made quite the same.
struct DrapedPanel {
    PanelId panel;
    Vec2 anchor;                                          // the panel's anchor when the mesh was made
    std::vector<Vec2> flat;                               // per particle
    std::vector<Point3> at;                               // per particle, as many as flat
    std::vector<std::array<std::uint32_t, 3>> triangles;  // each names three of the particles
    friend bool operator==(const DrapedPanel&, const DrapedPanel&) = default;
};

struct SavedDrape {
    double time = 0.0;    // seconds of cloth time
    bool sewing = false;  // its seams were still closing
    std::vector<DrapedPanel> panels;
    friend bool operator==(const SavedDrape&, const SavedDrape&) = default;
};

// How the scene was left. Saved in the file beside the pattern, but no part of
// the document: changing it is no undo step.
struct SceneState {
    std::vector<PanelId> frozen;  // panels deactivated: they take no part in the cloth
    std::vector<PanelId> hidden;  // panels not drawn in space
    std::vector<SeamId> hiddenSeams;
    bool bodyHidden = false;  // and so out of the simulation
    bool bodySeeThrough = false;
    std::optional<SavedDrape> drape;  // none: the panels stand as they are arranged
    friend bool operator==(const SceneState&, const SceneState&) = default;
};

// What a file holds.
struct SavedScene {
    Document document;
    SceneState state;
};

struct FileError {
    std::size_t line = 0;  // 1-based; 0 when the problem is the file itself, not a line in it
    std::string message;
};

// Panels and seams that `state` names must be in the document.
std::string toText(const Document& doc, const SceneState& state = {});
std::variant<SavedScene, FileError> fromText(std::string_view text);

// Writes a file. Returns why it could not, or nothing. The file is written
// beside the old one and then takes its place, so that a save which fails
// leaves the old file as it was. Where another program holds the old file, that
// is tried again for about half a second before it fails.
std::string save(const Document& doc, const std::string& path, const SceneState& state = {});
std::variant<SavedScene, FileError> open(const std::string& path);

// A UTF-8 path as the system's file functions take it.
std::filesystem::path filePath(std::string_view utf8);

} // namespace clothcad
