cmake_minimum_required(VERSION 3.13)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

project(vocal_fx_processor C CXX)
set(CMAKE_CXX_STANDARD 17)

# Fetch Raylib for GUI and OLED simulation
include(FetchContent)
FetchContent_Declare(
    raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/5.0.tar.gz
)
FetchContent_MakeAvailable(raylib)

# Locate PortAudio across Apple Silicon (/opt/homebrew), Intel Mac (/usr/local), and Linux
find_path(PORTAUDIO_INCLUDE_DIR portaudio.h
    PATHS
    /opt/homebrew/include
    /usr/local/include
    /usr/include
)

find_library(PORTAUDIO_LIBRARY NAMES portaudio
    PATHS
    /opt/homebrew/lib
    /usr/local/lib
    /usr/lib
)

add_executable(vocal_desktop
    desktop/main_desktop.cpp
    src/demonic_engine.cpp
)

target_include_directories(vocal_desktop PRIVATE 
    include 
)

if(PORTAUDIO_INCLUDE_DIR)
    target_include_directories(vocal_desktop PRIVATE ${PORTAUDIO_INCLUDE_DIR})
endif()
if(EXISTS /opt/homebrew/include)
    target_include_directories(vocal_desktop PRIVATE /opt/homebrew/include)
endif()
if(EXISTS /usr/local/include)
    target_include_directories(vocal_desktop PRIVATE /usr/local/include)
endif()

if(EXISTS /opt/homebrew/lib)
    target_link_directories(vocal_desktop PRIVATE /opt/homebrew/lib)
endif()
if(EXISTS /usr/local/lib)
    target_link_directories(vocal_desktop PRIVATE /usr/local/lib)
endif()

if(PORTAUDIO_LIBRARY)
    target_link_libraries(vocal_desktop PRIVATE ${PORTAUDIO_LIBRARY})
else()
    target_link_libraries(vocal_desktop PRIVATE portaudio)
endif()

target_link_libraries(vocal_desktop PRIVATE raylib)

if(APPLE)
    target_link_libraries(vocal_desktop PRIVATE
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework AudioUnit"
        "-framework CoreServices"
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
    )
endif()

# Headless smoke test executable
add_executable(engine_smoke_test
    tests/engine_smoke_test.cpp
    src/demonic_engine.cpp
)

target_include_directories(engine_smoke_test PRIVATE 
    include
)
