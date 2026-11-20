# Tests that run the whole window: driven through its widgets by mouse and
# keys, without a screen. Labelled "integration", as the examples are (see
# examples/).
#
# Built when the window is. Its tests are listed when ctest runs (not when
# built), with Qt's libraries on the path; they find assets/ from the
# project's folder.
if(TARGET clothcad_qt_ui)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Core)  # its targets are seen only where Qt was found
    add_executable(clothcad_qt_window_tests QtWindowTests.cpp)
    target_link_libraries(clothcad_qt_window_tests PRIVATE clothcad::qt_ui Catch2::Catch2WithMain)
    target_include_directories(clothcad_qt_window_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
    clothcad_warnings(clothcad_qt_window_tests)
    catch_discover_tests(clothcad_qt_window_tests
        TEST_PREFIX "qt: "
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        DISCOVERY_MODE PRE_TEST
        DL_PATHS "$<TARGET_FILE_DIR:Qt6::Core>"
        PROPERTIES LABELS integration ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
