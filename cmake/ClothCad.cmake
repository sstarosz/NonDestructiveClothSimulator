# Helpers shared by every target of the project.

# Warnings are defects: the same strict set on every compiler.
function(clothcad_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            # False alarms of GCC 12 inside its own standard library in optimised builds.
            target_compile_options(${target} PRIVATE -Wno-maybe-uninitialized -Wno-restrict)
        endif()
    endif()
endfunction()

# A library of the project: sources in src/, public headers in
# include/clothcad/<name>/, private headers in src/.
#
#   clothcad_library(core SOURCES src/Document.cpp ... DEPENDS ...)
#
# makes the target clothcad_<name> and its alias clothcad::<name>.
function(clothcad_library name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPENDS;PRIVATE_DEPENDS" ${ARGN})
    add_library(clothcad_${name} STATIC ${ARG_SOURCES})
    add_library(clothcad::${name} ALIAS clothcad_${name})
    target_include_directories(clothcad_${name}
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_features(clothcad_${name} PUBLIC cxx_std_23)
    target_link_libraries(clothcad_${name} PUBLIC ${ARG_DEPENDS} PRIVATE ${ARG_PRIVATE_DEPENDS})
    clothcad_warnings(clothcad_${name})
endfunction()

# The unit tests of a library: one test file per source file, all in one
# program, each test case its own CTest test, labelled "unit".
#
#   clothcad_unit_tests(core SOURCES ExprTests.cpp ...)
function(clothcad_unit_tests name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPENDS" ${ARGN})
    add_executable(clothcad_${name}_tests ${ARG_SOURCES})
    target_link_libraries(clothcad_${name}_tests PRIVATE clothcad::${name} ${ARG_DEPENDS} Catch2::Catch2WithMain)
    # The tests may use a library's private headers: they test its insides too.
    target_include_directories(clothcad_${name}_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src ${PROJECT_SOURCE_DIR}/tests/support)
    clothcad_warnings(clothcad_${name}_tests)
    catch_discover_tests(clothcad_${name}_tests TEST_PREFIX "${name}: " PROPERTIES LABELS unit)
endfunction()
