cmake_minimum_required(VERSION 3.13)
project(vocal_fx_processor C CXX ASM)
set(CMAKE_CXX_STANDARD 17)

# Fetch Raylib for GUI and OLED simulation
include(FetchContent)
FetchContent_Declare(
    raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/5.0.tar.gz
)
FetchContent_MakeAvailable(raylib)

add_executable(vocal_desktop
    desktop/main_desktop.cpp
    src/demonic_engine.cpp
    src/dsp_pipeline.cpp
)

target_include_directories(vocal_desktop PRIVATE 
    include 
    /usr/local/include
)

target_link_directories(vocal_desktop PRIVATE 
    /usr/local/lib
)

target_link_libraries(vocal_desktop PRIVATE 
    portaudio
    raylib
    "-framework CoreAudio"
    "-framework AudioToolbox"
    "-framework AudioUnit"
    "-framework CoreServices"
    "-framework OpenGL"
    "-framework Cocoa"
    "-framework IOKit"
)
