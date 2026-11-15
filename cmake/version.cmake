# Sets VFX_VERSION from git, for both the desktop/test build and the Pico build.
#   On a release commit:   0.3.0              (tag v0.3.0, made by .github/workflows/release.yml)
#   After a release:       0.3.0-2-gabc1234   (2 commits after v0.3.0)
#   Uncommitted changes:   ...-dirty
#   No release tag yet:    0.0.0-gabc1234
# It is read when CMake configures, so re-run cmake to refresh it after a new commit or tag.
find_package(Git QUIET)
set(VFX_VERSION "unknown")
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --match "v[0-9]*" --dirty
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
        OUTPUT_VARIABLE _vfx_describe OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _vfx_result ERROR_QUIET)
    if(_vfx_result EQUAL 0)
        string(REGEX REPLACE "^v" "" VFX_VERSION "${_vfx_describe}")
    else()
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --always --dirty
            WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
            OUTPUT_VARIABLE _vfx_describe OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _vfx_result ERROR_QUIET)
        if(_vfx_result EQUAL 0)
            set(VFX_VERSION "0.0.0-g${_vfx_describe}")
        endif()
    endif()
endif()
message(STATUS "vocal-fx version: ${VFX_VERSION}")
