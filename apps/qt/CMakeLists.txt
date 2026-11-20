# The Qt window (0.1.1 step 5). Built when Qt 6 is found: on Windows the
# presets point CMAKE_PREFIX_PATH at C:/Qt/6.12.0/msvc2022_64. The window's
# parts are a library, so that tests can drive them without a screen.
find_package(Qt6 6.2 QUIET COMPONENTS Widgets)
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
    ClothPanel.cpp
    Icons.cpp
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
endif()
