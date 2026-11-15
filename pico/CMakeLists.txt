# Raspberry Pi RP2350 benchmark build (WeAct RP2350B Core by default). This is a SEPARATE CMake project
# from the desktop build because it needs the Pico SDK and an ARM cross-compiler.
# Why this board: docs/HARDWARE_PLAN.md.
#
# One-time setup
#   1. Install the Arm toolchain and CMake.  macOS:  brew install cmake
#                                                     brew install --cask gcc-arm-embedded
#   2. Get the Pico SDK 2.x (with submodules) and point PICO_SDK_PATH at it:
#        git clone --recurse-submodules https://github.com/raspberrypi/pico-sdk.git ~/pico-sdk
#        export PICO_SDK_PATH=~/pico-sdk
#
# Build
#   cmake -S pico -B pico/build -DCMAKE_BUILD_TYPE=Release
#   cmake --build pico/build
# Other boards: add -DPICO_BOARD=pico2 (Raspberry Pi Pico 2) or -DPICO_BOARD=pico (original RP2040 Pico)
# to the first command, in a fresh build folder.
#
# Flash and read the results
#   Hold BOOT (BOOTSEL on a Pico) while plugging the board in, then copy pico/build/vocal_pico_bench.uf2 to
#   the drive that appears (RP2350, or RPI-RP2 for an RP2040).
#   Open the serial port that appears (macOS: ls /dev/tty.usbmodem*), e.g.  screen /dev/tty.usbmodem* 115200
cmake_minimum_required(VERSION 3.13)

# The board decides the chip. Its platform is "rp2350", which the SDK builds for the Arm Cortex-M33 cores
# (they have the FPU; the RISC-V Hazard3 cores do not).
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD weact_studio_rp2350b_core)
endif()

include(pico_sdk_import.cmake)   # must come before project(); a copy of $PICO_SDK_PATH/external/pico_sdk_import.cmake

project(vocal_pico C CXX ASM)
include(../cmake/version.cmake)   # VFX_VERSION, from the latest v* git tag
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(vocal_pico_bench
    main_pico.cpp
    ../src/demonic_engine.cpp)

target_include_directories(vocal_pico_bench PRIVATE ../include)
target_link_libraries(vocal_pico_bench PRIVATE pico_stdlib)
target_compile_definitions(vocal_pico_bench PRIVATE VFX_VERSION="${VFX_VERSION}")

pico_enable_stdio_usb(vocal_pico_bench 1)    # printf over the USB cable
pico_enable_stdio_uart(vocal_pico_bench 0)
pico_add_extra_outputs(vocal_pico_bench)     # also produce the .uf2 file you drag onto the board
