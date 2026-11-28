# Tests on the project's avatar (assets/avatarV1.obj): read from its file
# and dressed. Labelled "integration"; the unit tests read no files.
add_executable(twintailor_avatar_tests AvatarTests.cpp)
target_link_libraries(twintailor_avatar_tests PRIVATE twintailor::session Catch2::Catch2WithMain)
# Avatar.h and Expect.h, and the solver tests' helpers (run, worstStretch).
target_include_directories(twintailor_avatar_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support ${PROJECT_SOURCE_DIR}/libs/sim/tests)
target_compile_definitions(twintailor_avatar_tests PRIVATE TWINTAILOR_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets")
twintailor_warnings(twintailor_avatar_tests)
catch_discover_tests(twintailor_avatar_tests TEST_PREFIX "avatar: " PROPERTIES LABELS integration)

# The recovery copy, which is written to disk. Labelled "integration".
add_executable(twintailor_autosave_tests AutosaveTests.cpp)
target_link_libraries(twintailor_autosave_tests PRIVATE twintailor::session Catch2::Catch2WithMain)
target_include_directories(twintailor_autosave_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
target_compile_definitions(twintailor_autosave_tests PRIVATE TWINTAILOR_TEST_FOLDER="${CMAKE_CURRENT_BINARY_DIR}/autosave")
twintailor_warnings(twintailor_autosave_tests)
catch_discover_tests(twintailor_autosave_tests TEST_PREFIX "autosave: " PROPERTIES LABELS integration)

# The log of actions and the crash report: files too.
add_executable(twintailor_crash_report_tests CrashReportTests.cpp)
target_link_libraries(twintailor_crash_report_tests PRIVATE twintailor::session Catch2::Catch2WithMain)
target_include_directories(twintailor_crash_report_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
target_compile_definitions(twintailor_crash_report_tests PRIVATE TWINTAILOR_TEST_FOLDER="${CMAKE_CURRENT_BINARY_DIR}/crash-report")
twintailor_warnings(twintailor_crash_report_tests)
catch_discover_tests(twintailor_crash_report_tests TEST_PREFIX "crash reports: " PROPERTIES LABELS integration)

# Saving over a file that another program holds. Labelled "integration".
add_executable(twintailor_saving_tests SavingTests.cpp)
target_link_libraries(twintailor_saving_tests PRIVATE twintailor::core Catch2::Catch2WithMain)
target_include_directories(twintailor_saving_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
target_compile_definitions(twintailor_saving_tests PRIVATE TWINTAILOR_TEST_FOLDER="${CMAKE_CURRENT_BINARY_DIR}/saving")
twintailor_warnings(twintailor_saving_tests)
catch_discover_tests(twintailor_saving_tests TEST_PREFIX "saving: " PROPERTIES LABELS integration)

# Damaged files, from reading them to meshing what opens. Labelled "integration".
add_executable(twintailor_damaged_file_tests DamagedFileTests.cpp)
target_link_libraries(twintailor_damaged_file_tests PRIVATE twintailor::session Catch2::Catch2WithMain)
target_include_directories(twintailor_damaged_file_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
twintailor_warnings(twintailor_damaged_file_tests)
catch_discover_tests(twintailor_damaged_file_tests TEST_PREFIX "damaged files: " PROPERTIES LABELS integration)

# Work on other threads that fails. Its own program: it brings its own
# operator new, which runs out of memory when a test says so. Labelled "integration".
add_executable(twintailor_failure_tests FailureTests.cpp)
target_link_libraries(twintailor_failure_tests PRIVATE twintailor::session Catch2::Catch2WithMain)
target_include_directories(twintailor_failure_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
target_compile_definitions(twintailor_failure_tests PRIVATE TWINTAILOR_TEST_FOLDER="${CMAKE_CURRENT_BINARY_DIR}/failure")
twintailor_warnings(twintailor_failure_tests)
# A failure that is not caught ends or hangs the program: each test has a time of its own.
catch_discover_tests(twintailor_failure_tests TEST_PREFIX "failures: " PROPERTIES LABELS integration TIMEOUT 120)

# Tests that run the whole window: driven through its widgets by mouse and
# keys, without a screen. Labelled "integration", as the examples are (see
# examples/).
#
# Built when the window is. Its tests are listed when ctest runs (not when
# built), with Qt's libraries on the path; the avatars are put beside them,
# as beside the program.
if(TARGET twintailor_qt_ui)
    # Targets are seen only where Qt was found. Qt Test sends keys as the system does;
    # it is for the tests only and is not shipped.
    find_package(Qt6 6.2 REQUIRED COMPONENTS Core Test)
    add_executable(twintailor_qt_window_tests QtWindowTests.cpp)
    target_link_libraries(twintailor_qt_window_tests PRIVATE twintailor::qt_ui Qt6::Test Catch2::Catch2WithMain)
    target_include_directories(twintailor_qt_window_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
    twintailor_warnings(twintailor_qt_window_tests)
    twintailor_avatars_beside(twintailor_qt_window_tests)
    target_compile_definitions(twintailor_qt_window_tests PRIVATE TWINTAILOR_PROJECT_VERSION="${PROJECT_VERSION}")
    catch_discover_tests(twintailor_qt_window_tests
        TEST_PREFIX "qt: "
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        DISCOVERY_MODE PRE_TEST
        DL_PATHS "$<TARGET_FILE_DIR:Qt6::Core>"
        PROPERTIES LABELS integration ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# What a release is made of. The tag and the notes are checked everywhere; the
# installed folder and the program in it where the window is built.
add_test(NAME "release: the workflows name exact tools"
    COMMAND ${CMAKE_COMMAND} -DFOLDER=${PROJECT_SOURCE_DIR}/.github/workflows -P ${PROJECT_SOURCE_DIR}/cmake/CheckWorkflows.cmake)
set_tests_properties("release: the workflows name exact tools" PROPERTIES LABELS integration)
set(version_pattern "${PROJECT_VERSION}")
string(REPLACE "." "\\." version_pattern "${version_pattern}")
foreach(tag "v${PROJECT_VERSION}" "v${PROJECT_VERSION}-rc1")
    add_test(NAME "release: ${tag} has this version's notes"
        COMMAND ${CMAKE_COMMAND} -DTAG=${tag} -DNOTES=${CMAKE_CURRENT_BINARY_DIR}/notes-${tag}.md
                -P ${PROJECT_SOURCE_DIR}/cmake/ReleaseNotes.cmake)
    set_tests_properties("release: ${tag} has this version's notes" PROPERTIES LABELS integration)
endforeach()
foreach(tag "v99.0.0" "${PROJECT_VERSION}" "v${PROJECT_VERSION}-beta")
    add_test(NAME "release: the tag ${tag} is refused"
        COMMAND ${CMAKE_COMMAND} -DTAG=${tag} -DNOTES=${CMAKE_CURRENT_BINARY_DIR}/notes-refused.md
                -P ${PROJECT_SOURCE_DIR}/cmake/ReleaseNotes.cmake)
    set_tests_properties("release: the tag ${tag} is refused" PROPERTIES LABELS integration WILL_FAIL TRUE)
endforeach()

if(TARGET twintailor)
    add_test(NAME "release: twintailor --version prints the version and ends" COMMAND twintailor --version)
    set_tests_properties("release: twintailor --version prints the version and ends" PROPERTIES
        LABELS integration TIMEOUT 60
        PASS_REGULAR_EXPRESSION "Twintailor ${version_pattern} \\(([0-9a-f]+|unknown)\\)")
    # A Debug build is not handed out: its libraries are not in the package.
    add_test(NAME "release: the installed folder is complete, and its program starts"
        COMMAND ${CMAKE_COMMAND} -DBUILD=${CMAKE_BINARY_DIR} -DCONFIG=$<CONFIG>
                -DFOLDER=${CMAKE_CURRENT_BINARY_DIR}/package-$<CONFIG> -DVERSION=${PROJECT_VERSION}
                -P ${PROJECT_SOURCE_DIR}/cmake/CheckPackage.cmake
        CONFIGURATIONS Release RelWithDebInfo)
    set_tests_properties("release: the installed folder is complete, and its program starts" PROPERTIES LABELS integration)
endif()
