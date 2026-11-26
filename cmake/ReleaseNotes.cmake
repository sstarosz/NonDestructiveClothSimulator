# What a release tag stands for: stops unless the tag is the version in the
# root CMakeLists.txt (v0.1.0, or a try of it such as v0.1.0-rc1), and takes
# that version's section of CHANGELOG.md as the release's notes.
#
#   cmake -DTAG=v0.1.0 -DNOTES=notes.md -P cmake/ReleaseNotes.cmake
#
# It prints "version=0.1.0" and, for a try, "prerelease=true"; with
# -DOUTPUTS=<file> the same lines are added to that file (GitHub's outputs).

cmake_minimum_required(VERSION 3.25)

foreach(needed TAG NOTES)
    if(NOT DEFINED ${needed})
        message(FATAL_ERROR "Give -D${needed}=...")
    endif()
endforeach()
get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

file(READ "${root}/CMakeLists.txt" lists)
if(NOT lists MATCHES "project\\(NonDestructiveClothCad VERSION ([0-9]+\\.[0-9]+\\.[0-9]+)")
    message(FATAL_ERROR "CMakeLists.txt names no version in project().")
endif()
set(version "${CMAKE_MATCH_1}")

if(NOT TAG MATCHES "^v([0-9]+\\.[0-9]+\\.[0-9]+)(-rc[0-9]+)?$")
    message(FATAL_ERROR "The tag '${TAG}' is not of the form v${version} or v${version}-rc1.")
endif()
set(tagged "${CMAKE_MATCH_1}")
set(prerelease false)
if(CMAKE_MATCH_2)
    set(prerelease true)
endif()
if(NOT tagged STREQUAL version)
    message(FATAL_ERROR "The tag '${TAG}' is not the version in CMakeLists.txt, ${version}. "
                        "Change project() there, or tag v${version}.")
endif()

# The section runs from "## <version>" to the next "## ".
file(READ "${root}/CHANGELOG.md" log)
string(REPLACE "\r" "" log "${log}")
set(log "\n${log}")
string(FIND "${log}" "\n## ${version}" from)
if(from EQUAL -1)
    message(FATAL_ERROR "CHANGELOG.md has no section '## ${version}'. Write the release's notes there.")
endif()
string(SUBSTRING "${log}" ${from} -1 rest)
string(SUBSTRING "${rest}" 1 -1 rest)  # past the line break before the heading
string(FIND "${rest}" "\n" end_of_heading)
if(end_of_heading EQUAL -1)
    set(section "")
else()
    string(SUBSTRING "${rest}" ${end_of_heading} -1 section)
endif()
string(FIND "${section}" "\n## " next)
if(NOT next EQUAL -1)
    string(SUBSTRING "${section}" 0 ${next} section)
endif()
string(STRIP "${section}" section)
if(section STREQUAL "")
    message(FATAL_ERROR "The section '## ${version}' of CHANGELOG.md is empty. Write the release's notes there.")
endif()
file(WRITE "${NOTES}" "${section}\n")

set(outputs "version=${version}\nprerelease=${prerelease}\n")
if(DEFINED OUTPUTS)
    file(APPEND "${OUTPUTS}" "${outputs}")
endif()
message("${outputs}")
