# The Qt window (0.1.1 step 5). Built when Qt 6 is found: on Windows the
# presets point CMAKE_PREFIX_PATH at C:/Qt/6.12.0/msvc2022_64. The window's
# parts are a library, so that tests can drive them without a screen.
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
    AttributeEditor.cpp
    Icons.cpp
    LengthCard.cpp
    MainWindow.cpp
    Outliner.cpp
    ParametersPanel.cpp
    Section.cpp
    Shelf.cpp
    SpaceView.cpp
    TableView.cpp
    Theme.cpp
    ValueField.cpp
)
add_library(clothcad::qt_ui ALIAS clothcad_qt_ui)
set_target_properties(clothcad_qt_ui PROPERTIES AUTOMOC ON)
target_include_directories(clothcad_qt_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(clothcad_qt_ui PUBLIC clothcad::core clothcad::sim clothcad::session clothcad::render Qt6::Widgets)
clothcad_warnings(clothcad_qt_ui)

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
    # On Windows, Qt's DLLs and plugins go next to the program, so that it
    # starts from the build folder.
    if(WIN32 AND TARGET Qt6::windeployqt)
        add_custom_command(TARGET clothcad POST_BUILD
            COMMAND Qt6::windeployqt --no-translations --no-system-d3d-compiler --no-opengl-sw $<TARGET_FILE:clothcad>
            COMMENT "Putting Qt's libraries next to clothcad")
    endif()
    clothcad_avatars_beside(clothcad)

    # A package to hand out: cmake --install out/build/x64-win-msvc2026
    # --config Release --prefix <folder> puts the program, Qt's libraries and
    # the avatars in <folder>/bin.
    include(GNUInstallDirs)
    file(GLOB avatars CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/assets/*.obj")
    install(TARGETS clothcad RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES ${avatars} DESTINATION ${CMAKE_INSTALL_BINDIR}/assets)
    # Qt's libraries as the build folder has them (see windeployqt above).
    qt_generate_deploy_app_script(TARGET clothcad OUTPUT_SCRIPT clothcad_deploy NO_UNSUPPORTED_PLATFORM_ERROR NO_TRANSLATIONS
        DEPLOY_TOOL_OPTIONS --no-system-d3d-compiler --no-opengl-sw)
    install(SCRIPT ${clothcad_deploy})
    # The compiler's runtime, so that the package starts where it is not installed.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    include(InstallRequiredSystemLibraries)
endif()
