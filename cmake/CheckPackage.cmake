# Checks a folder made by `cmake --install`, as it would be zipped and handed
# out: what a user needs is there, nothing is there that THIRD-PARTY.md does
# not speak of, and the installed program starts and says its version.
#
#   cmake -DFOLDER=<installed folder> -DVERSION=0.1.0 -P cmake/CheckPackage.cmake
#
# With -DBUILD=<build folder> -DCONFIG=Release the folder is made first, anew.

cmake_minimum_required(VERSION 3.25)

foreach(needed FOLDER VERSION)
    if(NOT DEFINED ${needed})
        message(FATAL_ERROR "Give -D${needed}=...")
    endif()
endforeach()

if(DEFINED BUILD)
    file(REMOVE_RECURSE "${FOLDER}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --install "${BUILD}" --config "${CONFIG}" --prefix "${FOLDER}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "The install failed (${result}):\n${output}")
    endif()
endif()

set(needed
    bin/clothcad.exe
    bin/Qt6Core.dll
    bin/Qt6Gui.dll
    bin/Qt6Widgets.dll
    bin/qt.conf
    plugins/platforms/qwindows.dll
    bin/msvcp140.dll
    bin/vcruntime140.dll
    bin/vcruntime140_1.dll
    bin/assets/avatarV1.obj
    bin/LICENSE
    bin/THIRD-PARTY.md
    bin/licenses/GPL-3.0.txt
    bin/licenses/LGPL-3.0.txt
    bin/licenses/MPL-2.0.txt
    bin/licenses/qt-third-party.txt)
foreach(file IN LISTS needed)
    if(NOT EXISTS "${FOLDER}/${file}")
        message(FATAL_ERROR "The package lacks ${file}.")
    endif()
endforeach()

# Every kind of file the package may hold. Something new that ships needs its
# notice in THIRD-PARTY.md first, and then a line here.
set(allowed
    "bin/clothcad\\.exe"
    "bin/Qt6(Core|Gui|Widgets)\\.dll"
    "bin/qt\\.conf"
    "plugins/platforms/qwindows\\.dll"
    "plugins/styles/qmodernwindowsstyle\\.dll"
    "bin/(msvcp140[a-z0-9_]*|vcruntime140[a-z0-9_]*|concrt140)\\.dll"
    "bin/assets/[^/]+\\.obj"
    "bin/LICENSE"
    "bin/THIRD-PARTY\\.md"
    "bin/licenses/[^/]+\\.txt")
file(GLOB_RECURSE shipped RELATIVE "${FOLDER}" "${FOLDER}/*")
foreach(file IN LISTS shipped)
    set(known FALSE)
    foreach(pattern IN LISTS allowed)
        if(file MATCHES "^${pattern}$")
            set(known TRUE)
        endif()
    endforeach()
    if(NOT known)
        message(FATAL_ERROR "The package holds ${file}, which THIRD-PARTY.md does not speak of. "
                            "Write its notice there and allow it in cmake/CheckPackage.cmake, or leave it out.")
    endif()
endforeach()

# Started with only Windows' own folders on the path, so that nothing of this
# machine's Qt is used. A missing library would show a dialog and wait: hence
# the time limit.
set(path "$ENV{PATH}")
set(ENV{PATH} "$ENV{SystemRoot}\\System32;$ENV{SystemRoot}")
execute_process(
    COMMAND "${FOLDER}/bin/clothcad.exe" --version
    WORKING_DIRECTORY "${FOLDER}/bin"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
set(ENV{PATH} "${path}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "The installed program did not start (${result}). ${errors}")
endif()
string(REPLACE "." "\\." pattern "${VERSION}")
if(NOT output MATCHES "Cloth CAD ${pattern} \\(([0-9a-f]+|unknown)\\)")
    message(FATAL_ERROR "The installed program says '${output}', not version ${VERSION}.")
endif()

list(LENGTH shipped count)
message(STATUS "The package is complete: ${count} files, ${output}")
