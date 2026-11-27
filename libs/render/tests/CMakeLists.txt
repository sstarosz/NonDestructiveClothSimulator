# One test file per source file of the library. The renderer's tests draw
# with whatever Vulkan the machine has, and are skipped where it has none.
twintailor_unit_tests(render SOURCES
    CameraTests.cpp
    GizmoTests.cpp
    PickingTests.cpp
    RendererTests.cpp
    SceneOfTests.cpp
)
