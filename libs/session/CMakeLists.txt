# One open document and everything worked out from it, for any window to
# show: editing with undo, the mesh and arrangement, the body, the cloth on
# its own thread. Knows nothing of windows.
find_package(Threads REQUIRED)
twintailor_library(session
    SOURCES
        src/Autosave.cpp
        src/Commands.cpp
        src/Drafting.cpp
        src/Editor.cpp
        src/FrameTimes.cpp
        src/Lengths.cpp
        src/Locks.cpp
        src/Offers.cpp
        src/Samples.cpp
        src/Session.cpp
        src/Simulation.cpp
        src/TableGizmo.cpp
    DEPENDS twintailor::core twintailor::sim Threads::Threads
)
if(TWINTAILOR_BUILD_TESTS)
    add_subdirectory(tests)
endif()
