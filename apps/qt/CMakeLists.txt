# The Qt window (0.1.1 step 5). Built when Qt 6 is found: on Windows the
# presets point CMAKE_PREFIX_PATH at C:/Qt/6.12.0/msvc2022_64. The window's
# parts are a library, so that tests can drive them without a screen.

# The Windows package is flat: whoever unpacks the zip finds clothcad.exe at
# its top, not in bin. Set before Qt is looked for: its deploy script takes the
# program's folder from here.
if(WIN32)
    set(CMAKE_INSTALL_BINDIR ".")
endif()
find_package(Qt6 6.5 QUIET COMPONENTS Widgets)  # 6.5: qt_generate_deploy_app_script with its options
if(NOT Qt6_FOUND)
    message(STATUS "Qt 6 was not found: the Qt window is not built. Set CMAKE_PREFIX_PATH to your Qt to build it.")
    return()
endif()
# Its 3D view draws with libs/render, which needs the Vulkan SDK.
if(NOT TARGET clothcad::render)
    message(STATUS "The Qt window needs the Vulkan SDK for its 3D view: it is not built.")
    return()
endif()

add_library(clothcad_qt_ui STATIC
    Application.cpp
    AttributeEditor.cpp
    AvatarDialog.cpp
    Icons.cpp
    LengthCard.cpp
    Lists.cpp
    MainWindow.cpp
    Outliner.cpp
    ParametersPanel.cpp
    Section.cpp
    Shelf.cpp
    SpaceView.cpp
    TableView.cpp
    Theme.cpp
    ValueField.cpp
    Version.cpp
)
add_library(clothcad::qt_ui ALIAS clothcad_qt_ui)
set_target_properties(clothcad_qt_ui PROPERTIES AUTOMOC ON)
target_include_directories(clothcad_qt_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(clothcad_qt_ui PUBLIC clothcad::core clothcad::sim clothcad::session clothcad::render Qt6::Widgets)
clothcad_warnings(clothcad_qt_ui)

# The version and the commit, for the window only (Help > About, --version).
set(commit unknown)
find_package(Git QUIET)
if(Git_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        RESULT_VARIABLE result OUTPUT_VARIABLE head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(result EQUAL 0)
        set(commit ${head})
    endif()
endif()
# Configured again after a commit or a checkout, so that it names what is built.
if(EXISTS "${PROJECT_SOURCE_DIR}/.git/logs/HEAD")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/.git/logs/HEAD")
endif()
set_source_files_properties(Version.cpp PROPERTIES
    COMPILE_DEFINITIONS "CLOTHCAD_VERSION=\"${PROJECT_VERSION}\";CLOTHCAD_COMMIT=\"${commit}\"")

# The samples are draped on the avatars in assets/; the window looks for them
# in an assets/ folder beside the program, so that a build folder or an
# installed package works wherever it is. Only the OBJ files are needed.
function(clothcad_avatars_beside target)
    file(GLOB avatars CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/assets/*.obj")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${avatars} "$<TARGET_FILE_DIR:${target}>/assets"
        COMMENT "Putting the avatars beside ${target}")
endfunction()

if(CLOTHCAD_WINDOW)
    add_executable(clothcad WIN32 main.cpp)
    target_link_libraries(clothcad PRIVATE clothcad::qt_ui)
    clothcad_warnings(clothcad)
    # Only what the window uses: its icons are drawn in code, the file dialogs
    # are Windows' own, the 3D view is Vulkan and nothing goes over a network.
    # The compiler's runtime is added below as plain libraries, not as its
    # installer. What ships is listed in THIRD-PARTY.md and checked by
    # cmake/CheckPackage.cmake.
    set(deploy_only --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw --no-compiler-runtime
        --skip-plugin-types generic,iconengines,imageformats,networkinformation,tls)
    # On Windows, Qt's DLLs and plugins go next to the program, so that it
    # starts from the build folder.
    if(WIN32 AND TARGET Qt6::windeployqt)
        add_custom_command(TARGET clothcad POST_BUILD
            COMMAND Qt6::windeployqt --no-translations ${deploy_only} $<TARGET_FILE:clothcad>
            COMMENT "Putting Qt's libraries next to clothcad")
    endif()
    clothcad_avatars_beside(clothcad)

    # A package to hand out: cmake --install out/build/x64-win-msvc2026
    # --config Release --prefix <folder> puts the program, Qt's libraries, the
    # avatars and the licences in <folder> (Qt's plugins in <folder>/plugins).
    # Elsewhere than on Windows, <folder>/bin takes the place of <folder>.
    include(GNUInstallDirs)
    file(GLOB avatars CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/assets/*.obj")
    install(TARGETS clothcad RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES ${avatars} DESTINATION ${CMAKE_INSTALL_BINDIR}/assets)
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/THIRD-PARTY.md" DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/licenses" DESTINATION ${CMAKE_INSTALL_BINDIR})
    # Qt's libraries as the build folder has them (see windeployqt above).
    qt_generate_deploy_app_script(TARGET clothcad OUTPUT_SCRIPT clothcad_deploy NO_UNSUPPORTED_PLATFORM_ERROR NO_TRANSLATIONS
        DEPLOY_TOOL_OPTIONS ${deploy_only})
    install(SCRIPT ${clothcad_deploy})
    # The compiler's runtime, so that the package starts where it is not installed.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    include(InstallRequiredSystemLibraries)
endif()
