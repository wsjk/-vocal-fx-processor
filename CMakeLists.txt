cmake_minimum_required(VERSION 3.15)
project(vocal_fx_processor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable CTest
enable_testing()

# Include directories
include_directories(include)

# Core engine library
add_library(demonic_engine src/demonic_engine.cpp)

# Test target
add_executable(engine_smoke_test tests/engine_smoke_test.cpp)
target_link_libraries(engine_smoke_test PRIVATE demonic_engine)
add_test(NAME EngineSmokeTest COMMAND engine_smoke_test)

# Configure Local PortAudio (Disable JACK and shared libs)
set(PA_USE_JACK OFF CACHE BOOL "" FORCE)
set(PA_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PA_BUILD_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(portaudio)

# Automatically fetch and build Raylib from source
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(SUPPORT_FILEFORMAT_PIX OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
)
FetchContent_MakeAvailable(raylib)

# Desktop App target
add_executable(vocal_desktop desktop/main_desktop.cpp)
target_link_libraries(vocal_desktop PRIVATE demonic_engine portaudio raylib)
target_include_directories(vocal_desktop PRIVATE portaudio/include)

message(STATUS "Self-contained build configured successfully.")
