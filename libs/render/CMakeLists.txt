# The 3D view's drawing, with Vulkan through Vulkan-Hpp. Built when the
# Vulkan headers and glslangValidator are found: both come with the Vulkan
# SDK (CMake finds it through the VULKAN_SDK variable the installer sets).
# Vulkan itself is loaded when the program runs, so nothing is linked.
find_package(Vulkan QUIET COMPONENTS glslangValidator)
if(NOT Vulkan_FOUND OR NOT TARGET Vulkan::Headers OR NOT Vulkan_GLSLANG_VALIDATOR_EXECUTABLE)
    message(STATUS "The Vulkan SDK was not found: the 3D view is not built. Install it from vulkan.lunarg.com.")
    return()
endif()

# The shaders, compiled to SPIR-V arrays in headers, so the program needs no files beside it.
set(shader_headers)
foreach(shader surface.vert:surfaceVertexCode surface.frag:surfaceFragmentCode line.vert:lineVertexCode line.frag:lineFragmentCode)
    string(REPLACE ":" ";" parts ${shader})
    list(GET parts 0 file)
    list(GET parts 1 name)
    set(out ${CMAKE_CURRENT_BINARY_DIR}/shaders/${file}.h)
    add_custom_command(OUTPUT ${out}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
        COMMAND Vulkan::glslangValidator -V --target-env vulkan1.1 --vn ${name} -o ${out} ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/${file}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/${file}
        COMMENT "Compiling the shader ${file}")
    list(APPEND shader_headers ${out})
endforeach()

twintailor_library(render
    SOURCES
        src/Camera.cpp
        src/Gizmo.cpp
        src/Picking.cpp
        src/Renderer.cpp
        src/SceneOf.cpp
        ${shader_headers}
    DEPENDS twintailor::session
    PRIVATE_DEPENDS Vulkan::Headers
)
target_include_directories(twintailor_render PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
# Vulkan-Hpp finds Vulkan's functions when the program runs.
target_compile_definitions(twintailor_render PRIVATE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
if(CMAKE_DL_LIBS)
    target_link_libraries(twintailor_render PRIVATE ${CMAKE_DL_LIBS})
endif()

if(TWINTAILOR_BUILD_TESTS)
    add_subdirectory(tests)
endif()
