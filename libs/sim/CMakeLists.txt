# Panels in space: meshing, arrangement, bodies and the cloth solver.
find_package(CDT CONFIG REQUIRED)
find_package(Threads REQUIRED)
clothcad_library(sim
    SOURCES
        src/Body.cpp
        src/Cloth.cpp
        src/Layout.cpp
        src/Mesh.cpp
    DEPENDS clothcad::core
    PRIVATE_DEPENDS CDT::CDT
)
if(CLOTHCAD_BUILD_TESTS)
    add_subdirectory(tests)
endif()
