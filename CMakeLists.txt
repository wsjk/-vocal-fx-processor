cmake_minimum_required(VERSION 3.18)   # FetchContent SOURCE_SUBDIR
project(vocal_fx_processor C CXX)
include(cmake/version.cmake)   # VFX_VERSION, from the latest v* git tag

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Real-time DSP wants optimisation: default to a Release build unless told otherwise.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BUILD_DESKTOP     "Build the desktop app (needs PortAudio and raylib)"            ON)
option(BUILD_TESTS       "Build the headless DSP tests (no audio hardware needed)"      ON)
option(USE_SYSTEM_RAYLIB "Use an installed raylib (>= 5.0) instead of downloading 5.0"   OFF)

# ----------------------------------------------------------------------------------------------
# DaisySP, for one file: the ReverbSc reverb in its DaisySP-LGPL submodule (LGPL-2.1; see README).
# Downloaded at a pinned commit the first time (needs internet), or, offline, point CMake at a local
# clone made with --recurse-submodules:  -DFETCHCONTENT_SOURCE_DIR_DAISYSP=/path/to/DaisySP
# The Daisy build (daisy/Makefile) compiles the same file.
# ----------------------------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(daisysp
    GIT_REPOSITORY https://github.com/electro-smith/DaisySP.git
    GIT_TAG        2c72eaf9eac5fc0dca1919d65d606da907832618
    GIT_SUBMODULES DaisySP-LGPL
    SOURCE_SUBDIR  no-cmake)   # only fetch: DaisySP's own CMake build is not used
FetchContent_MakeAvailable(daisysp)
set(DAISYSP_LGPL_SOURCE ${daisysp_SOURCE_DIR}/DaisySP-LGPL/Source)
if(NOT EXISTS ${DAISYSP_LGPL_SOURCE}/Effects/reverbsc.cpp)
    message(FATAL_ERROR "DaisySP-LGPL is missing from ${daisysp_SOURCE_DIR}: clone DaisySP with --recurse-submodules.")
endif()

add_library(daisysp_reverbsc STATIC ${DAISYSP_LGPL_SOURCE}/Effects/reverbsc.cpp)
target_include_directories(daisysp_reverbsc PUBLIC ${DAISYSP_LGPL_SOURCE})

# ----------------------------------------------------------------------------------------------
# The DSP engine: no audio or graphics dependencies (only ReverbSc). Shared by the desktop app and
# the tests; the Daisy build compiles the same sources.
# ----------------------------------------------------------------------------------------------
add_library(demonic_engine STATIC src/demonic_engine.cpp)
target_include_directories(demonic_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(demonic_engine PUBLIC daisysp_reverbsc)
if(NOT MSVC)
    target_compile_options(demonic_engine PRIVATE -Wall -Wextra -Wshadow)
endif()

# ----------------------------------------------------------------------------------------------
# Tests:  cmake --build build && ctest --test-dir build --output-on-failure
# ----------------------------------------------------------------------------------------------
if(BUILD_TESTS)
    enable_testing()
    foreach(test_name engine_dsp_test sim_controls_test hw_pinout_test)
        add_executable(${test_name} tests/${test_name}.cpp)
        target_link_libraries(${test_name} PRIVATE demonic_engine)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()

# ----------------------------------------------------------------------------------------------
# Desktop app
# ----------------------------------------------------------------------------------------------
if(BUILD_DESKTOP)

    # --- PortAudio: try pkg-config, then common install locations, then the copy in ./portaudio ---
    set(VFX_PORTAUDIO_TARGET "")

    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(PORTAUDIO QUIET IMPORTED_TARGET portaudio-2.0)
        if(PORTAUDIO_FOUND)
            set(VFX_PORTAUDIO_TARGET PkgConfig::PORTAUDIO)
        endif()
    endif()

    if(NOT VFX_PORTAUDIO_TARGET)
        # /opt/homebrew = Homebrew on Apple Silicon, /usr/local = Homebrew on Intel Macs and most Linux
        find_path(PORTAUDIO_INCLUDE_DIR portaudio.h HINTS /opt/homebrew/include /usr/local/include)
        find_library(PORTAUDIO_LIBRARY portaudio HINTS /opt/homebrew/lib /usr/local/lib)
        if(PORTAUDIO_INCLUDE_DIR AND PORTAUDIO_LIBRARY)
            add_library(vfx_portaudio INTERFACE)
            target_include_directories(vfx_portaudio INTERFACE ${PORTAUDIO_INCLUDE_DIR})
            target_link_libraries(vfx_portaudio INTERFACE ${PORTAUDIO_LIBRARY})
            set(VFX_PORTAUDIO_TARGET vfx_portaudio)
        endif()
    endif()

    if(NOT VFX_PORTAUDIO_TARGET AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/portaudio/CMakeLists.txt)
        message(STATUS "PortAudio not installed; building the copy in ./portaudio")
        set(PA_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(PA_BUILD_TESTS       OFF CACHE BOOL "" FORCE)
        set(PA_BUILD_EXAMPLES    OFF CACHE BOOL "" FORCE)
        add_subdirectory(portaudio EXCLUDE_FROM_ALL)
        set(VFX_PORTAUDIO_TARGET portaudio)
    endif()

    if(NOT VFX_PORTAUDIO_TARGET)
        message(FATAL_ERROR
            "PortAudio was not found. Install it (macOS: 'brew install portaudio', "
            "Debian/Ubuntu: 'sudo apt install portaudio19-dev'), or configure with "
            "-DBUILD_DESKTOP=OFF to build only the engine and tests.")
    endif()
    message(STATUS "PortAudio: ${VFX_PORTAUDIO_TARGET}")

    # --- raylib: download 5.0 by default (needs internet the first time) ---
    if(USE_SYSTEM_RAYLIB)
        find_package(raylib 5.0 REQUIRED)
    else()
        if(POLICY CMP0135)
            cmake_policy(SET CMP0135 NEW)   # stamp extracted files with download time (silences a warning)
        endif()
        if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
            set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Lets raylib 5.0 configure under CMake 4 and newer")
        endif()
        include(FetchContent)
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(raylib URL https://github.com/raysan5/raylib/archive/refs/tags/5.0.tar.gz)
        FetchContent_MakeAvailable(raylib)
    endif()

    add_executable(vocal_desktop desktop/main_desktop.cpp)
    target_link_libraries(vocal_desktop PRIVATE demonic_engine ${VFX_PORTAUDIO_TARGET} raylib)
    target_compile_definitions(vocal_desktop PRIVATE VFX_VERSION="${VFX_VERSION}")

    if(APPLE)
        target_link_libraries(vocal_desktop PRIVATE
            "-framework CoreAudio"
            "-framework AudioToolbox"
            "-framework AudioUnit"
            "-framework CoreServices"
            "-framework OpenGL"
            "-framework Cocoa"
            "-framework IOKit")
    endif()
endif()

# The Daisy Seed build is a separate Makefile project because it needs libDaisy and an Arm
# cross-compiler. See daisy/Makefile.
