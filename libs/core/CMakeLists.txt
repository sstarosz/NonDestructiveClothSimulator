# The document model: parameters, panels, operations, seams; formulas;
# evaluation to 2D outlines; files. Uses nothing but the standard library.
clothcad_library(core SOURCES
    src/Document.cpp
    src/Drawing.cpp
    src/Evaluate.cpp
    src/Expr.cpp
    src/Fields.cpp
    src/File.cpp
    src/Outline.cpp
    src/Pattern.cpp
    src/References.cpp
    src/Text.cpp
)
if(CLOTHCAD_BUILD_TESTS)
    add_subdirectory(tests)
endif()
