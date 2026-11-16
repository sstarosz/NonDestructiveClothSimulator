# The window of 0.1, made with Dear ImGui. It is replaced by the Qt window;
# until then it keeps working.
find_package(imgui CONFIG REQUIRED)
find_package(Threads REQUIRED)

# Everything of the window except opening it and the graphics card, so that
# the integration tests and benchmarks can run it without either.
add_library(clothcad_imgui_ui STATIC
    App.cpp
    Canvas.cpp
    Locks.cpp
    Picture.cpp
    Samples.cpp
    View3D.cpp
)
add_library(clothcad::imgui_ui ALIAS clothcad_imgui_ui)
target_include_directories(clothcad_imgui_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(clothcad_imgui_ui PUBLIC clothcad::core clothcad::sim clothcad::session imgui::imgui Threads::Threads)
# Where the avatar that comes with the project is, so the program finds it wherever it is run from.
target_compile_definitions(clothcad_imgui_ui PRIVATE CLOTHCAD_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets")
clothcad_warnings(clothcad_imgui_ui)

if(CLOTHCAD_WINDOW)
    find_package(glfw3 CONFIG REQUIRED)
    find_package(OpenGL REQUIRED)
    add_executable(clothcad main.cpp Texture.cpp)
    target_link_libraries(clothcad PRIVATE clothcad::imgui_ui glfw OpenGL::GL)
    clothcad_warnings(clothcad)
endif()
