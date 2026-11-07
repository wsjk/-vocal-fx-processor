# Raspberry Pi Pico (RP2040) build. This is a SEPARATE CMake project from the desktop build because it
# needs the Pico SDK and an ARM cross-compiler.
#
# One-time setup
#   1. Install the Arm toolchain and CMake.  macOS:  brew install cmake
#                                                     brew install --cask gcc-arm-embedded
#   2. Get the Pico SDK (with submodules) and point PICO_SDK_PATH at it:
#        git clone --recurse-submodules https://github.com/raspberrypi/pico-sdk.git ~/pico-sdk
#        export PICO_SDK_PATH=~/pico-sdk
#   3. Copy the SDK's import helper next to this file (the SDK ships it; this project does not):
#        cp $PICO_SDK_PATH/external/pico_sdk_import.cmake pico/
#
# Build
#   cmake -S pico -B pico/build -DCMAKE_BUILD_TYPE=Release
#   cmake --build pico/build
#
# Flash and read the results
#   Hold BOOTSEL while plugging in the Pico, then copy pico/build/vocal_pico_bench.uf2 to the RPI-RP2 drive.
#   Open the serial port that appears (macOS: ls /dev/tty.usbmodem*), e.g.  screen /dev/tty.usbmodem* 115200
cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)   # must come before project()

project(vocal_pico C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(vocal_pico_bench
    main_pico.cpp
    ../src/demonic_engine.cpp)

target_include_directories(vocal_pico_bench PRIVATE ../include)
target_link_libraries(vocal_pico_bench PRIVATE pico_stdlib)

pico_enable_stdio_usb(vocal_pico_bench 1)    # printf over the USB cable
pico_enable_stdio_uart(vocal_pico_bench 0)
pico_add_extra_outputs(vocal_pico_bench)     # also produce the .uf2 file you drag onto the Pico
