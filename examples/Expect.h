#pragma once
// Shared by the examples: print a check and count failures.

#include <cmath>
#include <cstdio>

inline int failures = 0;

inline void expect(const char* what, double actual, double expected) {
    const bool pass = std::abs(actual - expected) < 1e-9;
    std::printf("%s  %-28s %-8g (expected %g)\n", pass ? "ok  " : "FAIL", what, actual, expected);
    if (!pass) ++failures;
}

inline void expect(const char* what, bool actual, bool expected) {
    const bool pass = actual == expected;
    std::printf("%s  %-28s %-8s (expected %s)\n", pass ? "ok  " : "FAIL", what,
                actual ? "yes" : "no", expected ? "yes" : "no");
    if (!pass) ++failures;
}
