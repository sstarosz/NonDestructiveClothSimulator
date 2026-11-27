# Helpers shared by every target of the project.

# Warnings are defects: the same strict set on every compiler.
function(twintailor_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            # False alarms of GCC 12 inside its own standard library in optimised builds.
            target_compile_options(${target} PRIVATE -Wno-maybe-uninitialized -Wno-restrict)
        endif()
    endif()
    if(TWINTAILOR_WARNINGS_AS_ERRORS)
        set_target_properties(${target} PROPERTIES COMPILE_WARNING_AS_ERROR ON)
    endif()
endfunction()

# A library of the project: sources in src/, public headers in
# include/twintailor/<name>/, private headers in src/.
#
#   twintailor_library(core SOURCES src/Document.cpp ... DEPENDS ...)
#
# makes the target twintailor_<name> and its alias twintailor::<name>.
function(twintailor_library name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPENDS;PRIVATE_DEPENDS" ${ARGN})
    add_library(twintailor_${name} STATIC ${ARG_SOURCES})
    add_library(twintailor::${name} ALIAS twintailor_${name})
    target_include_directories(twintailor_${name}
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_features(twintailor_${name} PUBLIC cxx_std_23)
    target_link_libraries(twintailor_${name} PUBLIC ${ARG_DEPENDS} PRIVATE ${ARG_PRIVATE_DEPENDS})
    twintailor_warnings(twintailor_${name})
endfunction()

# The unit tests of a library: one test file per source file, all in one
# program, each test case its own CTest test, labelled "unit".
#
#   twintailor_unit_tests(core SOURCES ExprTests.cpp ...)
function(twintailor_unit_tests name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPENDS" ${ARGN})
    add_executable(twintailor_${name}_tests ${ARG_SOURCES})
    target_link_libraries(twintailor_${name}_tests PRIVATE twintailor::${name} ${ARG_DEPENDS} Catch2::Catch2WithMain)
    # The tests may use a library's private headers: they test its insides too.
    target_include_directories(twintailor_${name}_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src ${PROJECT_SOURCE_DIR}/tests/support)
    twintailor_warnings(twintailor_${name}_tests)
    catch_discover_tests(twintailor_${name}_tests TEST_PREFIX "${name}: " PROPERTIES LABELS unit)
endfunction()
