cmake_minimum_required(VERSION 3.20)
project(NonDestructiveClothCad VERSION 0.1.0 LANGUAGES C CXX)  # C is for GLFW

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- core: the document model and evaluation; no UI, no dependencies --------
add_library(clothcore STATIC
    core/Document.cpp
    core/Drawing.cpp
    core/Evaluate.cpp
    core/Expr.cpp
    core/Fields.cpp
    core/File.cpp
    core/Outline.cpp
    core/Pattern.cpp
    core/References.cpp
    core/Text.cpp
)
target_include_directories(clothcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(clothcore PUBLIC /W4 /permissive-)
else()
    target_compile_options(clothcore PUBLIC -Wall -Wextra -Wpedantic)
endif()

# ---- sim: meshing (and later the cloth solver) -------------------------------
# CDT (constrained Delaunay triangulation, header-only) is downloaded from
# GitHub the first time you configure. Use -DBUILD_SIM=OFF to build only the
# core; the window needs it.
option(BUILD_SIM "Build meshing and simulation" ON)
if(BUILD_SIM)
    include(FetchContent)
    FetchContent_Declare(cdt
        GIT_REPOSITORY https://github.com/artem-ogre/CDT.git
        GIT_TAG 1.4.4
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR do-not-build)  # header-only: fetch it, don't run its CMake
    FetchContent_MakeAvailable(cdt)
    add_library(clothsim STATIC sim/Body.cpp sim/Cloth.cpp sim/Layout.cpp sim/Mesh.cpp)
    target_include_directories(clothsim SYSTEM PRIVATE ${cdt_SOURCE_DIR}/CDT/include)
    target_link_libraries(clothsim PUBLIC clothcore)
endif()

# ---- examples: one per garment in the design -------------------------------
set(EXAMPLES
    01_waistband_skirt
    02_circle_skirt
    03_asymmetric_hem
    04_mirrored_sleeve
    05_shared_base
)
foreach(example IN LISTS EXAMPLES)
    add_executable(example_${example} examples/${example}.cpp)
    target_link_libraries(example_${example} PRIVATE clothcore)
endforeach()

# ---- tests -----------------------------------------------------------------
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE clothcore)

enable_testing()
add_test(NAME core_tests COMMAND core_tests)
foreach(example IN LISTS EXAMPLES)
    add_test(NAME example_${example} COMMAND example_${example})
endforeach()
if(BUILD_SIM)
    add_executable(sim_tests tests/sim_tests.cpp)
    target_link_libraries(sim_tests PRIVATE clothsim)
    add_test(NAME sim_tests COMMAND sim_tests)
endif()

# ---- app: the window ----------------------------------------------------------
# Dear ImGui and GLFW are downloaded from GitHub the first time you configure
# (this needs git and a network connection). Use -DBUILD_APP=OFF to build only
# the core, examples and tests.
option(BUILD_APP "Build the window" ON)
if(BUILD_APP AND NOT BUILD_SIM)
    message(FATAL_ERROR "The window needs BUILD_SIM=ON")
endif()
if(BUILD_APP)
    include(FetchContent)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(glfw imgui)
    find_package(OpenGL REQUIRED)

    # Dear ImGui ships as source files without a CMake project of its own.
    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

    add_executable(clothcad
        app/main.cpp
        app/App.cpp
        app/Canvas.cpp
        app/Locks.cpp
        app/Samples.cpp
        app/Texture.cpp
        app/View3D.cpp
    )
    find_package(Threads REQUIRED)
    target_link_libraries(clothcad PRIVATE clothcore clothsim imgui Threads::Threads)
    # Where the avatar that comes with the project is, so the program finds it wherever it is run from.
    target_compile_definitions(clothcad PRIVATE CLOTHCAD_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
endif()
