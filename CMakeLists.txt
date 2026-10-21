cmake_minimum_required(VERSION 3.13)

project(vocal_fx_processor C CXX ASM)
set(CMAKE_CXX_STANDARD 17)

# Desktop Target Mode for macOS / Linux testing
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
    "-framework CoreAudio"
    "-framework AudioToolbox"
    "-framework AudioUnit"
    "-framework CoreServices"
)
