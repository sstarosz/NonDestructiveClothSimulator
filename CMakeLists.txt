cmake_minimum_required(VERSION 3.20)
project(NonDestructiveClothCad LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- core: the document model and evaluation; no UI, no dependencies --------
add_library(clothcore STATIC
    core/Document.cpp
    core/Evaluate.cpp
    core/Expr.cpp
    core/Pattern.cpp
)
target_include_directories(clothcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(clothcore PUBLIC /W4 /permissive-)
else()
    target_compile_options(clothcore PUBLIC -Wall -Wextra -Wpedantic)
endif()

# ---- examples: one per garment in the design -------------------------------
add_executable(example_01_waistband_skirt examples/01_waistband_skirt.cpp)
target_link_libraries(example_01_waistband_skirt PRIVATE clothcore)

add_executable(example_02_circle_skirt examples/02_circle_skirt.cpp)
target_link_libraries(example_02_circle_skirt PRIVATE clothcore)

# ---- tests -----------------------------------------------------------------
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE clothcore)

enable_testing()
add_test(NAME core_tests COMMAND core_tests)
add_test(NAME example_01_waistband_skirt COMMAND example_01_waistband_skirt)
add_test(NAME example_02_circle_skirt COMMAND example_02_circle_skirt)
