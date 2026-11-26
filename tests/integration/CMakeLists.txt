# Tests on the project's avatar (assets/avatarV1.obj): read from its file
# and dressed. Labelled "integration"; the unit tests read no files.
add_executable(clothcad_avatar_tests AvatarTests.cpp)
target_link_libraries(clothcad_avatar_tests PRIVATE clothcad::session Catch2::Catch2WithMain)
# Avatar.h and Expect.h, and the solver tests' helpers (run, worstStretch).
target_include_directories(clothcad_avatar_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support ${PROJECT_SOURCE_DIR}/libs/sim/tests)
target_compile_definitions(clothcad_avatar_tests PRIVATE CLOTHCAD_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets")
clothcad_warnings(clothcad_avatar_tests)
catch_discover_tests(clothcad_avatar_tests TEST_PREFIX "avatar: " PROPERTIES LABELS integration)

# The recovery copy, which is written to disk. Labelled "integration".
add_executable(clothcad_autosave_tests AutosaveTests.cpp)
target_link_libraries(clothcad_autosave_tests PRIVATE clothcad::session Catch2::Catch2WithMain)
target_include_directories(clothcad_autosave_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
target_compile_definitions(clothcad_autosave_tests PRIVATE CLOTHCAD_TEST_FOLDER="${CMAKE_CURRENT_BINARY_DIR}/autosave")
clothcad_warnings(clothcad_autosave_tests)
catch_discover_tests(clothcad_autosave_tests TEST_PREFIX "autosave: " PROPERTIES LABELS integration)

# Tests that run the whole window: driven through its widgets by mouse and
# keys, without a screen. Labelled "integration", as the examples are (see
# examples/).
#
# Built when the window is. Its tests are listed when ctest runs (not when
# built), with Qt's libraries on the path; the avatars are put beside them,
# as beside the program.
if(TARGET clothcad_qt_ui)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Core)  # its targets are seen only where Qt was found
    add_executable(clothcad_qt_window_tests QtWindowTests.cpp)
    target_link_libraries(clothcad_qt_window_tests PRIVATE clothcad::qt_ui Catch2::Catch2WithMain)
    target_include_directories(clothcad_qt_window_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
    clothcad_warnings(clothcad_qt_window_tests)
    clothcad_avatars_beside(clothcad_qt_window_tests)
    catch_discover_tests(clothcad_qt_window_tests
        TEST_PREFIX "qt: "
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        DISCOVERY_MODE PRE_TEST
        DL_PATHS "$<TARGET_FILE_DIR:Qt6::Core>"
        PROPERTIES LABELS integration ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
