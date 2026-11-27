# Panels in space: meshing, arrangement, bodies and the cloth solver.
find_package(CDT CONFIG REQUIRED)
find_package(Threads REQUIRED)
clothcad_library(sim
    SOURCES
        src/AvatarImport.cpp
        src/Body.cpp
        src/Cloth.cpp
        src/Export.cpp
        src/Layout.cpp
        src/Mesh.cpp
        src/Normals.cpp
    DEPENDS clothcad::core
    PRIVATE_DEPENDS CDT::CDT Threads::Threads
)
if(CLOTHCAD_BUILD_TESTS)
    add_subdirectory(tests)
endif()
