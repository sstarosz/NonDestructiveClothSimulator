#pragma once
// A saved text damaged in many ways, for tests of reading it: cut short at
// every few characters, and with one word at a time changed for another.
//
//   forEachDamaged(text, [](const std::string& damaged) { ... });

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace clothcad::testing {

template <class Each>
void forEachDamaged(std::string_view text, const Each& each) {
    for (std::size_t at = 0; at < text.size(); at += 3) each(std::string(text.substr(0, at)));
    // What a word may turn into: no word, no number, numbers out of all reason, and other words of the format.
    constexpr const char* others[] = {"",   "x",  "nan", "inf", "-inf", "1e999", "1e300", "-1e300", "1e-300", "0",      "-1",  "1",
                                      "17", "40", "999999999999", "n",  "p",     "/",     "e",      "\"",     "Mirror", "panel"};
    for (std::size_t from = 0; from < text.size();) {
        const std::size_t end = std::min(text.find_first_of(" \n", from), text.size());
        if (end > from)
            for (const char* other : others) each(std::string(text.substr(0, from)) + other + std::string(text.substr(end)));
        from = end + 1;
    }
}

} // namespace clothcad::testing
