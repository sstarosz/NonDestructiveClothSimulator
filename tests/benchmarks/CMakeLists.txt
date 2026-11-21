# How long things take, held to limits and to the baselines in baselines/.
# See Perf.h. They do nothing in a build that is not optimised. Labelled
# "benchmark"; they run one at a time, so that they do not slow each other.
#
# After a change that makes something faster, write new baselines from an
# optimised build:
#   clothcad_core_and_sim_benchmarks baselines/CoreAndSim.txt --update
#   clothcad_qt_window_benchmarks baselines/QtWindow.txt --update
add_executable(clothcad_core_and_sim_benchmarks CoreAndSimBenchmarks.cpp)
target_link_libraries(clothcad_core_and_sim_benchmarks PRIVATE clothcad::sim)
clothcad_warnings(clothcad_core_and_sim_benchmarks)
add_test(NAME "benchmark: core and sim" COMMAND clothcad_core_and_sim_benchmarks ${CMAKE_CURRENT_SOURCE_DIR}/baselines/CoreAndSim.txt)
set_tests_properties("benchmark: core and sim" PROPERTIES LABELS benchmark RUN_SERIAL TRUE)

# The window, without a screen. Qt's libraries are put on the path for it.
if(TARGET clothcad_qt_ui)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Core)
    add_executable(clothcad_qt_window_benchmarks QtWindowBenchmarks.cpp)
    target_link_libraries(clothcad_qt_window_benchmarks PRIVATE clothcad::qt_ui)
    clothcad_warnings(clothcad_qt_window_benchmarks)
    # On the project's avatar: tests/support/Avatar.h.
    target_include_directories(clothcad_qt_window_benchmarks PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
    target_compile_definitions(clothcad_qt_window_benchmarks PRIVATE CLOTHCAD_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets")
    add_test(NAME "benchmark: window" COMMAND clothcad_qt_window_benchmarks ${CMAKE_CURRENT_SOURCE_DIR}/baselines/QtWindow.txt)
    set_tests_properties("benchmark: window" PROPERTIES LABELS benchmark RUN_SERIAL TRUE
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
