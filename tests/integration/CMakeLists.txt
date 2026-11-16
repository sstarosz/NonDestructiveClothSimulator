# Tests that go across libraries, or run a whole program: the window driven
# by mouse and keys, files of every example, the examples themselves (see
# examples/). Labelled "integration".
add_executable(clothcad_integration_tests
    WindowPlacementTests.cpp
    WindowScriptTests.cpp
    ${PROJECT_SOURCE_DIR}/tests/support/TextureStub.cpp
)
target_link_libraries(clothcad_integration_tests PRIVATE clothcad::imgui_ui Catch2::Catch2WithMain)
target_include_directories(clothcad_integration_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
clothcad_warnings(clothcad_integration_tests)
# The tests find assets/ from the project's folder.
catch_discover_tests(clothcad_integration_tests
    TEST_PREFIX "integration: "
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    PROPERTIES LABELS integration)
