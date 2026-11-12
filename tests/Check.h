#pragma once
// A minimal test harness: no macros, no dependencies.
//
//   void myTest(Check& check) { check.equal(1 + 1, 2.0, "addition"); }
//   int main() { return runTests({{"my test", myTest}}); }

#include <cmath>
#include <cstdio>
#include <source_location>
#include <string_view>
#include <vector>

class Check {
public:
    void that(bool condition, std::string_view what,
              std::source_location where = std::source_location::current()) {
        if (!condition) fail(what, where);
    }

    void equal(double actual, double expected, std::string_view what,
               std::source_location where = std::source_location::current()) {
        if (std::abs(actual - expected) < 1e-9) return;
        fail(what, where);
        std::printf("        got %g, expected %g\n", actual, expected);
    }

    template <class Exception, class Action>
    void throws(Action action, std::string_view what,
                std::source_location where = std::source_location::current()) {
        try {
            action();
        } catch (const Exception&) {
            return;
        } catch (...) {
        }
        fail(what, where);
    }

    int failures() const { return failures_; }

private:
    void fail(std::string_view what, const std::source_location& where) {
        ++failures_;
        std::printf("    FAIL %.*s  (%s:%u)\n", static_cast<int>(what.size()), what.data(),
                    where.file_name(), static_cast<unsigned>(where.line()));
    }

    int failures_ = 0;
};

struct Test {
    const char* name;
    void (*run)(Check&);
};

inline int runTests(const std::vector<Test>& tests) {
    int failed = 0;
    for (const Test& test : tests) {
        Check check;
        test.run(check);
        std::printf("%s  %s\n", check.failures() == 0 ? "ok  " : "FAIL", test.name);
        if (check.failures() != 0) ++failed;
    }
    std::printf("%zu tests, %d failed\n", tests.size(), failed);
    return failed == 0 ? 0 : 1;
}
