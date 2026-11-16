# How long things take, held to limits and to the baselines in baselines/.
# See Perf.h. They do nothing in a build that is not optimised. Labelled
# "benchmark"; they run one at a time, so that they do not slow each other.
#
# After a change that makes something faster, write new baselines from an
# optimised build:
#   clothcad_core_and_sim_benchmarks baselines/CoreAndSim.txt --update
add_executable(clothcad_core_and_sim_benchmarks CoreAndSimBenchmarks.cpp)
target_link_libraries(clothcad_core_and_sim_benchmarks PRIVATE clothcad::sim)
clothcad_warnings(clothcad_core_and_sim_benchmarks)
add_test(NAME "benchmark: core and sim" COMMAND clothcad_core_and_sim_benchmarks ${CMAKE_CURRENT_SOURCE_DIR}/baselines/CoreAndSim.txt)
set_tests_properties("benchmark: core and sim" PROPERTIES LABELS benchmark RUN_SERIAL TRUE)

if(TARGET clothcad::imgui_ui)
    add_executable(clothcad_window_benchmarks WindowBenchmarks.cpp ${PROJECT_SOURCE_DIR}/tests/support/TextureStub.cpp)
    target_link_libraries(clothcad_window_benchmarks PRIVATE clothcad::imgui_ui)
    clothcad_warnings(clothcad_window_benchmarks)
    add_test(NAME "benchmark: window" COMMAND clothcad_window_benchmarks ${CMAKE_CURRENT_SOURCE_DIR}/baselines/Window.txt)
    set_tests_properties("benchmark: window" PROPERTIES LABELS benchmark RUN_SERIAL TRUE)
endif()
