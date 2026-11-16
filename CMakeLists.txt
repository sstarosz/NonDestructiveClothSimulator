cmake_minimum_required(VERSION 3.25)
project(NonDestructiveClothCad VERSION 0.1.0 LANGUAGES CXX)

# Dependencies come from vcpkg (see vcpkg.json and CMakePresets.json).
#
#   libs/core   the document, formulas, evaluation, files      standard library only
#   libs/sim    meshing, bodies, the cloth solver              core, CDT
#   libs/session  a document being edited and all worked out from it: undo, mesh, body, the cloth's thread
#   apps/imgui  the window of 0.1, until the Qt one replaces it
#   examples/   five garments as small programs
#   tests/      integration tests and benchmarks; unit tests live beside each library

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CLOTHCAD_BUILD_TESTS "Unit tests, integration tests and benchmarks" ON)
option(CLOTHCAD_BUILD_EXAMPLES "The five garment examples" ON)
option(CLOTHCAD_BUILD_IMGUI_APP "The window of 0.1 (Dear ImGui)" ON)
# Off: build the window's code and its tests, but not the program that opens
# a window. For machines without a display or GLFW, such as a build server.
option(CLOTHCAD_WINDOW "Build the program that opens a window" ON)

include(cmake/ClothCad.cmake)

if(CLOTHCAD_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 CONFIG REQUIRED)
    include(Catch)
endif()

add_subdirectory(libs/core)
add_subdirectory(libs/sim)
add_subdirectory(libs/session)
if(CLOTHCAD_BUILD_IMGUI_APP)
    add_subdirectory(apps/imgui)
endif()
if(CLOTHCAD_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(CLOTHCAD_BUILD_TESTS)
    add_subdirectory(tests)
endif()
