# Checks that the workflows name exact tools, so that a commit is built the same
# way on another day, and a release is what was tried: every action of someone
# else's is taken at a commit, with its version in a comment beside it, and the
# Vulkan SDK by its number.
#
#   cmake -DFOLDER=.github/workflows -P cmake/CheckWorkflows.cmake
#
#   uses: actions/checkout@11d5960a326750d5838078e36cf38b85af677262 # v4.4.0
#
# To take a newer one, change the commit and the comment together.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED FOLDER)
    message(FATAL_ERROR "Give -DFOLDER=...")
endif()

file(GLOB workflows "${FOLDER}/*.yml" "${FOLDER}/*.yaml")
if(NOT workflows)
    message(FATAL_ERROR "There is no workflow in ${FOLDER}.")
endif()

set(commit "")
foreach(i RANGE 1 40)
    string(APPEND commit "[0-9a-f]")
endforeach()

set(actions 0)
foreach(workflow IN LISTS workflows)
    get_filename_component(name "${workflow}" NAME)
    file(STRINGS "${workflow}" lines)
    foreach(line IN LISTS lines)
        if(line MATCHES "^[ \t-]*uses:[ \t]*(.*)$")
            set(used "${CMAKE_MATCH_1}")
            if(used MATCHES "^\\./")
                continue()  # a workflow of this repository, at the same commit
            endif()
            if(NOT used MATCHES "^[^@ ]+@${commit}[ \t]+# v[0-9]+\\.[0-9]+\\.[0-9]+$")
                message(FATAL_ERROR "${name} uses '${used}'. Name the action's commit, with its version in a "
                                    "comment: owner/action@<40 characters> # v1.2.3")
            endif()
            math(EXPR actions "${actions} + 1")
        elseif(line MATCHES "^[ \t]*vulkan_version:[ \t]*(.*)$")
            set(version "${CMAKE_MATCH_1}")
            if(NOT version MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$")
                message(FATAL_ERROR "${name} installs the Vulkan SDK '${version}'. Name its version: 1.2.3.4")
            endif()
        endif()
    endforeach()
endforeach()

message(STATUS "The workflows name exact tools: ${actions} actions")
