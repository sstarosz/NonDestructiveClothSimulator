# How long things take, held to limits and to the baselines in baselines/.
# See Perf.h. They do nothing in a build that is not optimised. Labelled
# "benchmark"; they run one at a time, so that they do not slow each other.
#
# After a change that makes something faster, write new baselines from an
# optimised build:
#   twintailor_core_and_sim_benchmarks baselines/CoreAndSim.txt --update
#   twintailor_qt_window_benchmarks baselines/QtWindow.txt --update
add_executable(twintailor_core_and_sim_benchmarks CoreAndSimBenchmarks.cpp)
# The trousers sample (session) on the project's avatar (tests/support/Avatar.h).
target_link_libraries(twintailor_core_and_sim_benchmarks PRIVATE twintailor::sim twintailor::session)
target_include_directories(twintailor_core_and_sim_benchmarks PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
target_compile_definitions(twintailor_core_and_sim_benchmarks PRIVATE TWINTAILOR_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets")
twintailor_warnings(twintailor_core_and_sim_benchmarks)
add_test(NAME "benchmark: core and sim" COMMAND twintailor_core_and_sim_benchmarks ${CMAKE_CURRENT_SOURCE_DIR}/baselines/CoreAndSim.txt)
set_tests_properties("benchmark: core and sim" PROPERTIES LABELS benchmark RUN_SERIAL TRUE)

# The window, without a screen. Qt's libraries are put on the path for it.
if(TARGET twintailor_qt_ui)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Core)
    add_executable(twintailor_qt_window_benchmarks QtWindowBenchmarks.cpp)
    target_link_libraries(twintailor_qt_window_benchmarks PRIVATE twintailor::qt_ui)
    twintailor_warnings(twintailor_qt_window_benchmarks)
    # On the project's avatar: tests/support/Avatar.h.
    target_include_directories(twintailor_qt_window_benchmarks PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
    target_compile_definitions(twintailor_qt_window_benchmarks PRIVATE TWINTAILOR_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets")
    add_test(NAME "benchmark: window" COMMAND twintailor_qt_window_benchmarks ${CMAKE_CURRENT_SOURCE_DIR}/baselines/QtWindow.txt)
    set_tests_properties("benchmark: window" PROPERTIES LABELS benchmark RUN_SERIAL TRUE
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
