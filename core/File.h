#pragma once
// A document as text, for saving and opening files. Everything the document
// holds is written, and reading gives back an equal document: the same ids,
// formulas, references and seams.
//
//   std::string text = toText(doc);
//   auto read = fromText(text);          // a Document, or what is wrong and on which line
//
//   save(doc, "skirt.clothcad");         // empty on success, else why not
//   auto opened = open("skirt.clothcad");
//
// The format is one thing per line and meant to be stable; its first line
// carries a version number.

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>

#include "core/Document.h"

namespace clothcad {

struct FileError {
    std::size_t line = 0;  // 1-based; 0 when the problem is the file itself, not a line in it
    std::string message;
};

std::string toText(const Document& doc);
std::variant<Document, FileError> fromText(std::string_view text);

// Writes the document to a file. Returns why it could not, or nothing.
std::string save(const Document& doc, const std::string& path);
std::variant<Document, FileError> open(const std::string& path);

} // namespace clothcad
