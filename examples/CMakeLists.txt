# Five garments built through the API, each a small program that checks
# its own numbers. They also run as integration tests.
foreach(example 01_waistband_skirt 02_circle_skirt 03_asymmetric_hem 04_mirrored_sleeve 05_shared_base)
    add_executable(example_${example} ${example}.cpp)
    target_link_libraries(example_${example} PRIVATE clothcad::core)
    clothcad_warnings(example_${example})
    if(CLOTHCAD_BUILD_TESTS)
        add_test(NAME "example: ${example}" COMMAND example_${example})
        set_tests_properties("example: ${example}" PROPERTIES LABELS integration)
    endif()
endforeach()
