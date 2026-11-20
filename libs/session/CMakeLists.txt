# One open document and everything worked out from it, for any window to
# show: editing with undo, the mesh and arrangement, the body, the cloth on
# its own thread. Knows nothing of windows.
find_package(Threads REQUIRED)
clothcad_library(session
    SOURCES
        src/Drafting.cpp
        src/Editor.cpp
        src/Lengths.cpp
        src/Locks.cpp
        src/Offers.cpp
        src/Samples.cpp
        src/Session.cpp
        src/Simulation.cpp
    DEPENDS clothcad::core clothcad::sim Threads::Threads
)
if(CLOTHCAD_BUILD_TESTS)
    add_subdirectory(tests)
endif()
