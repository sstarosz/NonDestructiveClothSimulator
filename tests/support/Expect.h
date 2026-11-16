#pragma once
// Checks that read as sentences, on top of Catch2: what is checked, and in
// words what it means, shown when it fails.
//
//   EXPECT(result.ok(), "cutting is not an error");
//   EXPECT_NEAR(result[panel].length(rect.top()), 30.0, "the top keeps its width");
//   EXPECT_THROWS([&] { doc.get(PanelId{99}); }, std::out_of_range, "an unknown panel throws");
//
// These three are the only macros the project defines; Catch2 itself is
// made of macros, and these only add the sentence.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#define EXPECT(condition, what)                                                                    \
    do {                                                                                           \
        INFO(what);                                                                                \
        CHECK(condition);                                                                          \
    } while (false)

#define EXPECT_NEAR(actual, expected, what)                                                        \
    do {                                                                                           \
        INFO(what);                                                                                \
        CHECK_THAT((actual), Catch::Matchers::WithinAbs((expected), 1e-9));                        \
    } while (false)

#define EXPECT_THROWS(action, Exception, what)                                                     \
    do {                                                                                           \
        INFO(what);                                                                                \
        CHECK_THROWS_AS((action)(), Exception);                                                    \
    } while (false)
